#pragma once

#include <QString>
#include <QVector>

#include "Protocol/FileMeta.h"

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
    uint16_t exist{};
    uint16_t mark{};
};

class FileDir
{
public:
    FileDir() = default;
    ~FileDir() = default;

public:
    static bool GetHome(QString& home);
    static bool GetFileList(const QString& path, QVector<RFileMeta>& fileList, bool recursive);
    static void GetFileRFileMeta(const QString& path, RFileMeta& rmeta);
    static void TurnMeta(const RFileMeta& rmeta, FileMeta& meta);
    static QString GetErrInfo();
    static QString cdUp(const QString& path);
    static QString Join(const QString& path, const QString& name);
    static QString Join(const QString& path, const QString& n1, const QString& n2);
    static QString GenOutPath(const QString& root, const QString& fullPath, const QString& outRoot);
    static QString GenOutPath(const QString& root, const std::string& fullPath, const QString& outRoot);
    static QString GenDir(const QString& fullPath);
    static QString GenFileName(const QString& fullPath);
    static bool IsDir(const QString& path);
    static bool IsFile(const QString& path);
    static bool IsExist(const QString& path);
    static bool IsExist(const QString& path, std::uint64_t& fileSize);
    static bool Delete(const QString& path);
    static bool CreateDir(const QString& path);
    static bool EnsureDir(const QString& path);
    static bool Rename(const QString& oldName, const QString& newName);
    static bool GetFileNameNoExt(const QString& path, QString& fileName);
    static uint16_t GetMark();
    static bool SetPermission(const QString& path, quint16 permission);
};