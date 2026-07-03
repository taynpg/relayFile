#include "FileSession.h"

#include "CoreDefine.hpp"
#include "Protocol/Serialize.hpp"

FileSession::FileSession(QObject* parent)
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

std::string FileSession::getMapKeyUUIDByMode(const std::string& uuid, OneFileTrans::TransMode mode)
{
    if (mode == OneFileTrans::TransMode::Send) {
        return uuid + "S";
    } else {
        return uuid + "R";
    }
}

std::string FileSession::getMapKeyUUIDByMark(const std::string& uuid, int16_t mark)
{
    if (0 != mark) {
        return uuid + "S";
    } else {
        return uuid + "R";
    }
}

void FileSession::pushTask(const std::shared_ptr<OneFileTrans>& fileTrans, const Message& msg, const std::string& errMsg,
                           FrameType type, bool ret, bool needConnect)
{
    Message resp(msg);
    resp.uuid = msg.uuid;
    std::swap(resp.from, resp.to);

    if (needConnect && msg.mark != 0) {
        connect(fileTrans.get(), &OneFileTrans::signalProcess, this, &FileSession::signalCurFileProgress);
        signalCurFile(QString::fromStdString(msg.ff.fullPath), QString::fromStdString(msg.ft.fullPath));
        resp.mark = 0;
    }

    auto mapKeyUUID = getMapKeyUUIDByMode(msg.uuid, fileTrans->getTransMode());
    if (ret) {
        {
            QMutexLocker locker(&transferMapLock_);
            transferMap_[mapKeyUUID] = fileTrans;
        }
        resp.transId = clientCore_->getOwnClientInfo().clientId;
        resp.msgStateCode = MessageStateCode::kMessageStateCodeSuccess;
    } else {
        resp.msgStateCode = MessageStateCode::kMessageStateCodeFailed;
        resp.errMsg = errMsg;
    }
    auto rf = OneFrame::Create();
    rf->to = resp.to.clientId;
    rf->data = serializeStruct(resp);
    rf->type = type;
    rf->mark = fileTrans->getTransMode() == OneFileTrans::TransMode::Send ? 0 : 1;
    rf->fuuid = msg.uuid;

    if (ret) {
        connect(fileTrans.get(), &OneFileTrans::signalRequestSend, this, [this](FramePtr frame) { signalRequestSend(frame); });
    }
    emit signalRequestSend(rf);
}

void FileSession::clearTask(const std::string& uuid, bool isSend)
{
    bool isFind = false;
    auto mapKeyUUID = getMapKeyUUIDByMode(uuid, isSend ? OneFileTrans::TransMode::Send : OneFileTrans::TransMode::Receive);
    {
        QMutexLocker locker(&transferMapLock_);
        if (transferMap_.find(mapKeyUUID) != transferMap_.end()) {
            isFind = true;
            auto& tf = transferMap_[mapKeyUUID];
            if (tf) {
                tf->stopTrans();
            }
            transferMap_.remove(mapKeyUUID);
        }
    }
    if (isFind) {
        qWarning() << "清除任务：" << QString::fromStdString(uuid);
    }
}

