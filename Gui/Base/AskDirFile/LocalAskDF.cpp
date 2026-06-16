#include "LocalAskDF.h"

#include <File/FileDir.h>
#include <Utils/miniUtil.h>

bool LocalAskDF::AskFileList(const std::string& path, std::vector<FileMeta>& fileList, bool recursive)
{
    QVector<RFileMeta> result;
    if (!FileDir::GetFileList(QString::fromStdString(path), result, recursive)) {
        return false;
    }
    for (const auto& rmeta : result) {
        FileMeta meta;
        FileDir::TurnMeta(rmeta, meta);
        fileList.push_back(meta);
    }
    return true;
}
bool LocalAskDF::AskHome(std::string& home)
{
    QString homeStr;
    if (!FileDir::GetHome(homeStr)) {
        return false;
    }
    home = homeStr.toStdString();
    return true;
}

bool LocalAskDF::AskFileExist(const std::string& path, bool& existExist, std::uint64_t& fileSize)
{
    existExist = FileDir::IsExist(QString::fromStdString(path), fileSize);
    return true;
}
