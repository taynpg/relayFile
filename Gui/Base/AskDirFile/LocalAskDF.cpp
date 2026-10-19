#include "LocalAskDF.h"

#include <File/LocalHandle.h>

bool LocalAskDF::AskFileList(const std::string& path, std::vector<FileMeta>& fileList, bool recursive)
{
    return LocalHandle::AskFileList(path, fileList, recursive);
}
bool LocalAskDF::AskHome(std::string& home)
{
    return LocalHandle::AskHome(home);
}

bool LocalAskDF::AskFileMeta(const std::string& path, FileMeta& meta)
{
    return LocalHandle::AskFileMeta(path, meta);
}

bool LocalAskDF::AskFileSamples(const std::string& path, std::vector<SampleBlock>& samples)
{
    return LocalHandle::AskFileSamples(path, samples);
}

bool LocalAskDF::AskFileMetaBatch(const std::vector<std::string>& paths, std::vector<FileMeta>& metas)
{
    metas.clear();
    metas.reserve(paths.size());
    for (const auto& path : paths) {
        FileMeta meta;
        LocalHandle::AskFileMeta(path, meta);
        if (meta.exist == 0 && meta.fullPath.empty()) {
            meta.fullPath = path;
        }
        metas.push_back(meta);
    }
    return true;
}

bool LocalAskDF::AskFileSamplesBatch(const std::vector<std::string>& paths,
                                     std::vector<std::vector<SampleBlock>>& samples)
{
    samples.clear();
    samples.reserve(paths.size());
    for (const auto& path : paths) {
        std::vector<SampleBlock> one;
        LocalHandle::AskFileSamples(path, one);
        samples.push_back(std::move(one));
    }
    return true;
}

bool LocalAskDF::AskDelete(const std::vector<std::string>& fileList, std::vector<std::string>& failedList)
{
    return LocalHandle::AskDelete(fileList, failedList);
}

bool LocalAskDF::AskSha256(const std::string& path, std::string& sha256)
{
    return LocalHandle::AskSha256(path, sha256);
}

bool LocalAskDF::AskRename(const std::string& oldName, const std::string& newName)
{
    return LocalHandle::AskRename(oldName, newName);
}

bool LocalAskDF::AskCreateDir(const std::string& path)
{
    return LocalHandle::AskCreateDir(path);
}

bool LocalAskDF::AskArchive(const std::vector<FileMeta>& fileList, const std::string& archivePath)
{
    return LocalHandle::AskArchive(fileList, archivePath);
}

bool LocalAskDF::AskUnArchive(const std::string& archivePath, const std::string& extractPath)
{
    return LocalHandle::AskUnArchive(archivePath, extractPath);
}

bool LocalAskDF::AskHomeAndDriver(std::vector<std::string>& drivers, std::string& home)
{
    return LocalHandle::AskHomeAndDriver(drivers, home);
}
