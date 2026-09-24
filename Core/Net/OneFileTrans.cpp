#include "OneFileTrans.h"

#include <File/FileDir.h>
#include <QDir>

#include "Compress/TarXzPacker.h"
#include "Protocol/Serialize.hpp"

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
    if (state_.load() == TransStatus::Sending) {
        if (sentIndex_ > ackIndex_ && retransCount_ < defRetransLimit) {
            // 滑动窗口下偶发 ACK 迟到：重传最早未确认块，恢复发送指针
            ++retransCount_;
            auto frame = CreateFrame(FrameType::kFileType_Request_Chuck);
            frame->index = static_cast<int64_t>(ackIndex_);
            qint64 savedPos = static_cast<qint64>(sentIndex_ * blockSize_);
            sendFile_.seek(static_cast<qint64>(ackIndex_ * blockSize_));
            QByteArray buffer(static_cast<int>(blockSize_), Qt::Uninitialized);
            qint64 bytesRead = sendFile_.read(buffer.data(), static_cast<qint64>(blockSize_));
            sendFile_.seek(savedPos);
            buffer.resize(static_cast<int>(bytesRead));
            frame->data.assign(buffer.cbegin(), buffer.cend());
            emit signalRequestSend(frame);
            sendOrRecvTimeout_->start(defSendTimeout);
            return;
        }
        emit signalFailed(ownId_, "超时");
        qWarning() << "发送超时:" << QString::fromStdString(ownId_);
        handleInterrupt(nullptr);
    } else if (state_.load() == TransStatus::Receving) {
        emit signalFailed(ownId_, "超时");
        qWarning() << "接收超时:" << QString::fromStdString(ownId_);
        handleInterrupt(nullptr);
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
    // mark==2 标记本传输为自描述 tar.xz 归档包（仅对接收侧生效：发送侧仅读取
    // 归档作为普通文件发出，isArchive_ 在 Send 分支无副作用）。
    isArchive_ = (msg_.mark == 2);
    totalSize_ = (tMode_ == TransMode::Send ? msg.ff.size : msg.ft.size);
    meta_ = (tMode_ == TransMode::Send ? msg.ff : msg.ft);
    transSize_ = 0;
    uuid_ = uuid;
    curBlockIndex_ = 0;
    ackIndex_ = 0;
    sentIndex_ = 0;
    eofReached_ = false;
    retransCount_ = 0;
    ownId_ = ownId;
    // filePath_ = QString::fromStdString(miniPath::Join(meta_.dir, meta_.name));
    filePath_ = QString::fromStdString(meta_.fullPath);
    // 归档接收：忽略发送方给定的 ft.fullPath（发送方无法预知本机临时目录），
    // 改写到本机临时目录，收完后再解包到清单指定的真实目的路径。
    if (isArchive_ && tMode_ == TransMode::Receive) {
        filePath_ = QDir::tempPath() + QString("/relay_archive_%1.tar.xz").arg(QString::fromStdString(uuid));
    }

    // qDebug() << "处理文件路径：" << filePath_;

    if (state_.load() != TransStatus::Idle) {
        return false;
    }

    if (tMode_ == TransMode::Send) {
        sendFile_.setFileName(filePath_);
        if (!FileDir::EnsureDir(FileDir::cdUp(filePath_)) || !sendFile_.open(QIODevice::ReadOnly)) {
            qWarning() << "打开发送文件失败:" << filePath_ << sendFile_.errorString();
            return false;
        }
        state_.store(TransStatus::Sending, std::memory_order_release);
    } else {
        recvFile_.setFileName(filePath_);
        if (!FileDir::EnsureDir(FileDir::cdUp(filePath_)) || !recvFile_.open(QIODevice::WriteOnly)) {
            qWarning() << "打开接收文件失败:" << filePath_ << recvFile_.errorString();
            return false;
        }
        state_.store(TransStatus::Receving, std::memory_order_release);
    }

    return true;
}

void OneFileTrans::stopTrans()
{
    handleInterrupt(nullptr);
}

void OneFileTrans::setTargetControlId(const std::string& targetControlId)
{
    targetControlId_ = targetControlId;
}

