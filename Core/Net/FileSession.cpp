#include "FileSession.h"

#include "Protocol/Serialize.hpp"

FileSession::FileSession(QObject* parent) : ClientCore(parent)
{
}

FileSession::~FileSession()
{
}

void FileSession::handleFrame(FramePtr frame)
{
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
