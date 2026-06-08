#pragma once

#include "ClientCore.h"

class ControlSession : public ClientCore
{
    Q_OBJECT
public:
    ControlSession(QObject* parent = nullptr);
    ~ControlSession();

private:
    void handleFrame(FramePtr frame) override;
};