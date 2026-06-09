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
    Message answerMsg;
    deserializeStruct(frame->data, answerMsg);

    switch (answerMsg.msType) {
    case MessageType::kMessageAskHome: {
        break;
    }
    case MessageType::kMessageAskFileList: {
        break;
    }
    default: {
        {
            QMutexLocker locker(&waitLock_);
            if (auto it = waitFrame_.find(frame->sessionId); it != waitFrame_.end()) {
                it.value()->call(frame);
            }
        }
        break;
    }
    }
}
