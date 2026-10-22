#include "FileSessionIO.h"

#include <QDebug>
#include <QEventLoop>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

#include "CoreDefine.hpp"
#include "File/FileDir.h"
#include "Protocol/Serialize.hpp"

FileSessionIO::FileSessionIO(ClientCore* clientCore, QObject* parent) : QObject(parent), clientCore_(clientCore)
{
}

std::string FileSessionIO::getMapKeyUUIDByMode(const std::string& uuid, OneFileTrans::TransMode mode)
{
    return mode == OneFileTrans::TransMode::Send ? uuid + "S" : uuid + "R";
}

std::string FileSessionIO::getMapKeyUUIDByDirection(const std::string& uuid, bool fromReceiver)
{
    // 对端是"接收数据的一侧"时，本端就是发送侧 → 查 "S" 槽
    return fromReceiver ? uuid + "S" : uuid + "R";
}

void FileSessionIO::setServerEndpoint(const QString& ip, qint16 port)
{
    serverIp_ = ip;
    serverPort_ = port;
}

bool FileSessionIO::ensureConnected()
{
    const auto ready = [this]() {
        return clientCore_->isConnected() && !clientCore_->getOwnClientInfo().clientId.empty();
    };
    if (ready()) {
        return true;
    }

    if (waitingDepth_ == 0) {
        if (serverIp_.isEmpty()) {
            emit signalNeedFileConnect();
        } else {
            auto* cli = clientCore_;
            const QString ip = serverIp_;
            const qint16 port = serverPort_;
            QMetaObject::invokeMethod(
                cli, [cli, ip, port]() { cli->connectToServer(ip, port); }, Qt::QueuedConnection);
        }
    }
    ++waitingDepth_;

    // 必须用事件循环等待：就绪条件（clientId 登记）要靠本线程处理队列里的 Answer_ID
    // 帧来完成。以前用 QThread::msleep 自旋，会把 handleFrame 自己阻塞住——队列里的
    // Answer_ID 永远得不到处理，这里必然空等 5 秒后失败，把等待它的请求帧（如
    // Request_Send）丢掉，对端只能等 30s 超时。
    // 事件循环允许嵌套：等待期间到来的其它帧会重入 handleFrame 再次进入本函数。
    QEventLoop loop;
    QTimer pollTimer;
    pollTimer.setInterval(10);
    bool askedId = false;
    QObject::connect(&pollTimer, &QTimer::timeout, &loop, [this, &ready, &loop, &askedId]() {
        if (ready()) {
            loop.quit();
            return;
        }
        // TCP 已连但还没登记 ID：主动索要（原逻辑保留）
        if (!askedId && clientCore_->isConnected()) {
            askOwnID();
            askedId = true;
        }
    });
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    pollTimer.start();
    timeoutTimer.start(5000);
    loop.exec();

    --waitingDepth_;
    return ready();
}

void FileSessionIO::replyNotReady(const FramePtr& frame)
{
    // 只对"请求-应答"类帧回失败应答，让对端立即失败并提示重试，
    // 而不是在 RunTaskItem 里等满 30s。
    FrameType answer;
    bool fromReceiver;
    switch (frame->type) {
    case FrameType::kFileType_Request_Send:
        answer = FrameType::kFileType_Answer_Send;
        fromReceiver = true;   // Answer_Send 由接收方回
        break;
    case FrameType::kFileType_Request_Down:
        answer = FrameType::kFileType_Answer_Down;
        fromReceiver = false;  // Answer_Down 由发送方回
        break;
    default:
        return;
    }

    Message req;
    try {
        deserializeStruct(frame->data, req);
    } catch (const std::exception& e) {
        qWarning() << "未就绪帧反序列化失败，无法回错误应答:" << e.what();
        return;
    }

    Message resp(req);
    std::swap(resp.from, resp.to);
    resp.msgStateCode = MessageStateCode::kMessageStateCodeFailed;
    resp.errMsg = "文件通道未就绪，请稍后重试";

    auto rf = OneFrame::Create();
    rf->to = resp.to.clientId;
    rf->data = serializeStruct(resp);
    rf->type = answer;
    rf->setFromReceiver(fromReceiver);
    rf->fuuid = req.uuid;
    emit signalRequestSend(rf);
}

void FileSessionIO::emitStateChanged(const std::string& uuid, OneFileTrans::TransMode mode,
                                     OneFileTrans::TransStatus status)
{
    emit signalTaskStateChanged(QString::fromStdString(uuid), mode == OneFileTrans::TransMode::Send,
                                static_cast<int>(status));
}

