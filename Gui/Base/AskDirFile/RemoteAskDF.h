#pragma once

#include "BaseAskDF.h"

class RemoteAskDF : public BaseAskDF
{
public:
    RemoteAskDF() = default;
    ~RemoteAskDF() = default;

public:
    bool GetFileList(const std::string& path, std::vector<FileMeta>& fileList) override;
    bool AskHome(std::string& home) override;
};
