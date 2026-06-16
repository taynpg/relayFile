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
};