bool OneFileTrans::nextSend()
{
    if (TransStatus::Sending != state_.load()) {
        return false;
    }
    // 滑动窗口：窗口未满则连续发送，无需等待每块 ACK
    while (!eofReached_ && sentIndex_ - ackIndex_ < defWindowSize) {
        QByteArray buffer(static_cast<int>(blockSize_), Qt::Uninitialized);
        qint64 bytesRead = sendFile_.read(buffer.data(), static_cast<qint64>(blockSize_));
        if (bytesRead <= 0) {
            eofReached_ = true;
            break;
        }
        buffer.resize(static_cast<int>(bytesRead));
        auto frame = CreateFrame(FrameType::kFileType_Request_Chuck);
        frame->index = static_cast<int64_t>(sentIndex_);
        frame->data.assign(buffer.cbegin(), buffer.cend());
        emit signalRequestSend(frame);
        sentIndex_++;
        transSize_ += static_cast<std::uint64_t>(bytesRead);
        if (transSize_ > totalSize_) {
            transSize_ = totalSize_;
        }
        // 每 10 块上报一次进度，避免高吞吐下 UI 事件过载
        if (sentIndex_ % 10 == 0) {
            emit signalProcess(transSize_, totalSize_);
        }
    }
    if (eofReached_ && sentIndex_ == ackIndex_) {
        // 完成帧走控制连接，与数据连接无序；必须等全部块 ACK（对端确认已写盘）后再发
        sendOrRecvTimeout_->stop();
        sendFile_.close();
        auto frame = CreateFrame(FrameType::kFileType_Request_Complete);
        frame->to = targetControlId_;
        emit signalRequestSend(frame);
        state_.store(TransStatus::Finished, std::memory_order_release);
        emit signalProcess(totalSize_, totalSize_);
        emit signalFinished(ownId_);
        return true;
    }
    if (sentIndex_ > ackIndex_) {
        sendOrRecvTimeout_->start(defSendTimeout);
    }
    return true;
}

bool OneFileTrans::handleAck(FramePtr frame)
{
    if (TransStatus::Sending != state_.load()) {
        return false;
    }
    auto idx = static_cast<std::uint64_t>(frame->index);
    // ACK.index=接收方刚写盘的块号，推进窗口左沿（忽略过期/越界 ACK）
    if (idx < ackIndex_ || idx >= sentIndex_) {
        return false;
    }
    ackIndex_ = idx + 1;
    retransCount_ = 0;
    nextSend();
    return true;
}

bool OneFileTrans::handleRecvChuck(FramePtr frame)
{
    if (state_.load() != TransStatus::Receving) {
        return false;
    }
    auto idx = static_cast<std::uint64_t>(frame->index);
    if (idx < curBlockIndex_) {
        // 重复块（发送方超时重传）：幂等重发 ACK
        auto f = CreateFrame(FrameType::kFileType_Request_Ack);
        f->index = static_cast<int64_t>(curBlockIndex_ - 1);
        emit signalRequestSend(f);
        return true;
    }
    if (idx != curBlockIndex_) {
        return false;
    }
    sendOrRecvTimeout_->stop();
    auto ackIdx = curBlockIndex_;
    curBlockIndex_++;
    transSize_ += frame->data.size();

    // 关键优化：先立即回 ACK，再写盘。
    // 否则写盘慢（尤其网络盘）会阻塞主线程，ACK 延迟触发发送方超时重传死循环。
    auto f = CreateFrame(FrameType::kFileType_Request_Ack);
    f->index = static_cast<int64_t>(ackIdx);
    emit signalRequestSend(f);

    if (curBlockIndex_ % 10 == 0 || transSize_ >= totalSize_) {
        emit signalProcess(transSize_, totalSize_);
    }

    qint64 written = recvFile_.write(frame->data.data(), static_cast<qint64>(frame->data.size()));
    if (written != static_cast<qint64>(frame->data.size())) {
        qWarning() << "写入文件失败, expected:" << frame->data.size() << "written:" << written
                   << "file:" << filePath_;
    }
    sendOrRecvTimeout_->start(defRecvTimeout);
    return true;
}

