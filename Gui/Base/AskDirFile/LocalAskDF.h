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
    bool AskFileExist(const std::string& path, bool& existExist, std::uint64_t& fileSize) override;
};
