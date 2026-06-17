#pragma once

#include "BaseAskDF.h"

class LocalAskDF : public BaseAskDF
{
public:
    LocalAskDF() = default;
    ~LocalAskDF() = default;

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
};
