#pragma once

#include "ClientCore.h"

class FileSession : public ClientCore
{
    Q_OBJECT
public:
    FileSession(QObject* parent = nullptr);
    ~FileSession() override;

private:
    void handleFrame(FramePtr frame) override;
};  