void FileSessionIO::pushTask(const std::shared_ptr<OneFileTrans>& fileTrans, const Message& msg,
                             const std::string& errMsg, FrameType type, bool ret, bool needConnect)
{
    Message resp(msg);
    std::swap(resp.from, resp.to);

    // 握手参数：接收方说了算（块大小 / 窗口 / 续传起点），发送方把源文件指纹带回去
    if (fileTrans->getTransMode() == OneFileTrans::TransMode::Receive) {
        resp.blockSize = fileTrans->getBlockSize();
        resp.windowBytes = fileTrans->getWindowBytes();
        resp.resumeOffset = fileTrans->getResumeOffset();
    } else {
        resp.resumeFingerprint = fileTrans->getFingerprint();
    }

    // 只有发起任务的一侧需要把进度接到界面上；应答侧不显示别人的任务
    if (needConnect) {
        connect(fileTrans.get(), &OneFileTrans::signalProcess, this, &FileSessionIO::signalCurFileProgress);
        signalCurFile(QString::fromStdString(msg.ff.fullPath), QString::fromStdString(msg.ft.fullPath));
    }

    const auto mode = fileTrans->getTransMode();
    auto mapKeyUUID = getMapKeyUUIDByMode(msg.uuid, mode);
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
    rf->setFromReceiver(mode == OneFileTrans::TransMode::Receive);
    rf->fuuid = msg.uuid;

    if (ret) {
        connect(fileTrans.get(), &OneFileTrans::signalRequestSend, this,
                [this](FramePtr frame) { emit signalRequestSend(frame); });
        // 结束（成功/失败/中断）时通知等待方，RunTaskItem 靠它从条件变量上醒来
        const auto uuid = msg.uuid;
        connect(fileTrans.get(), &OneFileTrans::signalFinished, this,
                [this, uuid, mode](const std::string&) { emitStateChanged(uuid, mode, OneFileTrans::TransStatus::Finished); });
        connect(fileTrans.get(), &OneFileTrans::signalFailed, this,
                [this, uuid, mode](const std::string&, const std::string&) {
                    emitStateChanged(uuid, mode, OneFileTrans::TransStatus::Interrupted);
                });
        connect(fileTrans.get(), &OneFileTrans::signalInterrupt, this,
                [this, uuid, mode](const std::string&) {
                    emitStateChanged(uuid, mode, OneFileTrans::TransStatus::Interrupted);
                });
        // 任务已登记：通知等待方"会话建好了"，否则它只能靠轮询发现
        emitStateChanged(uuid, mode, fileTrans->getTransStatus());
    }
    emit signalRequestSend(rf);
}

void FileSessionIO::clearTask(const QString& uuid, bool isSend)
{
    const auto std_uuid = uuid.toStdString();
    const auto mode = isSend ? OneFileTrans::TransMode::Send : OneFileTrans::TransMode::Receive;
    bool isFind = false;
    {
        QMutexLocker locker(&transferMapLock_);
        auto mapKeyUUID = getMapKeyUUIDByMode(std_uuid, mode);
        auto it = transferMap_.find(mapKeyUUID);
        if (it != transferMap_.end()) {
            isFind = true;
            if (*it) {
                (*it)->stopTrans();
            }
            transferMap_.erase(it);
        }
    }
    if (isFind) {
        qWarning() << "清除任务：" << uuid;
        // 唤醒还在等的 RunTaskItem
        emitStateChanged(std_uuid, mode, OneFileTrans::TransStatus::Interrupted);
    }
}

