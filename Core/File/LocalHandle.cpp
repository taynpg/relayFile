#include "LocalHandle.h"

#include "FileDir.h"
#include "Utils/Common.h"
#include "Utils/miniUtil.h"

bool LocalHandle::AskFileList(const std::string& path, std::vector<FileMeta>& fileList, bool recursive)
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
bool LocalHandle::AskHome(std::string& home)
{
    QString homeStr;
    if (!FileDir::GetHome(homeStr)) {
        return false;
    }
    home = homeStr.toStdString();
    return true;
}

bool LocalHandle::AskFileMeta(const std::string& path, FileMeta& meta)
{
    RFileMeta rmeta;
    FileDir::GetFileRFileMeta(QString::fromStdString(path), rmeta);
    FileDir::TurnMeta(rmeta, meta);
    return true;
}

bool LocalHandle::AskDelete(const std::vector<std::string>& fileList, std::vector<std::string>& failedList)
{
    failedList.clear();
    for (const auto& path : fileList) {
        if (!FileDir::Delete(QString::fromStdString(path))) {
            failedList.push_back(path);
        }
    }
    return true;
}

bool LocalHandle::AskSha256(const std::string& path, std::string& sha256)
{
    QString str = QString::fromStdString(path);
    sha256 = Common::GenSha256(str, true).toStdString();
    return true;
}

bool LocalHandle::AskRename(const std::string& oldName, const std::string& newName)
{
    return FileDir::Rename(QString::fromStdString(oldName), QString::fromStdString(newName));
}

bool LocalHandle::AskCreateDir(const std::string& path)
{
    return FileDir::CreateDir(QString::fromStdString(path));
}

bool LocalHandle::AskArchive(const std::vector<FileMeta>& fileList, const std::string& archivePath)
{
    return false;
}

bool LocalHandle::AskUnArchive(const std::string& archivePath, const std::string& extractPath)
{
    return false;
}

bool LocalHandle::AskHomeAndDriver(std::vector<std::string>& drivers, std::string& home)
{
    if (!AskHome(home)) {
        return false;
    }
    auto ds = Common::GetLocalDrivers();
    drivers.clear();
    for (const auto& driver : ds) {
        drivers.push_back(driver.toStdString());
    }
    return true;
}
