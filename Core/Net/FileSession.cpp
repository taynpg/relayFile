#include "FileSession.h"

#include "CoreDefine.hpp"
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

    if (state_ != TransStatus::Idle) {
        return false;
    }

    if (TransMode::Send == tMode_) {
        sendFile_.open(filePath_, std::ios::binary | std::ios::in);
        if (!sendFile_.is_open()) {
            return false;
        }
        state_ = TransStatus::Sending;
    } else {
        recvFile_.open(filePath_, std::ios::binary | std::ios::in);
        if (!recvFile_.is_open()) {
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
    if (curBlockIndex_ * blockSize_ >= totalSize_) {
        auto frame = CreateControlFrame(MessageType::kMessageFileRequestComplete);
        emit signalRequestSend(frame);
        state_ = TransStatus::Finished;
        emit signalFinished(ownId_);
        return true;
    }
    std::vector<char> buffer(blockSize_);
    sendFile_.read(buffer.data(), blockSize_);
    auto bytesRead = sendFile_.gcount();
    if (bytesRead <= 0) {
        auto frame = CreateControlFrame(MessageType::kMessageFileRequestComplete);
        emit signalRequestSend(frame);
        state_ = TransStatus::Finished;
        emit signalFinished(ownId_);
        return true;
    }
    buffer.resize(bytesRead);
    auto frame = CreateFileFrame(FrameType::FrameFileChuck);
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
    auto f = CreateFileFrame(FrameType::FrameFileAck);
    emit signalRequestSend(f);
    return true;
}

FramePtr OneFileTrans::CreateControlFrame(MessageType type)
{
    Message msg;
    msg.msType = type;
    msg.to.clientId = targetId_;
    msg.uuid = uuid_;
    auto frame = OneFrame::Create();
    frame->to = targetId_;
    frame->from = ownId_;
    frame->index = curBlockIndex_;
    frame->data = serializeStruct(msg);
    return frame;
}

FramePtr OneFileTrans::CreateFileFrame(FrameType type)
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
    case FrameType::FrameFileChuck: {
        handleChuck(frame);
        break;
    }
    case FrameType::FrameFileAck: {
        handleAck(frame);
        break;
    }
    default: {
        Message msg;
        deserializeStruct(frame->data, msg);
        switch (msg.msType) {
        case MessageType::kMessageFileRequestComplete: {
            handleFinish(frame);
            break;
        }
        case MessageType::kMessageFileRequestStart: {
            handleStart(frame);
            break;
        }
        case MessageType::kMessageFileRequestCancel: {
            handleInterrupt(frame);
            break;
        }
        default: {
            break;
        } break;
        }
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
        state_ = TransStatus::Receving;
    }
    return true;
}

FileSession::FileSession(QObject* parent) : ClientCore(parent)
{
    clientCore_ = new ClientCore();
    clientCore_->setIsControl(false);
    clientWorker_ = new ClientWorker(clientCore_, nullptr);
    clientCore_->moveToThread(clientWorker_);
    clientWorker_->start();
}

FileSession::~FileSession()
{
    delete clientCore_;
    delete clientWorker_;
}

void FileSession::Quit()
{
    clientWorker_->quit();
    clientWorker_->wait();
}

void FileSession::handleFrame(FramePtr frame)
{
    if (frame->type != FrameType::FrameMessage) {
        msgFrame(frame);
    } else {
        fileFrame(frame);
    }
}

void FileSession::msgFrame(FramePtr frame)
{
    Message msg;
    deserializeStruct(frame->data, msg);
    switch (msg.msType) {
    case MessageType::kMessageFileRequestSend: {
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Send, msg.ff, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);
        if (ret) {
            QMutexLocker locker(&transferMapLock_);
            transferMap_[msg.uuid] = fileTrans;
            fileTrans->nextSend();
        } else {
            Message resp;
            resp.uuid = msg.uuid;
            resp.msType = MessageType::kMessageFileAnswerRequestSend;
            resp.msgStateCode = MessageStateCode::kMessageStateCodeFailed;
            resp.errData = "文件任务初始化失败。";
            resp.to = msg.from;
            auto rf = OneFrame::Create();
            rf->to = msg.from.clientId;
            rf->data = serializeStruct(resp);
            emit signalRequestSend(rf);
        }
        break;
    }
    case MessageType::kMessageFileRequestDown: {

        break;
    }
    default: {
        break;
    }
    }
}

void FileSession::fileFrame(FramePtr frame)
{
}

bool FileSession::getFileMeta(const Message& msg, FileMeta& meta)
{
    bool isHave = false;
    for (const auto& item : msg.mapData) {
        auto& metaList = item.second;
        if (metaList.size() != 1) {
            meta = metaList[0];
            isHave = true;
            break;
        }
        break;
    }
    return isHave;
}

ClientCore* FileSession::getClientCore()
{
    return clientCore_;
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
