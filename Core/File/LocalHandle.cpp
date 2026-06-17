#include "LocalHandle.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "FileDir.h"
#include "Utils/Common.h"
#include "Utils/miniUtil.h"

#define MINIZ_NO_ZLIB_APIS
#include "miniz.h"

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
    ZipHandle zipHandle;
    QStringList inputPaths;
    for (const auto& meta : fileList) {
        ArchiveItem arItem;
        arItem.isDir = (meta.type == FileType::FILE_TYPE_DIR);
        arItem.absPath = QString::fromStdString(meta.fullPath);
        inputPaths.append(arItem.absPath);
    }
    return zipHandle.Archive(inputPaths, QString::fromStdString(archivePath));
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
    for (const auto& driver : ds) {
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
    QFileInfo fi(root);
    QString baseName = fi.fileName();
    QString relative = file.mid(root.length());

    if (relative.startsWith('/') || relative.startsWith('\\'))
        relative.remove(0, 1);

    return QDir(baseName).filePath(relative).replace("\\", "/");
}

bool ZipHandle::Archive(const QStringList& inputPaths, const QString& archivePath)
{
    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_file(&zip, archivePath.toUtf8().constData(), 0)) {
        qWarning() << "Failed to create zip:" << archivePath;
        return false;
    }

    for (const QString& input : inputPaths) {
        QFileInfo fi(input);

        if (!fi.exists()) {
            mz_zip_writer_end(&zip);
            return false;
        }

        if (fi.isDir()) {
            QDir dir(input);

            QStringList files = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);

            for (const QString& f : files) {
                QString child = dir.filePath(f);
                if (!Archive({child}, archivePath)) {
                    mz_zip_writer_end(&zip);
                    return false;
                }
            }
            continue;
        }

        QString zipPath = calcZipPath(fi.absolutePath(), fi.absoluteFilePath());

        QFile file(input);
        if (!file.open(QIODevice::ReadOnly)) {
            mz_zip_writer_end(&zip);
            return false;
        }

        QByteArray data = file.readAll();

        if (!mz_zip_writer_add_mem(&zip, zipPath.toUtf8().constData(), data.constData(), data.size(), MZ_DEFAULT_COMPRESSION)) {
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

    ensureDir(extractPath);

    const mz_uint n = mz_zip_reader_get_num_files(&zip);

    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            continue;

        QString relPath = QString::fromUtf8(st.m_filename);
        QString fullPath = QDir(extractPath).filePath(relPath);

        if (st.m_is_directory || relPath.endsWith('/')) {
            ensureDir(fullPath);
            continue;
        }

        ensureDir(QFileInfo(fullPath).absolutePath());

        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);

        if (!data) {
            mz_zip_reader_end(&zip);
            return false;
        }

        QFile out(fullPath);
        if (!out.open(QIODevice::WriteOnly)) {
            free(data);
            mz_zip_reader_end(&zip);
            return false;
        }

        out.write(static_cast<const char*>(data), static_cast<qint64>(size));
        free(data);
    }

    mz_zip_reader_end(&zip);
    return true;
}