#pragma once

#include <QString>
#include <QVector>

enum class RFileType {
    mTypeDir,
    mTypeFile,
};

struct RFileMeta {
    QString dir;
    RFileType fileType;
    QString fileName;
    quint64 fileSize;
    quint64 lastModified;
    quint16 permission;
};

class FileDir
{
public:
    FileDir() = default;
    ~FileDir() = default;

public:
    static bool GetFileList(const QString& path, QVector<RFileMeta>& fileList);
    static QString GetErrInfo();
};