#pragma once

#include <QString>
#include <QStringList>
#include <string>
#include <vector>

#include "Protocol/FileMeta.h"

class LocalHandle
{
public:
    LocalHandle() = default;
    ~LocalHandle() = default;

public:
    static bool AskFileList(const std::string& path, std::vector<FileMeta>& fileList, bool recursive);
    static bool AskHome(std::string& home);
    static bool AskFileMeta(const std::string& path, FileMeta& meta);
    static bool AskDelete(const std::vector<std::string>& fileList, std::vector<std::string>& failedList);
    static bool AskSha256(const std::string& path, std::string& sha256);
    static bool AskRename(const std::string& oldName, const std::string& newName);
    static bool AskCreateDir(const std::string& path);
    static bool AskArchive(const std::vector<FileMeta>& fileList, const std::string& archivePath);
    static bool AskUnArchive(const std::string& archivePath, const std::string& extractPath);
    static bool AskHomeAndDriver(std::vector<std::string>& drivers, std::string& home);
};

struct ArchiveItem {
    QString absPath;
    bool isDir{false};
};

class ZipHandle
{
public:
    ZipHandle() = default;
    ~ZipHandle() = default;

    bool Archive(const QStringList& inputPaths, const QString& archivePath);
    bool UnArchive(const QString& archivePath, const QString& extractPath);

private:
    static bool ensureDir(const QString& dir);
    static QString calcZipPath(const QString& root, const QString& file);
};