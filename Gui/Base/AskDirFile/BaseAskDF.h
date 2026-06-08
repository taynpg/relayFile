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
    virtual bool GetFileList(const std::string& path, std::vector<FileMeta>& fileList) = 0;
    virtual bool AskHome(std::string& home) = 0;

public:
    static std::shared_ptr<BaseAskDF> Create(AskType askType);
};