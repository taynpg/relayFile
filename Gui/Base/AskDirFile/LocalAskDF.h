#pragma once

#include "BaseAskDF.h"

class LocalAskDF : public BaseAskDF
{
public:
    LocalAskDF() = default;
    ~LocalAskDF() = default;

public:
    bool GetFileList(const std::string& path, std::vector<FileMeta>& fileList) override;
    bool AskHome(std::string& home) override;
};
