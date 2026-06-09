#include "ControlSession.h"

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
        break;
    }
    case MessageType::kMessageAskFileList: {
        break;
    }
    default: {
        QMutexLocker locker(&waitLock_);
        if (auto it = waitFrame_.find(frame->sessionId); it != waitFrame_.end()) {
            auto callType = it.value()->callType;
            switch (callType) {
            case CallType::CT_Message: {
                it.value()->call(answerMsg);
                break;
            }
            case CallType::CT_Frame: {
                it.value()->call(frame);
                break;
            }
            }
        }
        break;
    }
    }
}
