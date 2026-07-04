#include "LocalHandle.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

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
    if (fileList.empty()) {
        return false;
    }
    auto parentPath = FileDir::cdUp(QString::fromStdString(fileList[0].fullPath));
    ZipHandle zipHandle;
    QStringList inputPaths;
    for (const auto& meta : fileList) {
        QString path = QString::fromStdString(meta.fullPath);
        inputPaths.append(path);
    }
    return zipHandle.Archive(parentPath, inputPaths, QString::fromStdString(archivePath));
}

bool LocalHandle::AskUnArchive(const std::string& archivePath, const std::string& extractPath)
{
    ZipHandle zipHandle;
    return zipHandle.UnArchive(QString::fromStdString(archivePath), QString::fromStdString(extractPath));
}

bool LocalHandle::AskHomeAndDriver(std::vector<std::string>& drivers, std::string& home)
{
    if (!AskHome(home)) {
        return false;
    }
    auto ds = Common::GetLocalDrivers();
    drivers.clear();
    for (auto& driver : ds) {
        driver.replace(wrongSep, targetSep);
        drivers.push_back(driver.toStdString());
    }
    return true;
}

bool ZipHandle::ensureDir(const QString& dir)
{
    return QDir().mkpath(dir);
}

QString ZipHandle::calcZipPath(const QString& root, const QString& file)
{
    QDir rootDir(root);
    QString rel = rootDir.relativeFilePath(file);

    if (rel.isEmpty() || rel == ".") {
        return QFileInfo(file).fileName();
    }

    return rel.replace(wrongSep, targetSep);
}

bool ZipHandle::addToZip(mz_zip_archive& zip, const QString& rootPath, const QString& filePath)
{
    QFileInfo fi(filePath);

    if (fi.isDir()) {
        QString zipPath = calcZipPath(rootPath, filePath);
        if (!zipPath.endsWith(targetSep)) {
            zipPath += targetSep;
        }

        if (!mz_zip_writer_add_mem(&zip, zipPath.toUtf8().constData(), nullptr, 0, MZ_DEFAULT_COMPRESSION)) {
            qWarning() << "Failed to add directory:" << zipPath;
            return false;
        }

        QDirIterator it(filePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            if (!addToZip(zip, rootPath, it.next())) {
                return false;
            }
        }
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    QString zipPath = calcZipPath(rootPath, filePath);

    return mz_zip_writer_add_mem(&zip, zipPath.toUtf8().constData(), data.constData(), static_cast<size_t>(data.size()),
                                 MZ_DEFAULT_COMPRESSION);
}

bool ZipHandle::Archive(const QString& rootPath, const QStringList& inputPaths, const QString& archivePath)
{
    mz_zip_archive zip = {};

    if (!mz_zip_writer_init_file(&zip, archivePath.toUtf8().constData(), 0)) {
        qWarning() << "Failed to create zip:" << archivePath;
        return false;
    }

    for (const QString& input : inputPaths) {
        QFileInfo fi(input);
        if (!fi.exists()) {
            qWarning() << "Input does not exist:" << input;
            mz_zip_writer_end(&zip);
            return false;
        }

        if (!addToZip(zip, rootPath, input)) {
            mz_zip_writer_end(&zip);
            return false;
        }
    }

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return true;
}

bool ZipHandle::UnArchive(const QString& archivePath, const QString& extractPath)
{
    mz_zip_archive zip = {};

    if (!mz_zip_reader_init_file(&zip, archivePath.toUtf8().constData(), 0)) {
        qWarning() << "Failed to open zip:" << archivePath;
        return false;
    }

    std::shared_ptr<void> recv(nullptr, [&](void*) { mz_zip_reader_end(&zip); });

    // 确保解压根目录存在
    if (!ensureDir(extractPath)) {
        qWarning() << "Failed to create extract path:" << extractPath;
        return false;
    }

    const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);

    for (mz_uint i = 0; i < fileCount; ++i) {
        // 关键：立刻拿到文件名，避免 m_filename 被覆盖
        char rawName[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
        if (!mz_zip_reader_get_filename(&zip, i, rawName, sizeof(rawName))) {
            continue;
        }

        QString relPath = QString::fromUtf8(rawName).replace(wrongSep, targetSep);
        if (relPath.isEmpty()) {
            continue;
        }

        const QString fullPath = QDir::cleanPath(extractPath + targetSep + relPath);

        if (!fullPath.startsWith(QDir::cleanPath(extractPath))) {
            qWarning() << "Zip slip detected:" << relPath;
            return false;
        }

        if (relPath.endsWith(targetSep)) {
            if (!ensureDir(fullPath)) {
                qWarning() << "Failed to create directory:" << fullPath;
                return false;
            }
            continue;
        }

        if (!ensureDir(QFileInfo(fullPath).absolutePath())) {
            qWarning() << "Failed to create parent dir for:" << fullPath;
            return false;
        }

        if (!mz_zip_reader_extract_to_file(&zip, i, fullPath.toUtf8().constData(), 0)) {
            qWarning() << "Failed to extract file:" << fullPath;
            return false;
        }
    }

    return true;
}