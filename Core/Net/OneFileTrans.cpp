#include "OneFileTrans.h"

#include <algorithm>
#include <chrono>
#include <File/FileDir.h>
#include <QDir>
#include <QFileInfo>

#include "Compress/TarXzPacker.h"
#include "Protocol/Serialize.hpp"

namespace {

qint64 NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// SACK 位图最多描述 256 个块，足够覆盖默认窗口（4MB / 64KB = 64 块）
constexpr std::size_t kMaxSackBits = 256;

// 接收方根据文件大小选块：小文件小块（少浪费），大文件大块（少事件循环往返）
std::uint64_t PickBlockSize(std::uint64_t totalSize)
{
    if (totalSize <= 4ull * 1024 * 1024) {
        return defMinBlockSize;   // 64KB
    }
    if (totalSize <= 256ull * 1024 * 1024) {
        return defBlockSize;   // 256KB
    }
    return 1024ull * 1024;   // 1MB
}

}   // namespace

OneFileTrans::OneFileTrans(QObject* parent) : QObject(parent)
{
    sendOrRecvTimeout_ = new QTimer(this);
    sendOrRecvTimeout_->setSingleShot(true);
    connect(sendOrRecvTimeout_, &QTimer::timeout, this, &OneFileTrans::onSendOrRecvTimeout);
}

void OneFileTrans::initSignals()
{
}

void OneFileTrans::onSendOrRecvTimeout()
{
    const auto st = state_.load();
    if (st == TransStatus::Sending) {
        if (completeSent_) {
            // 数据已发完，正在等接收方"落盘并校验完成"的回执。回执丢失则重发 Complete（幂等）。
            if (completeRetry_ < defRetransLimit) {
                ++completeRetry_;
                qWarning() << "等待完成确认超时，重发 Complete, retry:" << completeRetry_ << "file:" << filePath_;
                sendCompleteFrame();
                return;
            }
            failAndInterrupt("等待接收方完成确认超时");
            return;
        }

        if (nextIndex_ >= totalBlocks_ && ackIndex_ >= totalBlocks_) {
            sendCompleteFrame();
            return;
        }

        // 只重传"确实没到"的块：SACK 命中的块不会留在 inflight_ 里
        const qint64 now = NowMs();
        std::vector<std::uint64_t> expired;
        for (const auto& item : inflight_) {
            if (now - item.second.lastSendMs >= defSendTimeout) {
                expired.push_back(item.first);
            }
        }
        // 一个都没超时，但仍有未确认块：多半是 ACK 丢了，挑最早的一块重发探路
        if (expired.empty() && ackIndex_ < totalBlocks_ && !inflight_.empty()) {
            expired.push_back(inflight_.begin()->first);
        }
        for (auto idx : expired) {
            auto& bs = inflight_[idx];
            if (bs.retries >= defRetransLimit) {
                failAndInterrupt("块 " + std::to_string(idx) + " 重传次数超限");
                return;
            }
            ++bs.retries;
            qWarning() << "重传块:" << idx << "retry:" << bs.retries << "file:" << filePath_;
            sendBlock(idx);
        }
        sendOrRecvTimeout_->start(defSendTimeout);
        return;
    }
    if (st == TransStatus::Receving) {
        failAndInterrupt("接收超时");
    }
}

OneFileTrans::TransMode OneFileTrans::getTransMode()
{
    return tMode_;
}

