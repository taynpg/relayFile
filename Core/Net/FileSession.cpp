#include "FileSession.h"

#include "Protocol/Serialize.hpp"

OneFileTrans::OneFileTrans(QObject* parent) : QObject(parent)
{
}

void OneFileTrans::initSignals()
{
}

bool OneFileTrans::initTransfer(TransMode mode, const FileMeta& fileMeta, const std::string& targetId, ClientCore* fileControl)
{
    QMutexLocker locker(&qMut_);

    tMode_ = mode;
    fileControl_ = fileControl;
    targetId_ = targetId;
    totalSize_ = fileMeta.size;
    meta_ = fileMeta;
    transSize_ = 0;
    curBlockIndex_ = 0;
    filePath_ = miniPath::Join(meta_.dir, meta_.name);

    if (state_ != TransStatus::Idle) {
        return false;
    }

    if (TransMode::Send == tMode_) {
        sendFile_.open(filePath_, std::ios::binary | std::ios::in);
        if (!sendFile_.is_open()) {
            return false;
        }
        state_ = TransStatus::WaitingAccept;
    } else {
        recvFile_.open(filePath_, std::ios::binary | std::ios::in);
        if (!recvFile_.is_open()) {
            return false;
        }
        state_ = TransStatus::WaitingAccept;
    }

    return true;
}

bool OneFileTrans::nextSend()
{
    if (TransStatus::Sending != state_) {
        return false;
    }
    if (curBlockIndex_ * blockSize_ >= totalSize_) {
        auto frame = CreateFrame(FrameType::FrameFileFinish);
        fileControl_->Send(frame);
        state_ = TransStatus::Finished;
        emit signalFinished(ownId_);
        return true;
    }
    std::vector<char> buffer(blockSize_);
    sendFile_.read(buffer.data(), blockSize_);
    auto bytesRead = sendFile_.gcount();
    if (bytesRead <= 0) {
        auto frame = CreateFrame(FrameType::FrameFileFinish);
        fileControl_->Send(frame);
        state_ = TransStatus::Finished;
        emit signalFinished(ownId_);
        return true;
    }
    buffer.resize(bytesRead);
    auto frame = CreateFrame(FrameType::FrameFileChuck);
    frame->data = std::move(buffer);

    if (!fileControl_->Send(frame)) {
        return false;
    }
    // timer
    return true;
}

bool OneFileTrans::handleAck(FramePtr frame)
{
    if (frame->index == curBlockIndex_) {
        // timer
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

bool OneFileTrans::handleChuck(FramePtr frame)
{
    if (state_ != TransStatus::Receving) {
        return false;
    }
    if (frame->index != curBlockIndex_) {
        return false;
    }
    recvFile_.write(frame->data.data(), frame->data.size());
    transSize_ += frame->data.size();
    curBlockIndex_++;

    emit signalProcess(transSize_, totalSize_);
    auto f = CreateFrame(FrameType::FrameFileAck);
    return fileControl_->Send(f);
}

FramePtr OneFileTrans::CreateFrame(FrameType type)
{
    auto frame = OneFrame::Create();
    frame->type = type;
    frame->to = targetId_;
    frame->from = ownId_;
    frame->index = curBlockIndex_;
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

void OneFileTrans::onFrameReceive(FramePtr frame)
{
    QMutexLocker locker(&qMut_);
    if (state_ == TransStatus::Finished || state_ == TransStatus::Interrupted) {
        return;
    }
    switch (frame->type) {
    case FrameType::FrameFileFinish: {
        handleFinish(frame);
        break;
    }
    case FrameType::FrameFileAccept: {
        handleStart(frame);
        break;
    }
    case FrameType::FrameFileAck: {
        handleAck(frame);
        break;
    }
    case FrameType::FrameFileChuck: {
        handleChuck(frame);
        break;
    }
    case FrameType::FrameFileInterrupt: {
        handleInterrupt(frame);
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

bool OneFileTrans::handleStart(FramePtr frame)
{
    QMutexLocker locker(&qMut_);
    if (TransMode::Send == tMode_) {
        if (!sendFile_.is_open()) {
            return false;
        }
        state_ = TransStatus::Sending;
    } else {
        if (!recvFile_.is_open()) {
            return false;
        }
        state_ = TransStatus::Sending;
    }
    return true;
}

FileSession::FileSession(QObject* parent) : ClientCore(parent)
{
}

FileSession::~FileSession()
{
}

void FileSession::handleFrame(FramePtr frame)
{
}

void FileSession::AskOwnID()
{
    Message msg;
    msg.msType = MessageType::kMessageAskId;
    msg.msData = "倔强的小强";
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->sessionId = GetSessionId();
    Send(frame);
}
