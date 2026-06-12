#include "OneFileTrans.h"

#include "Protocol/Serialize.hpp"

OneFileTrans::OneFileTrans(QObject* parent) : QObject(parent)
{
    sendTimeoutTimer_ = new QTimer(this);
    sendTimeoutTimer_->setSingleShot(true);
    connect(sendTimeoutTimer_, &QTimer::timeout, this, &OneFileTrans::onSendTimeout);
}

void OneFileTrans::initSignals()
{
}

void OneFileTrans::onSendTimeout()
{
    QMutexLocker locker(&qMut_);
    if (state_ == TransStatus::Sending) {
        emit signalFailed(ownId_, "超时");
        state_ = TransStatus::Interrupted;
    }
}

OneFileTrans::TransMode OneFileTrans::getTransMode()
{
    return tMode_;
}

bool OneFileTrans::initTransfer(TransMode mode, const FileMeta& fileMeta, const std::string& targetId, const std::string& ownId,
                                const std::string& uuid)
{
    QMutexLocker locker(&qMut_);

    tMode_ = mode;
    targetId_ = targetId;
    totalSize_ = fileMeta.size;
    meta_ = fileMeta;
    transSize_ = 0;
    uuid_ = uuid;
    curBlockIndex_ = 0;
    ownId_ = ownId;
    filePath_ = miniPath::Join(meta_.dir, meta_.name);

    qDebug() << "处理文件路径：" << QString::fromStdString(filePath_);

    if (state_ != TransStatus::Idle) {
        return false;
    }

    if (TransMode::Send == tMode_) {
        sendFile_.open(filePath_, std::ios::binary | std::ios::in);
        if (!sendFile_.is_open()) {
            qWarning() << "打开文件失败: " << QString::fromStdString(filePath_) << ", " << __LINE__;
            return false;
        }
        state_ = TransStatus::Sending;
    } else {
        recvFile_.open(filePath_, std::ios::binary | std::ios::out);
        if (!recvFile_.is_open()) {
            qWarning() << "打开文件失败: " << QString::fromStdString(filePath_) << ", " << __LINE__;
            return false;
        }
        state_ = TransStatus::Receving;
    }

    return true;
}

bool OneFileTrans::nextSend()
{
    if (TransStatus::Sending != state_) {
        return false;
    }
    std::vector<char> buffer(blockSize_);
    sendFile_.read(buffer.data(), blockSize_);
    auto bytesRead = sendFile_.gcount();
    if (bytesRead <= 0) {
        if (sendFile_.is_open()) {
            sendFile_.close();
        }
        auto frame = CreateFrame(FrameType::kFileType_Request_Complete);
        emit signalRequestSend(frame);
        state_ = TransStatus::Finished;
        emit signalFinished(ownId_);
        return true;
    }
    buffer.resize(bytesRead);
    auto frame = CreateFrame(FrameType::kFileType_Request_Chuck);
    frame->data = std::move(buffer);
    emit signalRequestSend(frame);

    // timer
    sendTimeoutTimer_->start(defSendTimeout);
    return true;
}

bool OneFileTrans::handleAck(FramePtr frame)
{
    if (frame->index == curBlockIndex_) {
        // timer
        sendTimeoutTimer_->stop();
        transSize_ += blockSize_;
        if (transSize_ > totalSize_) {
            transSize_ = totalSize_;
        }
        curBlockIndex_++;
        emit signalProcess(transSize_, totalSize_);
        nextSend();
        return true;
    }
    return false;
}

bool OneFileTrans::handleRecvChuck(FramePtr frame)
{
    if (state_ != TransStatus::Receving) {
        return false;
    }
    if (frame->index != curBlockIndex_) {
        return false;
    }
    recvFile_.write(frame->data.data(), frame->data.size());
    transSize_ += frame->data.size();

    emit signalProcess(transSize_, totalSize_);
    auto f = CreateFrame(FrameType::kFileType_Request_Ack);
    emit signalRequestSend(f);

    curBlockIndex_++;
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
    if (tMode_ == TransMode::Receive) {
        if (recvFile_.is_open()) {
            recvFile_.close();
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
    QMutexLocker locker(&qMut_);
    if (state_ == TransStatus::Finished || state_ == TransStatus::Interrupted) {
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
        handleFinish(frame);
        break;
    }
    case FrameType::kFileType_Request_Cancel: {
        handleInterrupt(frame);
        break;
    }
    case FrameType::kFileType_Request_Start: {
        // 如果自己是接收方，那么消息原地返回。
        if (tMode_ == TransMode::Receive) {
            std::swap(frame->from, frame->to);
            frame->mark = frame->mark == 0 ? 1 : 0;
            emit signalRequestSend(frame);
            break;
        }
        // 如果自己是发送方，那么继续发送。
        if (tMode_ == TransMode::Send) {
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
    state_ = TransStatus::Interrupted;
    if (recvFile_.is_open()) {
        recvFile_.close();
    }
    if (sendFile_.is_open()) {
        sendFile_.close();
    }
    return true;
}