bool OneFileTrans::initTransfer(TransMode mode, const Message& msg, const std::string& targetId, const std::string& ownId,
                                const std::string& uuid)
{
    QMutexLocker locker(&qMut_);
    tMode_ = mode;
    targetId_ = targetId;
    msg_ = msg;
    isArchive_ = msg_.hasFlag(MessageFlag::kArchive);
    totalSize_ = (tMode_ == TransMode::Send ? msg.ff.size : msg.ft.size);
    meta_ = (tMode_ == TransMode::Send ? msg.ff : msg.ft);
    transSize_ = 0;
    uuid_ = uuid;
    curBlockIndex_ = 0;
    ackIndex_ = 0;
    nextIndex_ = 0;
    inflight_.clear();
    recvOutOfOrder_.clear();
    bytesSinceAck_ = 0;
    lastAckMs_ = 0;
    completeSent_ = false;
    completeRetry_ = 0;
    completeAnswered_ = false;
    completeAnswerOk_ = false;
    completeAnswerErr_.clear();
    fileFinalized_ = false;
    resumeOffset_ = 0;
    fingerprint_.clear();
    ownId_ = ownId;
    // 接收方决定本次传输的块大小；发送方先用默认值，收到 Answer/Start 里的参数后覆盖
    blockSize_ = (tMode_ == TransMode::Receive) ? PickBlockSize(totalSize_) : defBlockSize;
    windowBytes_ = defWindowBytes;
    if (msg.blockSize >= defMinBlockSize && msg.blockSize <= defMaxBlockSize) {
        blockSize_ = msg.blockSize;
    }
    if (msg.windowBytes >= 256u * 1024 && msg.windowBytes <= 64u * 1024 * 1024) {
        windowBytes_ = msg.windowBytes;
    }
    // 窗口至少 2 块、最多 256 块：SACK 位图按 256 位设计，窗口再大就有块描述不到
    windowBytes_ = std::clamp<std::uint64_t>(windowBytes_, blockSize_ * 2, blockSize_ * 256);
    totalBlocks_ = blockSize_ > 0 ? (totalSize_ + blockSize_ - 1) / blockSize_ : 0;

    // filePath_ = QString::fromStdString(miniPath::Join(meta_.dir, meta_.name));
    filePath_ = QString::fromStdString(meta_.fullPath);
    // 归档接收：忽略发送方给定的 ft.fullPath（发送方无法预知本机临时目录），
    // 改写到本机临时目录，收完后再解包到清单指定的真实目的路径。
    if (isArchive_ && tMode_ == TransMode::Receive) {
        filePath_ = QDir::tempPath() + QString("/relay_archive_%1.tar.xz").arg(QString::fromStdString(uuid));
    }

    if (state_.load() != TransStatus::Idle) {
        return false;
    }

    if (tMode_ == TransMode::Send) {
        sendFile_.setFileName(filePath_);
        if (!FileDir::EnsureDir(FileDir::cdUp(filePath_)) || !sendFile_.open(QIODevice::ReadOnly)) {
            qWarning() << "打开发送文件失败:" << filePath_ << sendFile_.errorString();
            return false;
        }
        // 指纹随 Request/Answer 带给接收方，供它校验本地 .relaypart 是否属于同一个文件
        fingerprint_ = FileDir::QuickFingerprint(filePath_).toStdString();
        state_.store(TransStatus::Sending, std::memory_order_release);
        // 接收方可能已经有前一段数据（断点续传），从它报告的偏移开始发
        applyResumeOffset(msg.resumeOffset);
    } else {
        // 先写临时文件，校验通过后再原子 rename 到目标路径。
        // 这样中断/取消/崩溃都不会破坏目标路径上原有的文件。
        partPath_ = filePath_ + ".relaypart";
        tryReusePartFile(msg);
        recvFile_.setFileName(partPath_);
        // ReadWrite 而不是 WriteOnly：WriteOnly 会截断，断点续传的已有数据就没了
        if (!FileDir::EnsureDir(FileDir::cdUp(partPath_)) || !recvFile_.open(QIODevice::ReadWrite)) {
            qWarning() << "打开接收临时文件失败:" << partPath_ << recvFile_.errorString();
            return false;
        }
        if (resumeOffset_ > 0) {
            qInfo() << "断点续传：已有" << resumeOffset_ << "字节，从块" << curBlockIndex_ << "继续, file:" << filePath_;
        }
        state_.store(TransStatus::Receving, std::memory_order_release);
    }

    return true;
}

