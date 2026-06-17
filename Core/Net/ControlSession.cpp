#include "ControlSession.h"

#include <QDebug>

#include "CoreDefine.hpp"
#include "File/LocalHandle.h"
#include "Protocol/Message.h"
#include "Protocol/Serialize.hpp"

ControlSession::ControlSession(QObject* parent)
    : QObject(parent), workerPool_(std::make_shared<ThreadPool>(3)), timerPoolStd_(std::make_shared<TimerPoolStd>(3))
{
    clearWorkerTimer_ = new QTimer(this);
    clientCore_ = new ClientCore();
    clientCore_->setIsControl(true);
    clientWorker_ = new ClientWorker(clientCore_, nullptr);
    clientCore_->moveToThread(clientWorker_);
    initSignals();
    clientWorker_->start();
}

ClientCore* ControlSession::getClientCore()
{
    return clientCore_;
}

ControlSession::~ControlSession()
{
    timerPoolStd_->stop();
    delete clientCore_;
    delete clientWorker_;
}

void ControlSession::Quit()
{
    qDebug() << "等待线程池完成...";
    workerPool_->Quit();
    qDebug() << "线程池完成";

    clearWorkerTimer_->stop();
    clientWorker_->quit();
    clientWorker_->wait();
}

void ControlSession::initSignals()
{
    connect(clearWorkerTimer_, &QTimer::timeout, this, &ControlSession::clearWorker);
    clearWorkerTimer_->start(defClearWorkerTimeout);
}

template <typename Callback> bool ControlSession::SendCall(FramePtr frame, Callback callback)
{
    auto sid = clientCore_->GetSessionId();
    frame->sessionId = sid;

    emit signalRequestSend(frame);
    qDebug() << "ControlSession::SendCall:" << static_cast<int>(frame->type) << ", sid=" << sid;

    // 这里不能用QTimer，因为QTimer依赖事件循环，而阻塞需求常常会阻塞事件循环，导致定时器失效
    auto timeoutHandler = [this, sid]() {
        QMetaObject::invokeMethod(
            this,
            [this, sid]() {
                qDebug() << "ControlSession::SendCall timeout sid:" << sid;
                QMutexLocker locker(&requestWaitLock_);
                auto it = requestWaitFrame_.find(sid);
                if (it != requestWaitFrame_.end()) {
                    auto& waiter = *it.value();
                    switch (waiter.callType) {
                    case CallType::CT_Message: {
                        MessagePtr msg = nullptr;
                        waiter.call(msg);
                        break;
                    }
                    case CallType::CT_Frame: {
                        FramePtr f = nullptr;
                        waiter.call(f);
                        break;
                    }
                    default:
                        break;
                    }
                    timerPoolStd_->stop(waiter.timerId);
                    requestWaitFrame_.erase(it);
                }
            },
            Qt::QueuedConnection);
    };

    auto waiter = std::make_shared<WaiteFrame>();
    waiter->sessionId = sid;
    using ArgType = typename function_traits<Callback>::argument_type;
    waiter->call = [cb = std::move(callback)](std::any arg) { cb(std::any_cast<ArgType>(arg)); };

    if constexpr (std::is_same_v<ArgType, MessagePtr>) {
        waiter->callType = CallType::CT_Message;
    } else if constexpr (std::is_same_v<ArgType, FramePtr>) {
        waiter->callType = CallType::CT_Frame;
    } else {
        static_assert(!sizeof(ArgType), "Unsupported callback argument type");
    }

    {
        QMutexLocker locker(&requestWaitLock_);
        requestWaitFrame_[sid] = waiter;
    }

    waiter->timerId = timerPoolStd_->start_once(std::chrono::milliseconds(defWaitCmdTimeout), timeoutHandler);
    return true;
}

ClientInfo ControlSession::getOtherInfo()
{
    return clientCore_->oInfo_;
}

ClientInfo ControlSession::getOwnInfo()
{
    return clientCore_->mInfo_;
}

void ControlSession::dispatchMessage(FramePtr frame, FrameType answerType,
                                     std::function<void(const Message& sourceMsg, Message& ansMsg)> handler)
{
    auto worker = ControlSession::TaskWorker::CreateWorker(frame);
    {
        QMutexLocker locker(&responseWaitLock_);
        if (!responseWaitWorker_.contains(worker->frame->sessionId)) {
            responseWaitWorker_.insert(worker->frame->sessionId, worker);
        } else {
            qWarning() << "PushOneWork:" << QString::fromStdString(worker->frame->from) << "，会话ID已存在。"
                       << worker->frame->sessionId;
            return;
        }
    }
    workerPool_->enqueue([this, worker, answerType, handler]() {
        Message sourceMsg;
        deserializeStruct(worker->frame->data, sourceMsg);
        auto answerFrame = OneFrame::Create(worker->frame);
        Message m(sourceMsg);

        handler(sourceMsg, m);

        answerFrame->data = serializeStruct(m);
        answerFrame->type = answerType;
        emit signalRequestSend(answerFrame);
        worker->isDone = true;
    });
}

