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
    bool AskFileList(const std::string& path, std::vector<FileMeta>& fileList, bool recursive) override;
    bool AskHome(std::string& home) override;
    bool AskFileMeta(const std::string& path, FileMeta& meta) override;

private:
    std::shared_ptr<ControlSession> controlSession_{};
};