void FileSession::handleFrame(FramePtr frame)
{
    Message msg;
    if (static_cast<int>(frame->type) < defDirectChuckAck) {
        deserializeStruct(frame->data, msg);
    }
    switch (frame->type) {
    case FrameType::kFileType_Answer_ID: {
        ClientInfo info;
        info.clientId = msg.to.clientId;
        clientCore_->onRecordOwnInfo(info);
        qDebug() << "传输ID：" << QString::fromStdString(info.clientId);
        break;
    }
    case FrameType::kFileType_Request_Down: {
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Send, msg.ff, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        fileTrans->setTargetControlId(frame->from);
        pushTask(fileTrans, msg, "文件任务初始化失败（Request_Down）。", FrameType::kFileType_Answer_Down, ret, false);
        qDebug() << QString::fromStdString(msg.from.clientId)
                 << "kFileType_Answer_Down:" << QString::fromStdString(msg.ff.fullPath) << "，结果：" << ret;
        break;
    }
    case FrameType::kFileType_Answer_Down: {
        // 判断成功性
        Message ansMsg;
        deserializeStruct(frame->data, ansMsg);
        if (ansMsg.msgStateCode == MessageStateCode::kMessageStateCodeFailed) {
            auto kuuid = getMapKeyUUIDByMode(msg.uuid, OneFileTrans::TransMode::Receive);
            {
                QMutexLocker locker(&transferMapLock_);
                if (!transferMap_.contains(kuuid)) {
                    transferMap_[kuuid] = nullptr;
                }
            }
            // 失败了就放弃传输。
            qWarning() << QString::fromStdString(ansMsg.errMsg);
            break;
        }
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Receive, msg.ft, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        fileTrans->setTargetControlId(frame->from);
        pushTask(fileTrans, msg, "文件任务初始化失败（Answer_Down）。", FrameType::kFileType_Request_Start, ret, true);
        qDebug() << QString::fromStdString(msg.from.clientId)
                 << "kFileType_Answer_Down:" << QString::fromStdString(msg.ft.fullPath) << "，结果：" << ret;
        break;
    }
    case FrameType::kFileType_Request_Send: {
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Receive, msg.ft, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        fileTrans->setTargetControlId(frame->from);
        pushTask(fileTrans, msg, "文件任务初始化失败（Request_Send）。", FrameType::kFileType_Answer_Send, ret, false);
        qDebug() << QString::fromStdString(msg.from.clientId)
                 << "kFileType_Request_Send:" << QString::fromStdString(msg.ft.fullPath) << "，结果：" << ret;
        break;
    }
    case FrameType::kFileType_Answer_Send: {
        // 判断成功性
        Message ansMsg;
        deserializeStruct(frame->data, ansMsg);
        if (ansMsg.msgStateCode == MessageStateCode::kMessageStateCodeFailed) {
            auto kuuid = getMapKeyUUIDByMode(msg.uuid, OneFileTrans::TransMode::Send);
            {
                QMutexLocker locker(&transferMapLock_);
                if (!transferMap_.contains(kuuid)) {
                    transferMap_[kuuid] = nullptr;
                }
            }
            // 失败了就放弃传输。
            qWarning() << QString::fromStdString(ansMsg.errMsg);
            break;
        }
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Send, msg.ff, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        fileTrans->setTargetControlId(frame->from);
        pushTask(fileTrans, msg, "文件任务初始化失败（Answer_Send）。", FrameType::kFileType_Request_Start, ret, true);
        qDebug() << QString::fromStdString(msg.from.clientId)
                 << "kFileType_Answer_Send:" << QString::fromStdString(msg.ff.fullPath) << "，结果：" << ret;
        break;
    }
    // case FrameType::kFileType_Answer_Start: {
    //     break;
    // }
    default: {
        std::shared_ptr<OneFileTrans> fileTrans = nullptr;
        {
            auto mapKeyUUID = getMapKeyUUIDByMark(frame->fuuid, frame->mark);
            QMutexLocker locker(&transferMapLock_);
            if (transferMap_.contains(mapKeyUUID)) {
                fileTrans = transferMap_[mapKeyUUID];
            }
        }
        if (fileTrans) {
            fileTrans->onFrameReceive(frame);
        }
        break;
    }
    }
}

bool FileSession::getTransStatus(OneFileTrans::TransStatus& status, const std::string& baseUUID, bool isSend)
{
    auto mapKeyUUID = getMapKeyUUIDByMode(baseUUID, isSend ? OneFileTrans::TransMode::Send : OneFileTrans::TransMode::Receive);
    bool have = false;
    {
        QMutexLocker locker(&transferMapLock_);
        if (transferMap_.contains(mapKeyUUID)) {
            have = true;
            auto& ot = transferMap_[mapKeyUUID];
            if (ot == nullptr) {
                status = OneFileTrans::TransStatus::Interrupted;
            } else {
                status = ot->getTransStatus();
            }
        }
    }
    return have;
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
    frame->sessionId = getClientCore()->GetSessionId();
    frame->type = FrameType::kFileType_Request_ID;
    emit signalRequestSend(frame);
}
