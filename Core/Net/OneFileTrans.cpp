#include "OneFileTrans.h"

#include <File/FileDir.h>

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
    if (state_ == TransStatus::Sending) {
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
    } else if (state_ == TransStatus::Receving) {
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

    qDebug() << "处理文件路径：" << filePath_;

    if (state_ != TransStatus::Idle) {
        return false;
    }

    if (tMode_ == TransMode::Send) {
        sendFile_.setFileName(filePath_);
        if (!FileDir::EnsureDir(FileDir::cdUp(filePath_)) || !sendFile_.open(QIODevice::ReadOnly)) {
            qWarning() << "打开发送文件失败:" << filePath_ << sendFile_.errorString();
            return false;
        }
        state_ = TransStatus::Sending;
    } else {
        recvFile_.setFileName(filePath_);
        if (!FileDir::EnsureDir(FileDir::cdUp(filePath_)) || !recvFile_.open(QIODevice::WriteOnly)) {
            qWarning() << "打开接收文件失败:" << filePath_ << recvFile_.errorString();
            return false;
        }
        state_ = TransStatus::Receving;
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
    if (TransStatus::Sending != state_) {
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
        state_ = TransStatus::Finished;
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
    if (TransStatus::Sending != state_) {
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
    if (state_ != TransStatus::Receving) {
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
    qint64 written = recvFile_.write(frame->data.data(), static_cast<qint64>(frame->data.size()));
    if (written != static_cast<qint64>(frame->data.size())) {
        qWarning() << "写入文件失败";
    }
    transSize_ += frame->data.size();
    auto ackIdx = curBlockIndex_;
    curBlockIndex_++;

    if (curBlockIndex_ % 10 == 0 || transSize_ >= totalSize_) {
        emit signalProcess(transSize_, totalSize_);
    }
    auto f = CreateFrame(FrameType::kFileType_Request_Ack);
    f->index = static_cast<int64_t>(ackIdx);
    emit signalRequestSend(f);
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
        // 查看是否需要同步权限
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
        state_ = TransStatus::Finished;
    }
    return true;
}

OneFileTrans::TransStatus OneFileTrans::getTransStatus() const
{
    return state_;
}

void OneFileTrans::onFrameReceive(FramePtr frame)
{
    // qDebug() << "收到消息:" << static_cast<int>(frame->type) << "，from:" << frame->from << "，to:" << frame->to
    //          << "，index:" << frame->index;
    if (state_ == TransStatus::Finished || state_ == TransStatus::Interrupted) {
        // 正常完成后，窗口内迟到的 ACK/Complete 属预期，静默忽略；
        // 其余（如对方仍在发数据块）回 Cancel 终止对方
        if (state_ == TransStatus::Finished && (frame->type == FrameType::kFileType_Request_Ack
                || frame->type == FrameType::kFileType_Request_Complete)) {
            return;
        }
        qWarning() << "文件传输已结束，无法处理消息, state is:" << static_cast<int>(state_);
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
            qDebug() << QString::fromStdString(frame->from) << ", kFileType_Request_Start.";
            emit signalRequestSend(frame);
            break;
        }
        // 如果自己是发送方，那么继续发送。
        if (tMode_ == TransMode::Send) {
            qDebug() << QString::fromStdString(frame->from) << ", nextSend.";
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
    if (state_ == TransStatus::Finished) {
        return true;
    }
    if (tMode_ == TransMode::Receive && state_ == TransStatus::Receving) {
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
    }
    if (tMode_ == TransMode::Send && state_ == TransStatus::Sending) {
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
    }
    state_ = TransStatus::Interrupted;
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