void OneFileTrans::applyTransferParams(std::uint32_t blockSize, std::uint32_t windowBytes)
{
    QMutexLocker locker(&qMut_);
    if (tMode_ != TransMode::Send || state_.load() != TransStatus::Sending || nextIndex_ != 0) {
        return;   // 只有"还没开始发"的发送方能接受协商结果
    }
    if (blockSize < defMinBlockSize || blockSize > defMaxBlockSize) {
        return;
    }
    if (windowBytes < 256u * 1024 || windowBytes > 64u * 1024 * 1024) {
        return;
    }
    blockSize_ = blockSize;
    windowBytes_ = std::clamp<std::uint64_t>(windowBytes, blockSize_ * 2, blockSize_ * 256);
    totalBlocks_ = (totalSize_ + blockSize_ - 1) / blockSize_;
    qInfo() << "协商传输参数 blockSize:" << blockSize_ << "windowBytes:" << windowBytes_ << "file:" << filePath_;
}

void OneFileTrans::applyResumeOffset(std::uint64_t offset)
{
    if (tMode_ != TransMode::Send || blockSize_ == 0 || totalBlocks_ == 0 || offset == 0) {
        return;
    }
    // 只按块对齐续传：块边界之前的字节接收方保证已经落盘
    const std::uint64_t startBlock = std::min<std::uint64_t>(offset / blockSize_, totalBlocks_);
    if (startBlock == 0) {
        return;
    }
    ackIndex_ = startBlock;
    nextIndex_ = startBlock;
    transSize_ = std::min<std::uint64_t>(startBlock * blockSize_, totalSize_);
    qInfo() << "续传：从" << transSize_ << "字节（块" << startBlock << "）开始发送, file:" << filePath_;
    emit signalProcess(transSize_, totalSize_);
}

void OneFileTrans::tryReusePartFile(const Message& msg)
{
    resumeOffset_ = 0;
    curBlockIndex_ = 0;
    transSize_ = 0;
    if (!QFileInfo::exists(partPath_)) {
        return;
    }

    const qint64 partSize = QFileInfo(partPath_).size();
    // 三个条件同时满足才敢复用：长度合理、对端给了指纹、指纹一致。
    // 缺一个就丢弃残片从 0 重传——续传出错的代价是把两个文件拼成一个"成功"的坏文件。
    const bool usable = partSize > 0 && static_cast<std::uint64_t>(partSize) <= totalSize_
                        && !msg.resumeFingerprint.empty()
                        && FileDir::QuickFingerprint(partPath_, static_cast<qint64>(totalSize_))
                               == QString::fromStdString(msg.resumeFingerprint);
    if (!usable) {
        qWarning() << "丢弃不可复用的临时文件:" << partPath_ << "partSize:" << partSize
                   << "expect:" << totalSize_ << "hasFingerprint:" << !msg.resumeFingerprint.empty();
        QFile::remove(partPath_);
        return;
    }

    // 只保留块对齐的部分：崩溃可能留下写了一半的块（长度正确但内容撕裂）
    const std::uint64_t keep =
        std::min<std::uint64_t>(static_cast<std::uint64_t>(partSize) / blockSize_ * blockSize_, totalSize_);
    if (keep == 0 || !FileDir::ResizeFile(partPath_, static_cast<qint64>(keep))) {
        QFile::remove(partPath_);
        return;
    }
    resumeOffset_ = keep;
    curBlockIndex_ = keep / blockSize_;
    transSize_ = keep;
    qInfo() << "复用临时文件:" << partPath_ << "保留:" << keep << "字节";
}

void OneFileTrans::stopTrans()
{
    handleInterrupt(nullptr);
}

void OneFileTrans::setTargetControlId(const std::string& targetControlId)
{
    targetControlId_ = targetControlId;
}

std::uint64_t OneFileTrans::inflightBytes() const
{
    // 用"发送右沿 - 确认左沿"估算在途字节，SACK 命中的块也计入（保守，不超过接收端缓冲）
    const std::uint64_t blocks = nextIndex_ > ackIndex_ ? nextIndex_ - ackIndex_ : 0;
    return std::min(blocks * blockSize_, totalSize_);
}

