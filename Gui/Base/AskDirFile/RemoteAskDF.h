#pragma once

#include <Net/ClientCore.h>
#include <Net/ControlSession.h>

#include "BaseAskDF.h"

class RemoteAskDF : public BaseAskDF
{
public:
    RemoteAskDF();
    ~RemoteAskDF() = default;

public:
    template <typename HandleResp> bool Request(Message& msg, HandleResp handleResp, FrameType type);

public:
    bool AskFileList(const std::string& path, std::vector<FileMeta>& fileList) override;
    bool AskHome(std::string& home) override;

private:
    std::shared_ptr<ControlSession> controlSession_{};
};