void ControlSession::handleFrame(FramePtr frame)
{
    MessagePtr answerMsg = Message::Create();
    deserializeStruct(frame->data, *answerMsg);

    switch (frame->type) {
    case FrameType::kMsgType_Answer_ID: {
        ClientInfo info;
        info.clientId = answerMsg->to.clientId;
        info.clientName = answerMsg->to.clientName;
        info.uuid = answerMsg->comStr;
        clientCore_->onRecordOwnInfo(info);
        qDebug() << "本机信息：" << QString::fromStdString(info.clientId) << ", " << QString::fromStdString(info.clientName);
        emit signalOwnInfo(info);
        break;
    }
    case FrameType::kMsgType_Ask_Home: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Home,
                        [](const Message& sourceMsg, Message& ansMsg) { LocalHandle::AskHome(ansMsg.comStr); });
        break;
    }
    case FrameType::kMsgType_Ask_FileList: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_FileList, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.mapData[""] = std::vector<FileMeta>();
            auto& stdMeta = ansMsg.mapData[""];
            LocalHandle::AskFileList(sourceMsg.comStr, stdMeta, sourceMsg.mark == 1);
        });
        break;
    }
    case FrameType::kMsgType_Ask_FileMeta: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_FileMeta,
                        [](const Message& sourceMsg, Message& ansMsg) { LocalHandle::AskFileMeta(sourceMsg.comStr, ansMsg.ff); });
        break;
    }
    case FrameType::kMsgType_Ask_Delete: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Delete, [](const Message& sourceMsg, Message& ansMsg) {
            LocalHandle::AskDelete(sourceMsg.strVec, ansMsg.strVec);
        });
        break;
    }
    case FrameType::kMsgType_Ask_Rename: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Rename, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.mark = LocalHandle::AskRename(sourceMsg.ff.fullPath, sourceMsg.ft.fullPath);
        });
        break;
    }
    case FrameType::kMsgType_Ask_CreateDir: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_CreateDir, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.mark = LocalHandle::AskCreateDir(sourceMsg.comStr);
        });
        break;
    }
    case FrameType::kMsgType_Ask_Sha256: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Sha256, [](const Message& sourceMsg, Message& ansMsg) {
            LocalHandle::AskSha256(sourceMsg.comStr, ansMsg.comStr);
        });
        break;
    }
    case FrameType::kMsgType_Ask_Archive: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Archive, [](const Message& sourceMsg, Message& ansMsg) {
            const auto& fileList = sourceMsg.mapData.at("");
            ansMsg.mark = LocalHandle::AskArchive(fileList, sourceMsg.comStr);
        });
        break;
    }
    case FrameType::kMsgType_Ask_UnArchive: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_UnArchive, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.mark = LocalHandle::AskUnArchive(sourceMsg.ff.fullPath, sourceMsg.ft.fullPath);
        });
        break;
    }
    default: {
        QMutexLocker locker(&requestWaitLock_);
        if (auto it = requestWaitFrame_.find(frame->sessionId); it != requestWaitFrame_.end()) {
            auto& callType = it.value()->callType;
            switch (callType) {
            case CallType::CT_Message: {
                it.value()->call(answerMsg);
                callType = CallType::CT_Unknown;
                break;
            }
            case CallType::CT_Frame: {
                it.value()->call(frame);
                callType = CallType::CT_Unknown;
                break;
            }
            default:
                break;
            }
            timerPoolStd_->stop(it.value()->timerId);
            requestWaitFrame_.erase(it);
        }
        break;
    }
    }
}

bool ControlSession::SendWithCall(const Message& msg, FrameType type, std::function<void(FramePtr)> callback)
{
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->type = type;
    frame->to = clientCore_->oInfo_.clientId;
    return SendCall(frame, std::move(callback));
}

bool ControlSession::SendWithCall(FramePtr frame, std::function<void(MessagePtr)> callback)
{
    return SendCall(frame, std::move(callback));
}

bool ControlSession::SendWithCall(const Message& msg, FrameType type, std::function<void(MessagePtr)> callback)
{
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->type = type;
    frame->to = clientCore_->oInfo_.clientId;
    return SendCall(frame, std::move(callback));
}

bool ControlSession::SendWithCall(FramePtr frame, std::function<void(FramePtr)> callback)
{
    return SendCall(frame, std::move(callback));
}

void ControlSession::AskOwnID(const QString& name)
{
    Message msg;
    msg.from.clientName = name.toStdString();
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->sessionId = clientCore_->GetSessionId();
    frame->type = FrameType::kMsgType_Ask_ID;
    emit signalRequestSend(frame);
}

std::shared_ptr<ControlSession::TaskWorker> ControlSession::TaskWorker::CreateWorker(FramePtr frame)
{
    auto r = std::make_shared<ControlSession::TaskWorker>();
    r->frame = frame;
    r->isDone = false;
    r->sessionId = frame->sessionId;
    return r;
}

void ControlSession::clearWorker()
{
    QMutexLocker locker(&responseWaitLock_);
    for (auto iter = responseWaitWorker_.begin(); iter != responseWaitWorker_.end();) {
        if (iter.value()->isDone) {
            iter = responseWaitWorker_.erase(iter);
        } else {
            ++iter;
        }
    }
}