void OneFileTrans::sendBlock(std::uint64_t idx)
{
    const std::uint64_t offset = idx * blockSize_;
    if (offset >= totalSize_) {
        return;
    }
    const auto want = static_cast<qint64>(std::min(blockSize_, totalSize_ - offset));
    if (!sendFile_.seek(static_cast<qint64>(offset))) {
        failAndInterrupt("定位源文件失败: " + sendFile_.errorString().toStdString());
        return;
    }
    QByteArray buffer(static_cast<int>(want), Qt::Uninitialized);
    const qint64 got = sendFile_.read(buffer.data(), want);
    if (got != want) {
        failAndInterrupt("读取源文件失败: " + sendFile_.errorString().toStdString());
        return;
    }
    auto frame = CreateFrame(FrameType::kFileType_Request_Chuck);
    frame->index = static_cast<std::int64_t>(idx);
    frame->data.assign(buffer.cbegin(), buffer.cend());

    auto& bs = inflight_[idx];
    bs.lastSendMs = NowMs();
    emit signalRequestSend(frame);
}

bool OneFileTrans::nextSend()
{
    if (TransStatus::Sending != state_.load()) {
        return false;
    }
    if (completeSent_) {
        return true;
    }
    while (nextIndex_ < totalBlocks_ && inflightBytes() < windowBytes_) {
        sendBlock(nextIndex_);
        if (state_.load() != TransStatus::Sending) {
            return false;   // sendBlock 内部失败
        }
        ++nextIndex_;
    }
    if (nextIndex_ >= totalBlocks_ && ackIndex_ >= totalBlocks_) {
        sendCompleteFrame();
        return true;
    }
    if (!inflight_.empty()) {
        sendOrRecvTimeout_->start(defSendTimeout);
    }
    return true;
}

void OneFileTrans::sendCompleteFrame()
{
    if (sendFile_.isOpen()) {
        sendFile_.close();
    }
    auto frame = CreateFrame(FrameType::kFileType_Request_Complete);
    frame->to = targetControlId_;
    frame->from = ownId_;
    frame->index = static_cast<std::int64_t>(totalBlocks_);
    completeSent_ = true;
    emit signalRequestSend(frame);
    sendOrRecvTimeout_->start(defSendTimeout);
}

void OneFileTrans::answerComplete(bool ok, const std::string& err)
{
    Message msg;
    msg.uuid = uuid_;
    msg.msgStateCode = ok ? MessageStateCode::kMessageStateCodeSuccess : MessageStateCode::kMessageStateCodeFailed;
    msg.errMsg = err;

    auto frame = OneFrame::Create();
    frame->type = FrameType::kFileType_Answer_Complete;
    frame->from = ownId_;
    frame->to = targetControlId_;
    frame->fuuid = uuid_;
    // 接收方发出：置方向位，对端据此去 "S" 槽找发送侧的 OneFileTrans
    frame->setFromReceiver(true);
    frame->data = serializeStruct(msg);
    emit signalRequestSend(frame);
}

bool OneFileTrans::handleCompleteAnswer(FramePtr frame)
{
    if (tMode_ != TransMode::Send) {
        return false;
    }
    if (!completeSent_) {
        // 还没进入完成等待阶段，说明是迟到/重复回执，忽略
        return false;
    }
    Message msg;
    try {
        deserializeStruct(frame->data, msg);
    } catch (const std::exception& e) {
        qWarning() << "解析 Answer_Complete 失败:" << e.what() << "file:" << filePath_;
        return false;
    }

    sendOrRecvTimeout_->stop();
    if (msg.msgStateCode == MessageStateCode::kMessageStateCodeSuccess) {
        state_.store(TransStatus::Finished, std::memory_order_release);
        emit signalProcess(totalSize_, totalSize_);
        emit signalFinished(ownId_);
        return true;
    }

    auto err = msg.errMsg.empty() ? std::string("接收方报告传输失败") : msg.errMsg;
    failAndInterrupt(err);
    return true;
}

