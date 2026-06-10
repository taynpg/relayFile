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
    frame->fuuid = uuid_;
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
        handleRecvChuck(frame);
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
        case MessageType::kMessageFileAnswerRequestSend: {
            if (msg.msgStateCode == MessageStateCode::kMessageStateCodeSuccess) {
                nextSend();
            } else {
                handleInterrupt(frame);
            }
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
    if (frame->fuuid.empty()) {
        Message msg;
        deserializeStruct(frame->data, msg);
        switch (msg.msType) {
        case MessageType::kMessageFileRequestDown: {
            auto fileTrans = std::make_shared<OneFileTrans>();
            auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Send, msg.ff, msg.transId,
                                               clientCore_->getOwnClientInfo().clientId, msg.uuid);
            Message resp;
            resp.uuid = msg.uuid;
            resp.to = msg.from;
            resp.msType = MessageType::kMessageFileAnswerRequestDown;
            if (ret) {
                {
                    QMutexLocker locker(&transferMapLock_);
                    transferMap_[msg.uuid] = fileTrans;
                }
                fileTrans->nextSend();
            } else {
                resp.msgStateCode = MessageStateCode::kMessageStateCodeFailed;
                resp.errData = "文件任务初始化失败（SEND）。";
                auto rf = OneFrame::Create();
                rf->to = msg.from.clientId;
                rf->data = serializeStruct(resp);
                emit signalRequestSend(rf);
            }
            break;
        }
        case MessageType::kMessageFileRequestSend: {
            auto fileTrans = std::make_shared<OneFileTrans>();
            auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Receive, msg.ff, msg.transId,
                                               clientCore_->getOwnClientInfo().clientId, msg.uuid);
            Message resp;
            resp.uuid = msg.uuid;
            resp.msType = MessageType::kMessageFileAnswerRequestSend;
            resp.to = msg.from;
            if (ret) {
                {
                    QMutexLocker locker(&transferMapLock_);
                    transferMap_[msg.uuid] = fileTrans;
                }
                // 告知对方自己的传输ID是多少。
                resp.transId = clientCore_->getOwnClientInfo().clientId;
                resp.msgStateCode = MessageStateCode::kMessageStateCodeSuccess;
            } else {
                resp.msgStateCode = MessageStateCode::kMessageStateCodeFailed;
                resp.errData = "文件任务初始化失败（Receive）。";
            }
            auto rf = OneFrame::Create();
            rf->to = msg.from.clientId;
            rf->data = serializeStruct(resp);
            emit signalRequestSend(rf);
            break;
        }
        default: {
            break;
        }
        }
    } else {
        std::shared_ptr<OneFileTrans> fileTrans = nullptr;
        {
            QMutexLocker locker(&transferMapLock_);
            if (transferMap_.contains(frame->fuuid)) {
                fileTrans = transferMap_[frame->fuuid];
            }
        }
        if (fileTrans) {
            fileTrans->onFrameReceive(frame);
        }
    }
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
