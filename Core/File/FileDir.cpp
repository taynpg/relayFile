#include "FileDir.h"

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

QString FileDir::GetErrInfo()
{
    return errInfo;
}

QString FileDir::cdUp(const QString& path)
{
    QDir qdir(path);
    return qdir.cdUp() ? qdir.absolutePath() : path;
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
            info.dir = entry.absoluteFilePath();
            info.fileName = entry.fileName();
            info.permission = entry.permissions();

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
    meta.fullPath = rmeta.dir.toStdString();
    meta.dir = rmeta.dir.toStdString();
    meta.name = rmeta.fileName.toStdString();
    meta.size = rmeta.fileSize;
    meta.sizeStr = miniUtil::GetSizeInfo(rmeta.fileSize);
    meta.lastModified = rmeta.lastModified;
    meta.permission = rmeta.permission;
    meta.type = rmeta.fileType == RFileType::mTypeDir ? FileType::FILE_TYPE_DIR : FileType::FILE_TYPE_FILE;
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
    QFileInfo fileInfo(fullPath);
    return fileInfo.dir().path();
}
QString FileDir::GenFileName(const QString& fullPath)
{
    QFileInfo fileInfo(fullPath);
    return fileInfo.fileName();
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
