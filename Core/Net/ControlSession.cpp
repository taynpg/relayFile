#include "ControlSession.h"

#include <QDebug>

#include "CoreDefine.hpp"
#include "File/FileDir.h"
#include "Protocol/Message.h"
#include "Protocol/Serialize.hpp"

#define PushOneWork()                                                                                                            \
    auto worker = ControlSession::TaskWorker::CreateWorker(frame);                                                               \
    {                                                                                                                            \
        QMutexLocker locker(&responseWaitLock_);                                                                                 \
        if (!responseWaitWorker_.contains(worker->frame->sessionId)) {                                                           \
            responseWaitWorker_.insert(worker->frame->sessionId, worker);                                                        \
        } else {                                                                                                                 \
            return;                                                                                                              \
        }                                                                                                                        \
    }

ControlSession::ControlSession(QObject* parent) : QObject(parent), workerPool_(std::make_shared<ThreadPool>(8))
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
                    waiter.timer->stop();
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

    waiter->timer = std::make_shared<TimerStd>(timeoutHandler);
    waiter->timer->start_once(std::chrono::milliseconds(defWaitCmdTimeout));
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
        emit signalOwnInfo(info);
        break;
    }
    case FrameType::kMsgType_Ask_Home: {
        PushOneWork();
        workerPool_->enqueue([this, worker]() {
            QString home;
            FileDir::GetHome(home);
            auto answerFrame = OneFrame::Create(worker->frame);
            Message m;
            m.comStr = home.toStdString();
            answerFrame->data = serializeStruct(m);
            answerFrame->type = FrameType::kMsgType_Answer_Home;
            emit signalRequestSend(answerFrame);
            worker->isDone = true;
        });
        break;
    }
    case FrameType::kMsgType_Ask_FileList: {
        PushOneWork();
        workerPool_->enqueue([this, worker]() {
            Message sourceMsg;
            deserializeStruct(worker->frame->data, sourceMsg);
            QVector<RFileMeta> result;
            FileDir::GetFileList(QString::fromStdString(sourceMsg.comStr), result);
            auto answerFrame = OneFrame::Create(worker->frame);
            Message m(sourceMsg);
            std::vector<FileMeta> stdMeta;
            stdMeta.reserve(result.size());
            for (const auto& item : result) {
                FileMeta meta;
                FileDir::TurnMeta(item, meta);
                stdMeta.push_back(meta);
            }
            m.mapData[""] = stdMeta;
            answerFrame->data = serializeStruct(m);
            answerFrame->type = FrameType::kMsgType_Answer_FileList;
            emit signalRequestSend(answerFrame);
            worker->isDone = true;
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

void ControlSession::AskOwnID()
{
    Message msg;
    msg.from.clientName = "倔强的小强";
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