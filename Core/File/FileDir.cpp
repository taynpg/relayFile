#include "FileDir.h"

#include <QDir>

thread_local QString errInfo;

QString FileDir::GetErrInfo()
{
    return errInfo;
}

bool FileDir::GetFileList(const QString& path, QVector<RFileMeta>& fileList)
{
    fileList.clear();
    QString dirPath = path;

    QDir qdir(dirPath);
    if (!qdir.exists()) {
        errInfo = "路径不存在";
        return false;
    }

    if (!qdir.isReadable()) {
        errInfo = "目录不可读";
        return false;
    }

    const auto entries = qdir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    for (const auto& entry : entries) {
        RFileMeta info;
        info.dir = entry.absoluteFilePath();
        info.fileName = entry.fileName();
        info.permission = entry.permissions();

        if (entry.isDir()) {
            info.fileType = RFileType::mTypeDir;
            info.fileSize = 0;
        } else if (entry.isFile()) {
            info.fileType = RFileType::mTypeFile;
            info.fileSize = entry.size();
        } else {
            continue;
        }

        info.lastModified = entry.lastModified().toMSecsSinceEpoch();
        fileList.append(info);
    }

    return true;
}