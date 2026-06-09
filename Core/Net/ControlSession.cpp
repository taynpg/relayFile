#include "ControlSession.h"

#include "File/FileDir.h"
#include "Protocol/Serialize.hpp"

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
        emit signalOwnInfo(mInfo_);
        break;
    }
    case MessageType::kMessageAskHome: {
        auto woker = ClientCore::TaskWorker::CreateWorker(frame);
        {
            QMutexLocker locker(&responseWaitLock_);
            responseWaitWorker_.insert(woker->frame->sessionId, woker);
        }
        workerPool_->enqueue([this, woker]() {
            QString home;
            FileDir::GetHome(home);
            auto answerFrame = OneFrame::Create(woker->frame);
            Message m;
            m.msType = MessageType::kMessageAnswerHome;
            m.msData = home.toStdString();
            answerFrame->data = serializeStruct(m);
            emit signalSendFrame(answerFrame);
            woker->isDone = true;
        });
        break;
    }
    case MessageType::kMessageAskFileList: {
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
