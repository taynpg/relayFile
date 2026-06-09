#pragma once

#include <Net/ClientCore.h>

#include "BaseAskDF.h"

class RemoteAskDF : public BaseAskDF
{
public:
    RemoteAskDF();
    ~RemoteAskDF() = default;

public:
    bool GetFileList(const std::string& path, std::vector<FileMeta>& fileList) override;
    bool AskHome(std::string& home) override;

private:
    ClientCore* clientControl_{};
};
