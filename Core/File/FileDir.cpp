#include "FileDir.h"

#include <QDateTime>
#include <QDir>
#include <QQueue>
#include <Utils/miniUtil.h>

thread_local QString errInfo;

bool FileDir::IsDir(const QString& path)
{
    QFileInfo info(path);
    return info.exists() && info.isDir();
}

bool FileDir::IsFile(const QString& path)
{
    QFileInfo info(path);
    return info.exists() && info.isFile();
}

bool FileDir::IsExist(const QString& path)
{
    QFileInfo info(path);
    return info.exists();
}

bool FileDir::IsExist(const QString& path, std::uint64_t& fileSize)
{
    QFileInfo info(path);
    if (info.exists()) {
        fileSize = info.size();
        return true;
    }
    return false;
}

bool FileDir::Delete(const QString& path)
{
    QFileInfo fi(path);

    if (!fi.exists()) {
        return false;
    }

    if (fi.isDir()) {
        QDir dir(path);
        return dir.removeRecursively();
    }

    return QFile::remove(path);
}

QString FileDir::GetErrInfo()
{
    return errInfo;
}

bool isWindowsAbsolutePath(const QString& p)
{
    if (p.length() < 3) {
        return false;
    }

    const QChar drive = p.at(0);
    if (!drive.isLetter()) {
        return false;
    }

    return p.at(1) == QLatin1Char(':') && (p.at(2) == QLatin1Char('/') || p.at(2) == QLatin1Char('\\'));
}

bool isUncPath(const QString& p)
{
    return p.startsWith("//") || p.startsWith("\\\\");
}

bool isUnixAbsolutePath(const QString& p)
{
    return p.startsWith('/');
}

QString FileDir::cdUp(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }

    QString p = QDir::fromNativeSeparators(path);

    // Windows absolute path: C:/...
    if (isWindowsAbsolutePath(p)) {
        int idx = p.lastIndexOf('/', p.length() - 2);
        if (idx <= 2) {
            return p.left(3);   // C:/
        }
        return p.left(idx + 1);
    }

    // UNC path: //server/share/...
    if (isUncPath(p)) {
        QStringList parts = p.mid(2).split('/', Qt::SkipEmptyParts);
        if (parts.size() <= 2) {
            return "//" + parts.join('/');
        }
        parts.removeLast();
        return "//" + parts.join('/');
    }

    // Unix-style absolute path: /...
    if (isUnixAbsolutePath(p)) {
        if (p == "/") {
            return "/";
        }

        QString trimmed = p;
        if (trimmed.endsWith('/')) {
            trimmed.chop(1);
        }

        int idx = trimmed.lastIndexOf('/');
        if (idx <= 0) {
            return "/";
        }

        return trimmed.left(idx + 1);
    }

    // Relative path: resolve to absolute path first
    QFileInfo fi(p);
    QString abs = QDir::cleanPath(fi.absoluteFilePath());

    // Recurse only once
    return FileDir::cdUp(abs);
}

bool FileDir::GetFileList(const QString& path, QVector<RFileMeta>& fileList, bool recursive)
{
    fileList.clear();
    QQueue<QString> dirQueue;
    dirQueue.enqueue(path);

    while (!dirQueue.isEmpty()) {
        QString currentDir = dirQueue.dequeue();
        QDir qdir(currentDir);

        if (!qdir.exists() || !qdir.isReadable()) {
            continue;
        }

        const auto entries = qdir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

        for (const auto& entry : entries) {

            RFileMeta info;
            info.dir = entry.isDir() ? entry.absoluteFilePath() : entry.path();
            info.fileName = entry.fileName();
            info.permission = entry.permissions();
            info.mark = GetMark();

            if (entry.isDir()) {
                info.fileType = RFileType::mTypeDir;
                info.fileSize = 0;
                if (recursive) {
                    dirQueue.enqueue(entry.absoluteFilePath());
                    continue;
                }
            } else if (entry.isFile()) {
                info.fileType = RFileType::mTypeFile;
                info.fileSize = entry.size();
            } else {
                continue;
            }

            info.lastModified = entry.lastModified().toMSecsSinceEpoch();
            fileList.append(info);
        }
    }

    return true;
}

bool FileDir::GetHome(QString& home)
{
    home = QDir::homePath();
    return true;
}