bool OneFileTrans::handleAck(FramePtr frame)
{
    if (TransStatus::Sending != state_.load()) {
        return false;
    }
    // frame->index = 接收方"下一个期望的块号"，小于它的块都已经落盘
    const auto acked = static_cast<std::uint64_t>(frame->index);
    if (acked > totalBlocks_) {
        qWarning() << "收到非法 ACK, index:" << acked << "total:" << totalBlocks_;
        return false;
    }
    if (completeSent_) {
        return true;   // 已发完，迟到的 ACK 忽略
    }
    if (acked < ackIndex_) {
        return false;   // 过期 ACK
    }

    ackIndex_ = acked;
    for (auto it = inflight_.begin(); it != inflight_.end() && it->first < ackIndex_;) {
        it = inflight_.erase(it);
    }
    applySackBitmap(frame->data);

    const auto done = std::min<std::uint64_t>(ackIndex_ * blockSize_, totalSize_);
    if (done > transSize_) {
        transSize_ = done;
        emit signalProcess(transSize_, totalSize_);
    }

    nextSend();
    return true;
}

std::vector<char> OneFileTrans::buildSackBitmap() const
{
    std::vector<char> bitmap((kMaxSackBits + 7) / 8, 0);
    for (auto idx : recvOutOfOrder_) {
        if (idx < curBlockIndex_) {
            continue;
        }
        const auto bit = idx - curBlockIndex_;
        if (bit >= kMaxSackBits) {
            break;
        }
        bitmap[bit / 8] = static_cast<char>(static_cast<unsigned char>(bitmap[bit / 8]) | (1u << (bit % 8)));
    }
    return bitmap;
}

void OneFileTrans::applySackBitmap(const std::vector<char>& bitmap)
{
    // bit i 表示块 ackIndex_ + i 已经到达接收方（只是前面还有空洞）
    for (std::size_t i = 0; i < kMaxSackBits; ++i) {
        const std::size_t byteIdx = i / 8;
        if (byteIdx >= bitmap.size()) {
            break;
        }
        if ((static_cast<unsigned char>(bitmap[byteIdx]) & (1u << (i % 8))) == 0) {
            continue;
        }
        const auto idx = ackIndex_ + i;
        if (idx < ackIndex_ || idx >= nextIndex_) {
            continue;
        }
        auto it = inflight_.find(idx);
        if (it != inflight_.end()) {
            inflight_.erase(it);   // 已收到，不必再重传
        }
    }
}

bool OneFileTrans::shouldSendAck() const
{
    return bytesSinceAck_ >= defAckBytes || (NowMs() - lastAckMs_) >= defAckDelayMs;
}

void OneFileTrans::sendAck()
{
    auto frame = CreateFrame(FrameType::kFileType_Request_Ack);
    frame->index = static_cast<std::int64_t>(curBlockIndex_);   // 累积确认：下一个期望块
    frame->data = buildSackBitmap();
    emit signalRequestSend(frame);
    bytesSinceAck_ = 0;
    lastAckMs_ = NowMs();
}

bool OneFileTrans::writeAll(const char* data, qint64 size)
{
    qint64 written = 0;
    while (written < size) {
        const qint64 n = recvFile_.write(data + written, size - written);
        if (n <= 0) {
            return false;
        }
        written += n;
    }
    return true;
}

