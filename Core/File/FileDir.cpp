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

QString FileDir::cdUp(const QString& path)
{
    auto p = miniPath::cdUp(path.toStdString());
    return QString::fromStdString(p);
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
    home.replace(wrongSep, targetSep);
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

static bool IsWindowsPath(const QString& path)
{
    if (path.startsWith("\\\\")) {
        return true;
    }
    if (path.length() >= 2 && path[1] == QLatin1Char(':')) {
        return true;
    }
    return false;
}

QString FileDir::Join(const QString& path, const QString& name)
{
    if (path.isEmpty()) {
        return name;
    }

    if (name.isEmpty()) {
        return path;
    }

    /* 判断路径风格 */
    bool isWindows = IsWindowsPath(path);
    QChar sep = isWindows ? winSep : unixSep;

    QString p = path;

    /* 去掉尾部多余分隔符 */
    while (!p.isEmpty() && (p.back() == unixSep || p.back() == winSep)) {
        p.chop(1);
    }

    /* 去掉 name 前导分隔符 */
    QString n = name;
    while (!n.isEmpty() && (n.front() == unixSep || n.front() == winSep)) {
        n.remove(0, 1);
    }

    if (n.isEmpty()) {
        return p + sep;
    }

    QString result = p + sep + n;

    /* 只做规范化，不改变风格 */
    return QDir::cleanPath(result);
}

QString FileDir::Join(const QString& path, const QString& n1, const QString& n2)
{
    auto p = Join(path, n1);
    return Join(p, n2);
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

    auto lexRoot = root;
    auto lexFull = fullPath;

    lexRoot.replace('\\', '/');
    lexFull.replace('\\', '/');

    if (!lexRoot.endsWith('/')) {
        lexRoot += '/';
    }

    if (!lexFull.startsWith(lexRoot, Qt::CaseInsensitive)) {
        return QString();
    }

    QString relativePath = lexFull.mid(lexRoot.length());

    auto lexOutRoot = outRoot;
    if (lexOutRoot.startsWith('/')) {
        if (!lexOutRoot.endsWith('/')) {
            lexOutRoot += '/';
        }
        lexOutRoot += relativePath;
    } else {
        if (!lexOutRoot.endsWith('\\')) {
            lexOutRoot += '\\';
        }
        lexOutRoot += relativePath;
    }
    return lexOutRoot;
}

void FileDir::GetFileRFileMeta(const QString& path, RFileMeta& rmeta)
{
    QFileInfo info(path);
    if (info.exists()) {
        rmeta.exist = 1;
        rmeta.dir = cdUp(path);
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
