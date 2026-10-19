#pragma once

#include <Protocol/FileMeta.h>
#include <Protocol/Message.h>
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
    virtual bool AskHomeAndDriver(std::vector<std::string>& drivers, std::string& home) = 0;
    virtual bool AskFileMeta(const std::string& path, FileMeta& meta) = 0;
    virtual bool AskFileSamples(const std::string& path, std::vector<SampleBlock>& samples) = 0;
    // 批量版：一次往返查一批路径。返回 false 表示整批请求失败；
    // 单条路径查不到时该条 exist=0，不影响其余条目（调用方自行跳过）。
    virtual bool AskFileMetaBatch(const std::vector<std::string>& paths, std::vector<FileMeta>& metas) = 0;
    virtual bool AskFileSamplesBatch(const std::vector<std::string>& paths,
                                     std::vector<std::vector<SampleBlock>>& samples) = 0;
    virtual bool AskDelete(const std::vector<std::string>& fileList, std::vector<std::string>& failedList) = 0;
    virtual bool AskSha256(const std::string& path, std::string& sha256) = 0;
    virtual bool AskRename(const std::string& oldName, const std::string& newName) = 0;
    virtual bool AskCreateDir(const std::string& path) = 0;
    virtual bool AskArchive(const std::vector<FileMeta>& fileList, const std::string& archivePath) = 0;
    virtual bool AskUnArchive(const std::string& archivePath, const std::string& extractPath) = 0;

public:
    static std::shared_ptr<BaseAskDF> Create(AskType askType);
};