bool OneFileTrans::handleRecvChuck(FramePtr frame)
{
    if (state_.load() != TransStatus::Receving) {
        return false;
    }
    const auto idx = static_cast<std::uint64_t>(frame->index);
    if (idx >= totalBlocks_) {
        qWarning() << "收到越界块, index:" << idx << "total:" << totalBlocks_ << "file:" << filePath_;
        return false;
    }

    if (idx < curBlockIndex_ || recvOutOfOrder_.count(idx) != 0) {
        // 重复块（发送方没收到 ACK）：幂等回一个 ACK，让它别再重传
        sendAck();
        return true;
    }

    // 关键：先落盘，写成功后才计入接收进度并回 ACK。
    // 若先 ACK 再写盘，磁盘满/网络盘掉线/权限错误时会静默丢数据：
    // 发送方推进窗口、双方都报成功，文件却是坏的。
    const qint64 offset = static_cast<qint64>(idx * blockSize_);
    if (!recvFile_.seek(offset) || !writeAll(frame->data.data(), static_cast<qint64>(frame->data.size()))) {
        failAndInterrupt("写入文件失败: " + recvFile_.errorString().toStdString());
        return false;
    }

    if (idx == curBlockIndex_) {
        ++curBlockIndex_;
        while (recvOutOfOrder_.erase(curBlockIndex_) > 0) {
            ++curBlockIndex_;
        }
    } else {
        recvOutOfOrder_.insert(idx);
        // 正常发送方的乱序块不会超过窗口；超过说明对端在乱发，直接判失败，避免集合无界增长
        if (recvOutOfOrder_.size() > 8192) {
            failAndInterrupt("乱序块过多，疑似对端行为异常");
            return false;
        }
    }

    transSize_ += static_cast<std::uint64_t>(frame->data.size());
    bytesSinceAck_ += static_cast<std::uint64_t>(frame->data.size());
    sendOrRecvTimeout_->start(defRecvTimeout);

    if (curBlockIndex_ % 10 == 0 || curBlockIndex_ >= totalBlocks_) {
        emit signalProcess(std::min<std::uint64_t>(transSize_, totalSize_), totalSize_);
    }

    // 累积 ACK：收到足够字节 / 延迟到点 / 全部收齐，三选一
    if (curBlockIndex_ >= totalBlocks_ || shouldSendAck()) {
        sendAck();
    }
    return true;
}

FramePtr OneFileTrans::CreateFrame(FrameType type)
{
    auto frame = OneFrame::Create();
    frame->type = type;
    frame->to = targetId_;
    frame->from = ownId_;
    frame->fuuid = uuid_;
    frame->index = static_cast<std::int64_t>(curBlockIndex_);
    frame->setFromReceiver(tMode_ == TransMode::Receive);

    if (static_cast<uint16_t>(type) < defDirectChuckAck) {
        Message msg;
        msg.to.clientId = targetId_;
        msg.uuid = uuid_;
        frame->data = serializeStruct(msg);
    }
    return frame;
}

void OneFileTrans::discardPartFile()
{
    if (tMode_ != TransMode::Receive || fileFinalized_ || partPath_.isEmpty()) {
        return;
    }
    if (recvFile_.isOpen()) {
        recvFile_.close();
    }
    if (QFile::exists(partPath_)) {
        if (QFile::remove(partPath_)) {
            qWarning() << "已删除未完成的临时文件:" << partPath_;
        } else {
            qWarning() << "删除未完成的临时文件失败:" << partPath_;
        }
    }
}

bool OneFileTrans::finalizeRecvFile(QString& err)
{
    // 1) 刷盘并关闭，检查真实落盘长度
    if (recvFile_.isOpen()) {
        if (!recvFile_.flush()) {
            err = QString("刷新磁盘缓冲失败: %1").arg(recvFile_.errorString());
            recvFile_.close();
            return false;
        }
        recvFile_.close();
    }
    const qint64 onDisk = QFileInfo(partPath_).size();
    if (transSize_ != totalSize_ || static_cast<std::uint64_t>(onDisk) != totalSize_) {
        err = QString("接收字节数不一致：期望 %1，已接收 %2，落盘 %3")
                  .arg(totalSize_)
                  .arg(transSize_)
                  .arg(onDisk);
        return false;
    }

    // 2) 原子替换目标文件：优先直接 rename（Windows 下 QFile::rename 走
    //    MoveFileEx(MOVEFILE_REPLACE_EXISTING)，可以原子覆盖）；不支持时才退化为
    //    "删旧文件再 rename"。
    if (!QFile::rename(partPath_, filePath_)) {
        if (!QFile::exists(filePath_) || !QFile::remove(filePath_) || !QFile::rename(partPath_, filePath_)) {
            err = QString("重命名临时文件失败: %1 -> %2").arg(partPath_, filePath_);
            return false;
        }
    }
    fileFinalized_ = true;
    return true;
}