void FileSessionIO::handleFrame(FramePtr frame)
{
    if (!frame) {
        return;
    }
    // 传输帧要求文件通道已就绪；Answer_ID 本身就是在建立身份，不检查
    if (frame->type != FrameType::kFileType_Answer_ID && !ensureConnected()) {
        qWarning() << "文件通道未就绪，丢弃帧, type=" << static_cast<int>(frame->type)
                   << "uuid=" << QString::fromStdString(frame->fuuid);
        // 静默丢弃会让对端在 RunTaskItem 里等满 30s；请求类帧回一个失败应答让它立即失败
        replyNotReady(frame);
        return;
    }

    Message msg;
    if (static_cast<int>(frame->type) < defDirectChuckAck) {
        try {
            deserializeStruct(frame->data, msg);
        } catch (const std::exception& e) {
            qWarning() << "解析文件帧失败（已丢弃）, type=" << static_cast<int>(frame->type) << "err:" << e.what();
            return;
        }
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
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Send, msg, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        fileTrans->setTargetControlId(frame->from);

        pushTask(fileTrans, msg, "文件任务初始化失败（Request_Down）。", FrameType::kFileType_Answer_Down, ret, false);
        qDebug() << QString::fromStdString(msg.from.clientId)
                 << "kFileType_Answer_Down:" << QString::fromStdString(msg.ff.fullPath) << "，结果：" << ret;
        break;
    }
    case FrameType::kFileType_Answer_Down: {
        // 判断成功性（msg 已在上方统一反序列化）
        if (msg.msgStateCode == MessageStateCode::kMessageStateCodeFailed) {
            auto kuuid = getMapKeyUUIDByMode(msg.uuid, OneFileTrans::TransMode::Receive);
            {
                QMutexLocker locker(&transferMapLock_);
                if (!transferMap_.contains(kuuid)) {
                    transferMap_[kuuid] = nullptr;
                }
            }
            // 失败了就放弃传输。
            qWarning() << QString::fromStdString(msg.errMsg);
            emitStateChanged(msg.uuid, OneFileTrans::TransMode::Receive, OneFileTrans::TransStatus::Interrupted);
            break;
        }
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Receive, msg, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        fileTrans->setTargetControlId(frame->from);
        pushTask(fileTrans, msg, "文件任务初始化失败（Answer_Down）。", FrameType::kFileType_Request_Start, ret, true);
        qDebug() << QString::fromStdString(msg.from.clientId)
                 << "kFileType_Answer_Down:" << QString::fromStdString(msg.ft.fullPath) << "，结果：" << ret;
        break;
    }
    case FrameType::kFileType_Request_Send: {
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Receive, msg, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        fileTrans->setTargetControlId(frame->from);
        pushTask(fileTrans, msg, "文件任务初始化失败（Request_Send）。", FrameType::kFileType_Answer_Send, ret, false);
        qDebug() << QString::fromStdString(msg.from.clientId)
                 << "kFileType_Request_Send:" << QString::fromStdString(msg.ft.fullPath) << "，结果：" << ret;
        break;
    }
    case FrameType::kFileType_Answer_Send: {
        // 判断成功性（msg 已在上方统一反序列化）
        if (msg.msgStateCode == MessageStateCode::kMessageStateCodeFailed) {
            auto kuuid = getMapKeyUUIDByMode(msg.uuid, OneFileTrans::TransMode::Send);
            {
                QMutexLocker locker(&transferMapLock_);
                if (!transferMap_.contains(kuuid)) {
                    transferMap_[kuuid] = nullptr;
                }
            }
            // 失败了就放弃传输。
            qWarning() << QString::fromStdString(msg.errMsg);
            emitStateChanged(msg.uuid, OneFileTrans::TransMode::Send, OneFileTrans::TransStatus::Interrupted);
            break;
        }
        auto fileTrans = std::make_shared<OneFileTrans>();
        auto ret = fileTrans->initTransfer(OneFileTrans::TransMode::Send, msg, msg.transId,
                                           clientCore_->getOwnClientInfo().clientId, msg.uuid);

        fileTrans->setTargetControlId(frame->from);
        pushTask(fileTrans, msg, "文件任务初始化失败（Answer_Send）。", FrameType::kFileType_Request_Start, ret, true);
        break;
    }
    default: {
        std::shared_ptr<OneFileTrans> fileTrans = nullptr;
        {
            auto mapKeyUUID = getMapKeyUUIDByDirection(frame->fuuid, frame->isFromReceiver());
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

bool FileSessionIO::getTransStatus(OneFileTrans::TransStatus& status, const std::string& baseUUID, bool isSend)
{
    auto mapKeyUUID =
        getMapKeyUUIDByMode(baseUUID, isSend ? OneFileTrans::TransMode::Send : OneFileTrans::TransMode::Receive);
    bool have = false;
    QMutexLocker locker(&transferMapLock_);
    auto it = transferMap_.find(mapKeyUUID);
    if (it != transferMap_.end()) {
        have = true;
        if (*it == nullptr) {
            status = OneFileTrans::TransStatus::Interrupted;
        } else {
            status = (*it)->getTransStatus();
        }
    }
    return have;
}

void FileSessionIO::askOwnID()
{
    Message msg;
    msg.comStr = "F";
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->sessionId = clientCore_->GetSessionId();
    frame->type = FrameType::kFileType_Request_ID;
    emit signalRequestSend(frame);
}

void FileSessionIO::shutdown()
{
    if (quit_) {
        return;
    }
    quit_ = true;
    QMutexLocker locker(&transferMapLock_);
    for (auto& tf : transferMap_) {
        if (tf) {
            tf->stopTrans();
        }
    }
    transferMap_.clear();
}
