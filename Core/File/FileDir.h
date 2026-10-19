#pragma once

#include <QString>
#include <QVector>

#include "Protocol/FileMeta.h"

#ifdef Q_OS_WIN
const QChar targetSep('\\');
const QChar wrongSep('/');
#else
const QChar targetSep('/');
const QChar wrongSep('\\');
#endif

const QChar unixSep('/');
const QChar winSep('\\');

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

    // 断点续传用的快速指纹：文件长度 + 首 64KB 的 SHA-256。
    // 只读 64KB，代价可控；用来确认 .relaypart 与源文件是同一份数据，
    // 避免把两个不同文件的前后两截拼成一个"成功"的文件。
    // sizeOverride < 0 时取文件实际长度。接收方必须传"期望的总长度"：
    // 残片只有一部分，用残片自己的长度算出来的指纹永远对不上源文件。
    static QString QuickFingerprint(const QString& path, qint64 sizeOverride = -1);
    static bool ResizeFile(const QString& path, qint64 newSize);
};