bool OneFileTrans::handleFinish(FramePtr frame)
{
    sendOrRecvTimeout_->stop();
    if (tMode_ == TransMode::Send) {
        // 发送方正常情况下不会收到 Request_Complete。此前这里会顺手删掉临时归档，
        // 一旦帧被误路由就会毁掉刚发完的数据，改为忽略。
        qWarning() << "发送方收到意外的 Request_Complete，已忽略, file:" << filePath_;
        return true;
    }
    if (completeAnswered_) {
        // 幂等：Complete 重发（回执丢失）时重放上次的结果，不再重复落盘/解包
        answerComplete(completeAnswerOk_, completeAnswerErr_);
        return true;
    }

    QString err;
    if (!finalizeRecvFile(err)) {
        discardPartFile();
        completeAnswered_ = true;
        completeAnswerOk_ = false;
        completeAnswerErr_ = err.toStdString();
        answerComplete(false, completeAnswerErr_);
        state_.store(TransStatus::Interrupted, std::memory_order_release);
        emit signalFailed(ownId_, completeAnswerErr_);
        return true;
    }

    // 数据已完整落盘并替换到目标路径，先给对端明确回执（对端据此才认为成功）。
    // 归档解包可能耗时较长，放在回执之后，避免发送方等超时反复重发。
    completeAnswered_ = true;
    completeAnswerOk_ = true;
    completeAnswerErr_.clear();
    answerComplete(true, "");
    emit signalProcess(totalSize_, totalSize_);

    if (isArchive_) {
        // 压缩传输：归档收完后就地解包，按内嵌清单写入各目的路径并应用
        // 各自权限，随后删除临时归档。归档本身不需要单文件权限同步。
        std::vector<std::string> outPaths;
        std::string extractErr;
        if (!TarXzPacker::extract(filePath_.toStdString(), outPaths, extractErr)) {
            qWarning() << "归档解包失败:" << filePath_ << "err:" << QString::fromStdString(extractErr);
            QFile::remove(filePath_);
            emit signalFailed(ownId_, "归档解包失败: " + extractErr);
            state_.store(TransStatus::Interrupted, std::memory_order_release);
            return true;
        }
        qInfo() << "归档解包完成，文件数:" << outPaths.size() << "归档:" << filePath_;
        QFile::remove(filePath_);
        state_.store(TransStatus::Finished, std::memory_order_release);
        emit signalFinished(ownId_);
        return true;
    }

    // 普通传输：查看是否需要同步权限
    auto ownMark = FileDir::GetMark();
    if (ownMark != msg_.ff.mark) {
        qInfo() << "不同系统，不同步权限，ownMark:" << ownMark << "，msgMark:" << msg_.ff.mark;
    } else {
        // 同步权限
        auto ret = FileDir::SetPermission(filePath_, msg_.ff.permission);
        qInfo() << "相同系统，同步权限，ownMark:" << ownMark << "，msgMark:" << msg_.ff.mark
                << ", permission:" << msg_.ff.permission << ", ret:" << ret;
    }
    state_.store(TransStatus::Finished, std::memory_order_release);
    emit signalFinished(ownId_);
    return true;
}

