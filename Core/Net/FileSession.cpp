#include "FileSession.h"

#include "CoreDefine.hpp"
#include "Protocol/Serialize.hpp"

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

void FileSession::pushTask(const std::shared_ptr<OneFileTrans>& fileTrans, const Message& msg, const std::string& errMsg,
                           FrameType type, bool ret)
{
    Message resp(msg);
    resp.uuid = msg.uuid;
    resp.to = msg.from;
    if (ret) {
        {
            QMutexLocker locker(&transferMapLock_);
            transferMap_[msg.uuid] = fileTrans;
        }
        resp.transId = clientCore_->getOwnClientInfo().clientId;
        resp.msgStateCode = MessageStateCode::kMessageStateCodeSuccess;
    } else {
        resp.msgStateCode = MessageStateCode::kMessageStateCodeFailed;
        resp.errMsg = errMsg;
    }
    auto rf = OneFrame::Create();
    rf->to = msg.from.clientId;
    rf->data = serializeStruct(resp);
    rf->type = type;
    rf->fuuid = msg.uuid;

    connect(fileTrans.get(), &OneFileTrans::signalRequestSend, this, [this](FramePtr frame) { signalRequestSend(frame); });
    emit signalRequestSend(rf);
}

void FileSession::handleFrame(FramePtr frame)
{
    Message msg;
    {
        if (static_cast<int>(frame->type) < defDirectTranStartNum) {
            deserializeStruct(frame->data, msg);
        }
    }
    switch (frame->type) {
    case FrameType::kFileType_Answer_ID: {
        ClientInfo info;
        info.clientId = msg.to.clientId;
        clientCore_->onRecordOwnInfo(info);
        qDebug() << "传输ID：" << info.clientId;
        break;
    }
    case FrameType::kFileType_Request_Down: {
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Send, msg.ff, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        pushTask(fileTrans, msg, "文件任务初始化失败（Request_Down）。", FrameType::kFileType_Answer_Down, ret);
        break;
    }
    case FrameType::kFileType_Answer_Down: {
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Receive, msg.ft, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        pushTask(fileTrans, msg, "文件任务初始化失败（Answer_Down）。", FrameType::kFileType_Answer_Start, ret);
        break;
    }
    case FrameType::kFileType_Request_Send: {
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Receive, msg.ft, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        pushTask(fileTrans, msg, "文件任务初始化失败（Request_Send）。", FrameType::kFileType_Answer_Send, ret);
        break;
    }
    case FrameType::kFileType_Answer_Send: {
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Send, msg.ff, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        pushTask(fileTrans, msg, "文件任务初始化失败（Answer_Send）。", FrameType::kFileType_Request_Start, ret);
        break;
    }
    // case FrameType::kFileType_Answer_Start: {
    //     break;
    // }
    default: {
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
        break;
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
    msg.comStr = "F";
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->sessionId = GetSessionId();
    frame->type = FrameType::kFileType_Request_ID;
    emit signalRequestSend(frame);
}