void FileDir::TurnMeta(const RFileMeta& rmeta, FileMeta& meta)
{
    meta.fullPath = Join(rmeta.dir, rmeta.fileName).toStdString();
    meta.dir = rmeta.dir.toStdString();
    meta.name = rmeta.fileName.toStdString();
    meta.size = rmeta.fileSize;
    meta.sizeStr = miniUtil::GetSizeInfo(rmeta.fileSize);
    meta.lastModified = rmeta.lastModified;
    meta.permission = rmeta.permission;
    meta.mark = rmeta.mark;
    meta.type = rmeta.fileType == RFileType::mTypeDir ? FileType::FILE_TYPE_DIR : FileType::FILE_TYPE_FILE;
    meta.exist = rmeta.exist;
}

QString FileDir::Join(const QString& path, const QString& name)
{
    QDir dir(path);
    QString combined = dir.filePath(name);
    return QDir::cleanPath(combined);
}

QString FileDir::Join(const QString& path, const QString& n1, const QString& n2)
{
    QDir dir(path);
    QString combined = dir.filePath(n1);
    combined = QDir(combined).filePath(n2);
    return QDir::cleanPath(combined);
}

QString FileDir::GenDir(const QString& fullPath)
{
    if (fullPath.isEmpty()) {
        return {};
    }

    QString p = QDir::fromNativeSeparators(fullPath);

    // Windows absolute path (X:/)
    if (isWindowsAbsolutePath(p)) {
        int idx = p.lastIndexOf('/');
        if (idx <= 2) {
            return p.left(3);   // D:/
        }
        return p.left(idx + 1);
    }

    // Unix absolute path
    if (p.startsWith('/')) {
        QFileInfo fi(p);
        return QDir::cleanPath(fi.absolutePath());
    }

    // Relative path: resolve based on current working directory
    QFileInfo fi(p);
    return QDir::cleanPath(fi.absolutePath());
}

QString FileDir::GenFileName(const QString& fullPath)
{
    QFileInfo fileInfo(fullPath);
    return fileInfo.fileName();
}

bool FileDir::GetFileNameNoExt(const QString& path, QString& fileName)
{
    QFileInfo fileInfo(path);
    fileName = fileInfo.completeBaseName();
    return true;
}

uint16_t FileDir::GetMark()
{
#if defined(_WIN32)
    return 0;
#else
    return 1;
#endif
}

bool FileDir::SetPermission(const QString& path, quint16 permission)
{
    if (path.isEmpty()) {
        return false;
    }

    QFile file(path);
    if (!file.exists()) {
        return false;
    }

    QFile::Permissions perms = static_cast<QFile::Permissions>(permission);
    const bool ok = file.setPermissions(perms);
    return ok;
}

QString FileDir::GenOutPath(const QString& root, const std::string& fullPath, const QString& outRoot)
{
    return GenOutPath(root, QString::fromStdString(fullPath), outRoot);
}

QString FileDir::GenOutPath(const QString& root, const QString& fullPath, const QString& outRoot)
{
    if (root.isEmpty() || fullPath.isEmpty() || outRoot.isEmpty()) {
        return QString();
    }

    QFileInfo rootInfo(root);
    QFileInfo fullPathInfo(fullPath);

    QString absRoot = QDir::cleanPath(rootInfo.absoluteFilePath());
    QString absFull = QDir::cleanPath(fullPathInfo.absoluteFilePath());

    if (!absRoot.endsWith('/')) {
        absRoot += '/';
    }

    if (!absFull.startsWith(absRoot, Qt::CaseInsensitive)) {
        return QString();
    }

    QString relativePath = absFull.mid(absRoot.length());
    QDir outDir(outRoot);
    QString result = outDir.filePath(relativePath);

    return QDir::cleanPath(result);
}

void FileDir::GetFileRFileMeta(const QString& path, RFileMeta& rmeta)
{
    QFileInfo info(path);
    if (info.exists()) {
        rmeta.exist = 1;
        rmeta.dir = GenDir(path);
        rmeta.fileName = info.fileName();
        rmeta.permission = info.permissions();
        rmeta.fileType = info.isDir() ? RFileType::mTypeDir : RFileType::mTypeFile;
        rmeta.fileSize = info.size();
        rmeta.lastModified = info.lastModified().toMSecsSinceEpoch();
        rmeta.mark = GetMark();
        return;
    }
    rmeta.exist = 0;
}

bool FileDir::CreateDir(const QString& path)
{
    QDir dir;
    if (dir.exists(path)) {
        return false;
    }
    return dir.mkpath(path);
}

bool FileDir::EnsureDir(const QString& path)
{
    QDir dir;
    if (dir.exists(path)) {
        return true;
    }
    return dir.mkpath(path);
}

bool FileDir::Rename(const QString& oldName, const QString& newName)
{
    QFile file(oldName);
    return file.rename(newName);
}
