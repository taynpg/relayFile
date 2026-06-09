#include "ControlSession.h"

#include "File/FileDir.h"
#include "Protocol/Serialize.hpp"

#define PushOneWork()                                                                                                            \
    auto worker = ClientCore::TaskWorker::CreateWorker(frame);                                                                   \
    {                                                                                                                            \
        QMutexLocker locker(&responseWaitLock_);                                                                                 \
        if (!responseWaitWorker_.contains(worker->frame->sessionId)) {                                                           \
            responseWaitWorker_.insert(worker->frame->sessionId, worker);                                                        \
        } else {                                                                                                                 \
            return;                                                                                                              \
        }                                                                                                                        \
    }

ControlSession::ControlSession(QObject* parent) : ClientCore(parent)
{
}

ControlSession::~ControlSession()
{
}

void ControlSession::handleFrame(FramePtr frame)
{
    MessagePtr answerMsg = Message::Create();
    deserializeStruct(frame->data, *answerMsg);

    switch (answerMsg->msType) {
    case MessageType::kMessageAnswerId: {
        mInfo_.clientId = answerMsg->to.clientId;
        mInfo_.clientName = answerMsg->to.clientName;
        setCurUUID(QString::fromStdString(answerMsg->msData));
        emit signalOwnInfo(mInfo_);
        break;
    }
    case MessageType::kMessageAskHome: {
        PushOneWork();
        workerPool_->enqueue([this, worker]() {
            QString home;
            FileDir::GetHome(home);
            auto answerFrame = OneFrame::Create(worker->frame);
            Message m;
            m.msType = MessageType::kMessageAnswerHome;
            m.msData = home.toStdString();
            answerFrame->data = serializeStruct(m);
            emit signalSendFrame(answerFrame);
            worker->isDone = true;
        });
        break;
    }
    case MessageType::kMessageAskFileList: {
        PushOneWork();
        workerPool_->enqueue([this, worker]() {
            Message sourceMsg;
            deserializeStruct(worker->frame->data, sourceMsg);
            QVector<RFileMeta> result;
            FileDir::GetFileList(QString::fromStdString(sourceMsg.msData), result);
            auto answerFrame = OneFrame::Create(worker->frame);
            Message m(sourceMsg);
            m.msType = MessageType::kMessageAnswerFileList;
            std::vector<FileMeta> stdMeta;
            stdMeta.reserve(result.size());
            for (const auto& item : result) {
                FileMeta meta;
                FileDir::TurnMeta(item, meta);
                stdMeta.push_back(meta);
            }
            m.mapData[""] = stdMeta;
            answerFrame->data = serializeStruct(m);
            emit signalSendFrame(answerFrame);
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

void ControlSession::AskOwnID()
{
    Message msg;
    msg.msType = MessageType::kMessageAskId;
    msg.msData = "倔强的小强";
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->sessionId = GetSessionId();
    Send(frame);
}