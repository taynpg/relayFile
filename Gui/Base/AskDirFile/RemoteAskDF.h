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
    bool AskDelete(const std::vector<std::string>& fileList, std::vector<std::string>& failedList) override;
    bool AskSha256(const std::string& path, std::string& sha256) override;
    bool AskRename(const std::string& oldName, const std::string& newName) override;
    bool AskCreateDir(const std::string& path) override;
    bool AskArchive(const std::vector<FileMeta>& fileList, const std::string& archivePath) override;
    bool AskUnArchive(const std::string& archivePath, const std::string& extractPath) override;
    bool AskHomeAndDriver(std::vector<std::string>& drivers, std::string& home) override;

private:
    std::shared_ptr<ControlSession> controlSession_{};
};