FramePtr OneFileTrans::CreateFrame(FrameType type)
{
    auto frame = OneFrame::Create();
    frame->type = type;
    frame->to = targetId_;
    frame->from = ownId_;
    frame->fuuid = uuid_;
    frame->index = curBlockIndex_;
    frame->mark = tMode_ == TransMode::Send ? 0 : 1;

    if (static_cast<uint16_t>(type) < defDirectChuckAck) {
        Message msg;
        msg.to.clientId = targetId_;
        msg.uuid = uuid_;
        frame->data = serializeStruct(msg);
    }
    return frame;
}

bool OneFileTrans::handleFinish(FramePtr frame)
{
    sendOrRecvTimeout_->stop();
    if (tMode_ == TransMode::Receive) {
        if (recvFile_.isOpen()) {
            recvFile_.close();
        }
        if (isArchive_) {
            // 压缩传输：归档收完后就地解包，按内嵌清单写入各目的路径并应用
            // 各自权限，随后删除临时归档。归档本身不需要单文件权限同步。
            std::vector<std::string> outPaths;
            std::string err;
            if (!TarXzPacker::extract(filePath_.toStdString(), outPaths, err)) {
                qWarning() << "归档解包失败:" << filePath_ << "err:" << QString::fromStdString(err);
                QFile::remove(filePath_);
                emit signalFailed(ownId_, "归档解包失败: " + err);
                state_.store(TransStatus::Interrupted, std::memory_order_release);
                return true;
            }
            qInfo() << "归档解包完成，文件数:" << outPaths.size() << "归档:" << filePath_;
            QFile::remove(filePath_);
            emit signalFinished(ownId_);
            state_.store(TransStatus::Finished, std::memory_order_release);
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
        emit signalFinished(ownId_);
        state_.store(TransStatus::Finished, std::memory_order_release);
    }
    return true;
}

void OneFileTrans::onFrameReceive(FramePtr frame)
{
    // qDebug() << "收到消息:" << static_cast<int>(frame->type) << "，from:" << frame->from << "，to:" << frame->to
    //          << "，index:" << frame->index;
    TransStatus st = state_.load();
    if (st == TransStatus::Finished || st == TransStatus::Interrupted) {
        // 正常完成后，窗口内迟到的 ACK/Complete 属预期，静默忽略；
        // 其余（如对方仍在发数据块）回 Cancel 终止对方
        if (st == TransStatus::Finished && (frame->type == FrameType::kFileType_Request_Ack
                || frame->type == FrameType::kFileType_Request_Complete)) {
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
    case FrameType::kFileType_Request_Cancel: {
        qDebug() << QString::fromStdString(frame->from) << ", kFileType_Request_Cancel.";
        handleInterrupt(frame);
        break;
    }
    case FrameType::kFileType_Request_Start: {
        // 如果自己是接收方，那么消息原地返回。
        if (tMode_ == TransMode::Receive) {
            std::swap(frame->from, frame->to);
            frame->mark = frame->mark == 0 ? 1 : 0;
            // qDebug() << QString::fromStdString(frame->from) << ", kFileType_Request_Start.";
            emit signalRequestSend(frame);
            break;
        }
        // 如果自己是发送方，那么继续发送。
        if (tMode_ == TransMode::Send) {
            // qDebug() << QString::fromStdString(frame->from) << ", nextSend.";
            nextSend();
        }
        break;
    }
    default: {
        break;
    }
    }
}

bool OneFileTrans::handleInterrupt(FramePtr frame)
{
    QMutexLocker locker(&qMut_);
    TransStatus st = state_.load();
    if (st == TransStatus::Finished) {
        return true;
    }
    if (tMode_ == TransMode::Receive && st == TransStatus::Receving) {
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
    }
    if (tMode_ == TransMode::Send && st == TransStatus::Sending) {
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
    }
    state_.store(TransStatus::Interrupted, std::memory_order_release);
    if (recvFile_.isOpen()) {
        qWarning() << "关闭接收文件" << filePath_;
        recvFile_.close();
    }
    if (sendFile_.isOpen()) {
        qWarning() << "关闭发送文件" << filePath_;
        sendFile_.close();
    }
    return true;
}

QString OneFileTrans::getTransName() const
{
    return filePath_;
}
