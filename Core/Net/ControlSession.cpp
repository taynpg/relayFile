#include "ControlSession.h"

ControlSession::ControlSession(QObject* parent) : ClientCore(parent)
{
}

ControlSession::~ControlSession()
{
}

void ControlSession::handleFrame(FramePtr frame)
{
}