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

public:
    static std::shared_ptr<BaseAskDF> Create(AskType askType);
};