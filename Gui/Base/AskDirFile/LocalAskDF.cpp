#include "LocalAskDF.h"

#include <File/FileDir.h>

bool LocalAskDF::AskFileList(const std::string& path, std::vector<FileMeta>& fileList)
{
    QVector<RFileMeta> result;
    if (!FileDir::GetFileList(QString::fromStdString(path), result)) {
        return false;
    }
    for (const auto& rmeta : result) {
        FileMeta meta;
        meta.dir = rmeta.dir.toStdString();
        meta.name = rmeta.fileName.toStdString();
        meta.size = rmeta.fileSize;
        meta.lastModified = rmeta.lastModified;
        meta.permission = rmeta.permission;
        meta.type = rmeta.fileType == RFileType::mTypeDir ? FileType::FILE_TYPE_DIR : FileType::FILE_TYPE_FILE;
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