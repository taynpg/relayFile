#pragma once

#include <Protocol/FileMeta.h>
#include <memory>
#include <string>
#include <vector>

enum class AskType {
    ASK_TYPE_LOCAL,
    ASK_TYPE_REMOTE,
};

class BaseAskDF
{
public:
    BaseAskDF() = default;
    ~BaseAskDF() = default;

public:
    virtual bool AskFileList(const std::string& path, std::vector<FileMeta>& fileList, bool recursive = false) = 0;
    virtual bool AskHome(std::string& home) = 0;
    virtual bool AskFileMeta(const std::string& path, FileMeta& meta) = 0;
    virtual bool AskDelete(const std::vector<std::string>& fileList, std::vector<std::string>& failedList) = 0;
    virtual bool AskSha256(const std::string& path, std::string& sha256) = 0;
    virtual bool AskRename(const std::string& oldName, const std::string& newName) = 0;
    virtual bool AskCreateDir(const std::string& path) = 0;
    virtual bool AskArchive(const std::vector<FileMeta>& fileList, const std::string& archivePath) = 0;
    virtual bool AskUnArchive(const std::string& archivePath, const std::string& extractPath) = 0;

public:
    static std::shared_ptr<BaseAskDF> Create(AskType askType);
};