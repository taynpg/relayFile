#include "File/LocalHandle.h"
#include "Protocol/FileMeta.h"

#define MINIZ_NO_ZLIB_APIS
#include <File/miniz.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVector>
#include <cstdint>
#include <iostream>
#include <vector>

// 压缩/解压功能实测：构造多类测试文件 → AskArchive → AskUnArchive → 逐字节比对
// 用法: relayFileZipTest <工作目录>

static int gFail{};

static void check(bool cond, const QString& msg)
{
    if (cond) {
        std::cout << "[PASS] " << msg.toStdString() << std::endl;
    } else {
        std::cout << "[FAIL] " << msg.toStdString() << std::endl;
        ++gFail;
    }
}

static bool writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(data) == data.size();
}

// 递归比对两棵文件树（文件名集合 + 逐字节内容）
static bool treeEqual(const QString& a, const QString& b)
{
    QDir da(a), db(b);
    const auto listA = da.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    const auto listB = db.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    if (listA.size() != listB.size()) {
        std::cout << "  entry count differ: " << listA.size() << " vs " << listB.size() << std::endl;
        return false;
    }
    for (const QString& name : listA) {
        QString pa = a + "/" + name;
        QString pb = b + "/" + name;
        QFileInfo ia(pa), ib(pb);
        if (!ib.exists()) {
            std::cout << "  missing in B: " << name.toStdString() << std::endl;
            return false;
        }
        if (ia.isDir()) {
            if (!ib.isDir() || !treeEqual(pa, pb)) {
                return false;
            }
        } else {
            if (ib.isDir() || ia.size() != ib.size()) {
                return false;
            }
            QFile fa(pa), fb(pb);
            if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) {
                return false;
            }
            if (fa.readAll() != fb.readAll()) {
                std::cout << "  content differ: " << name.toStdString() << std::endl;
                return false;
            }
        }
    }
    return true;
}

static QByteArray makePattern(int seed, int size)
{
    QByteArray d;
    d.reserve(size);
    for (int i = 0; i < size; ++i) {
        d.append(static_cast<char>((seed * 31 + i * 7) & 0xFF));
    }
    return d;
}

static FileMeta itemOf(const QString& fullPath, bool isDir)
{
    FileMeta m;
    m.fullPath = fullPath.toStdString();
    m.type = isDir ? FileType::FILE_TYPE_DIR : FileType::FILE_TYPE_FILE;
    return m;
}

// 检查 zip 中央目录中不存在重名条目
static bool noDupEntries(const QString& zipPath)
{
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0)) {
        return false;
    }
    QStringList names;
    bool unique = true;
    const auto count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE]{};
        if (!mz_zip_reader_get_filename(&zip, i, name, sizeof(name))) {
            continue;
        }
        const QString n(name);
        if (names.contains(n)) {
            std::cout << "  duplicate entry: " << n.toStdString() << std::endl;
            unique = false;
        }
        names.append(n);
    }
    mz_zip_reader_end(&zip);
    return unique;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cout << "usage: relayFileZipTest <workdir>" << std::endl;
        return 2;
    }
    const QString base = QString::fromLocal8Bit(argv[1]);
    QDir(base).removeRecursively();
    QDir().mkpath(base);

    // --- 构造测试数据 ---
    // ASCII 目录：文本 + 500KB 二进制
    check(writeFile(base + "/ascii/a.txt", "hello relay file\n"), "create ascii text");
    check(writeFile(base + "/ascii/bin.dat", makePattern(1, 500 * 1024)), "create ascii binary");

    // 中文目录：中文文件名 + 嵌套 + 空文件
    check(writeFile(base + "/中文目录/文件.txt", "中文内容测试ABC"), "create unicode text");
    check(writeFile(base + "/中文目录/嵌套/deep/空文件.txt", QByteArray()), "create nested empty file");
    check(writeFile(base + "/中文目录/嵌套/deep/data.bin", makePattern(9, 120 * 1024)), "create nested binary");

    // ===== 场景1：ASCII 路径压缩 =====
    {
        std::vector<FileMeta> list{itemOf(base + "/ascii", true)};
        const QString zipPath = base + "/out_ascii.zip";
        bool ok = LocalHandle::AskArchive(list, zipPath.toStdString());
        check(ok, "archive ascii dir");
        check(QFileInfo::exists(zipPath) && QFileInfo(zipPath).size() > 0, "ascii zip non-empty");
        check(noDupEntries(zipPath), "ascii zip has no duplicate entries");

        // 解压
        const QString extractDir = base + "/ex_ascii";
        bool uok = LocalHandle::AskUnArchive(zipPath.toStdString(), extractDir.toStdString());
        check(uok, "unarchive ascii zip");
        check(treeEqual(base + "/ascii", extractDir + "/ascii"), "ascii roundtrip byte-identical");
    }

    // ===== 场景2：中文路径 + 中文压缩包名 =====
    {
        std::vector<FileMeta> list{itemOf(base + "/中文目录", true)};
        const QString zipPath = base + "/压缩包.zip";
        bool ok = LocalHandle::AskArchive(list, zipPath.toStdString());
        check(ok, "archive unicode dir (zip path is Chinese)");
        check(QFileInfo::exists(zipPath) && QFileInfo(zipPath).size() > 0, "unicode zip non-empty");
        check(noDupEntries(zipPath), "unicode zip has no duplicate entries");

        const QString extractDir = base + "/解压目录";
        bool uok = LocalHandle::AskUnArchive(zipPath.toStdString(), extractDir.toStdString());
        check(uok, "unarchive unicode zip (paths are Chinese)");
        check(treeEqual(base + "/中文目录", extractDir + "/中文目录"), "unicode roundtrip byte-identical");
    }

    // ===== 场景3：多选中项混合压缩 =====
    {
        std::vector<FileMeta> list{itemOf(base + "/ascii", true), itemOf(base + "/中文目录", true)};
        const QString zipPath = base + "/mixed.zip";
        bool ok = LocalHandle::AskArchive(list, zipPath.toStdString());
        check(ok, "archive multiple mixed items");
        check(noDupEntries(zipPath), "mixed zip has no duplicate entries");
        const QString extractDir = base + "/ex_mixed";
        bool uok = LocalHandle::AskUnArchive(zipPath.toStdString(), extractDir.toStdString());
        check(uok, "unarchive mixed zip");
        bool all = treeEqual(base + "/ascii", extractDir + "/ascii") &&
                   treeEqual(base + "/中文目录", extractDir + "/中文目录");
        check(all, "mixed roundtrip byte-identical");
    }

    std::cout << (gFail == 0 ? "ALL ZIP TESTS PASSED" : "ZIP TESTS FAILED") << std::endl;
    return gFail == 0 ? 0 : 1;
}