void OneFileTrans::onFrameReceive(FramePtr frame)
{
    TransStatus st = state_.load();
    if (st == TransStatus::Finished || st == TransStatus::Interrupted) {
        // 已结束后仍可能收到窗口内迟到的 ACK / Complete，属预期，按需幂等应答
        if (st == TransStatus::Finished && frame->type == FrameType::kFileType_Request_Ack) {
            return;
        }
        if (st == TransStatus::Finished && frame->type == FrameType::kFileType_Request_Complete) {
            if (completeAnswered_) {
                answerComplete(completeAnswerOk_, completeAnswerErr_);
            }
            return;
        }
        // Cancel 不再回 Cancel：否则两端都进入 Interrupted 后会无限互发
        if (frame->type == FrameType::kFileType_Request_Cancel) {
            return;
        }
        qWarning() << "文件传输已结束，无法处理消息, state is:" << static_cast<int>(st);
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
        return;
    }
    switch (frame->type) {
    case FrameType::kFileType_Request_Chuck: {
        handleRecvChuck(frame);
        break;
    }
    case FrameType::kFileType_Request_Ack: {
        handleAck(frame);
        break;
    }
    case FrameType::kFileType_Request_Complete: {
        qDebug() << QString::fromStdString(frame->from) << ", kFileType_Request_Complete.";
        handleFinish(frame);
        break;
    }
    case FrameType::kFileType_Answer_Complete: {
        qDebug() << QString::fromStdString(frame->from) << ", kFileType_Answer_Complete.";
        handleCompleteAnswer(frame);
        break;
    }
    case FrameType::kFileType_Request_Cancel: {
        qDebug() << QString::fromStdString(frame->from) << ", kFileType_Request_Cancel.";
        handleInterrupt(frame);
        break;
    }
    case FrameType::kFileType_Request_Start: {
        // 如果自己是接收方，那么消息原地返回。
        if (tMode_ == TransMode::Receive) {
            std::swap(frame->from, frame->to);
            frame->setFromReceiver(!frame->isFromReceiver());
            emit signalRequestSend(frame);
            break;
        }
        // 如果自己是发送方，那么应用对端在 Request_Start 里协商的参数后开始发送。
        if (tMode_ == TransMode::Send) {
            try {
                Message param;
                deserializeStruct(frame->data, param);
                applyTransferParams(param.blockSize, param.windowBytes);
                // 接收方可能已经有前一段数据（断点续传），从它报告的偏移开始发
                applyResumeOffset(param.resumeOffset);
            } catch (const std::exception& e) {
                qWarning() << "解析 Request_Start 参数失败:" << e.what();
            }
            nextSend();
        }
        break;
    }
    default: {
        break;
    }
    }
}

bool OneFileTrans::handleInterrupt(FramePtr frame, bool discardPart)
{
    QMutexLocker locker(&qMut_);
    TransStatus st = state_.load();
    if (st == TransStatus::Finished) {
        return true;
    }
    // 对端发来的 Cancel 不需要再回 Cancel，避免两端互发死循环
    const bool remoteCancel = (frame != nullptr && frame->type == FrameType::kFileType_Request_Cancel);
    if (!remoteCancel && (st == TransStatus::Receving || st == TransStatus::Sending)) {
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
    }
    state_.store(TransStatus::Interrupted, std::memory_order_release);
    if (recvFile_.isOpen()) {
        qWarning() << "关闭接收文件" << partPath_;
        recvFile_.close();
    }
    if (sendFile_.isOpen()) {
        qWarning() << "关闭发送文件" << filePath_;
        sendFile_.close();
    }
    // 显式取消：删掉残片，不留垃圾。
    // 超时/网络中断：保留 .relaypart，下次传同一个文件时从断点续传（靠指纹确认是同一份数据）。
    if (discardPart) {
        discardPartFile();
    } else if (partPath_ != QString() && QFileInfo::exists(partPath_)) {
        qWarning() << "保留未完成的临时文件用于续传:" << partPath_ << "大小:" << QFileInfo(partPath_).size();
    }
    return true;
}

void OneFileTrans::failAndInterrupt(const std::string& err)
{
    qWarning() << "传输失败:" << filePath_ << QString::fromStdString(err);
    handleInterrupt(nullptr, false);
    emit signalFailed(ownId_, err);
}

QString OneFileTrans::getTransName() const
{
    return filePath_;
}
