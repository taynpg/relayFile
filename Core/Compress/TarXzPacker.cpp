#include "TarXzPacker.h"

#include "TarCodec.h"
#include "XzCodec.h"

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <algorithm>
#include <sstream>

#include <File/FileDir.h>
#include <QFile>
#include <QIODevice>

namespace {

constexpr const char* kManifestName = "__relay_manifest__";
constexpr size_t kFileChunk = 64 * 1024;

// One manifest entry per packed file. Traveled inside the archive so the
// receiver is self-contained (no side-channel protocol data needed).
struct ManifestEntry
{
    std::string destPath;
    std::uint16_t permission{};
    template <class Archive> void serialize(Archive& ar) { ar(destPath, permission); }
};

std::vector<char> serializeManifest(const std::vector<ManifestEntry>& m)
{
    std::stringstream ss;
    {
        cereal::BinaryOutputArchive ar(ss);
        ar(m);
    }
    const std::string& s = ss.str();
    return std::vector<char>(s.begin(), s.end());
}

bool deserializeManifest(const std::vector<uint8_t>& data, std::vector<ManifestEntry>& m)
{
    std::stringstream ss;
    ss.write(reinterpret_cast<const char*>(data.data()),
             static_cast<std::streamsize>(data.size()));
    cereal::BinaryInputArchive ar(ss);
    try {
        ar(m);
    } catch (...) {
        return false;
    }
    return true;
}

// QFile sink (push): writes compressed/raw bytes to the output file.
bool fileSink(QFile& f, const uint8_t* data, size_t len)
{
    return f.write(reinterpret_cast<const char*>(data), len) == static_cast<qint64>(len);
}

// QFile source (pull): reads compressed/raw bytes from the input file.
size_t fileSource(QFile& f, uint8_t* buf, size_t len)
{
    auto r = f.read(reinterpret_cast<char*>(buf), static_cast<qint64>(len));
    return r <= 0 ? 0 : static_cast<size_t>(r);
}

} // namespace

bool TarXzPacker::pack(const std::vector<PackItem>& items, const std::string& outPath,
                       std::uint64_t& outSize, std::string& err)
{
    outSize = 0;
    QFile out(QString::fromStdString(outPath));
    if (!out.open(QIODevice::WriteOnly)) {
        err = "cannot open output archive: " + out.errorString().toStdString();
        return false;
    }
    auto sink = [&out](const uint8_t* d, size_t n) { return fileSink(out, d, n); };

    XzEncoder enc;
    if (!enc.open(sink, 6, &err)) {
        out.close();
        return false;
    }
    auto tarSink = [&enc](const uint8_t* d, size_t n) { return enc.write(d, n); };

    TarWriter tw;
    tw.open(tarSink);

    // 先解析每个文件的权限：未显式指定则从源文件自身读取。
    std::vector<std::uint16_t> perms;
    perms.reserve(items.size());
    for (const auto& it : items) {
        if (it.permission != 0) {
            perms.push_back(it.permission);
        } else {
            RFileMeta rmeta;
            FileDir::GetFileRFileMeta(QString::fromStdString(it.srcPath), rmeta);
            perms.push_back(rmeta.permission);
        }
    }

    // Embed manifest as the first entry.
    std::vector<ManifestEntry> manifest;
    manifest.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        manifest.push_back({items[i].destPath, perms[i]});
    }
    auto mb = serializeManifest(manifest);
    if (!tw.addBuffer(kManifestName, 0,
                      reinterpret_cast<const uint8_t*>(mb.data()), mb.size())) {
        err = "failed to write manifest entry";
        enc.close();
        out.close();
        return false;
    }

    // Append each source file; entry name is its numeric index so names stay
    // short and deterministic (receiver matches by order, not by name).
    for (size_t i = 0; i < items.size(); ++i) {
        QFile src(QString::fromStdString(items[i].srcPath));
        if (!src.open(QIODevice::ReadOnly)) {
            err = "cannot open source: " + items[i].srcPath;
            tw.close();
            enc.close();
            out.close();
            return false;
        }
        std::uint64_t size = static_cast<std::uint64_t>(src.size());
        auto srcFn = [&src](uint8_t* d, size_t n) -> size_t { return fileSource(src, d, n); };
        std::string entryName = std::to_string(i);
        if (!tw.addStream(entryName, perms[i], size, srcFn)) {
            err = "failed to stream file: " + items[i].srcPath;
            tw.close();
            enc.close();
            out.close();
            return false;
        }
        src.close();
    }

    if (!tw.close()) {
        err = "failed to finalize tar";
        enc.close();
        out.close();
        return false;
    }
    if (!enc.close(&err)) {
        out.close();
        return false;
    }
    outSize = static_cast<std::uint64_t>(out.size());
    out.close();
    return true;
}

bool TarXzPacker::extract(const std::string& archivePath,
                          std::vector<std::string>& extractedPaths, std::string& err)
{
    QFile in(QString::fromStdString(archivePath));
    if (!in.open(QIODevice::ReadOnly)) {
        err = "cannot open archive: " + in.errorString().toStdString();
        return false;
    }
    auto src = [&in](uint8_t* d, size_t n) -> size_t { return fileSource(in, d, n); };

    XzDecoder dec;
    if (!dec.open(src, &err)) {
        in.close();
        return false;
    }
    auto tarSrc = [&dec](uint8_t* d, size_t n) -> size_t { return dec.read(d, n); };

    TarReader tr;
    tr.open(tarSrc);

    // First entry must be the manifest.
    std::string name;
    std::uint16_t mode = 0;
    std::uint64_t size = 0;
    bool isPax = false;
    if (!tr.nextEntry(name, mode, size, isPax, err)) {
        err = "archive missing manifest entry";
        dec.close();
        in.close();
        return false;
    }
    std::vector<ManifestEntry> manifest;
    if (size == 0) {
        err = "empty manifest";
        dec.close();
        in.close();
        return false;
    }
    std::vector<uint8_t> mbuf(static_cast<size_t>(size));
    if (!tr.readData(mbuf.data(), static_cast<size_t>(size))) {
        err = "manifest read failed";
        dec.close();
        in.close();
        return false;
    }
    tr.skipPadding(size);
    if (!deserializeManifest(mbuf, manifest)) {
        err = "manifest deserialize failed";
        dec.close();
        in.close();
        return false;
    }

    extractedPaths.clear();
    extractedPaths.reserve(manifest.size());
    std::vector<uint8_t> chunk(kFileChunk);

    for (size_t i = 0; i < manifest.size(); ++i) {
        if (!tr.nextEntry(name, mode, size, isPax, err)) {
            err = "missing file entry " + std::to_string(i);
            dec.close();
            in.close();
            return false;
        }
        if (isPax) {
            // Unexpected PAX header for an index entry; skip its payload.
            tr.skipData(size);
            continue;
        }
        const QString dest = QString::fromStdString(manifest[i].destPath);
        if (!FileDir::EnsureDir(FileDir::cdUp(dest))) {
            err = "cannot create dest dir: " + manifest[i].destPath;
            dec.close();
            in.close();
            return false;
        }
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly)) {
            err = "cannot open dest: " + manifest[i].destPath;
            dec.close();
            in.close();
            return false;
        }
        std::uint64_t remaining = size;
        while (remaining > 0) {
            size_t want = static_cast<size_t>(
                std::min<std::uint64_t>(remaining, chunk.size()));
            if (!tr.readData(chunk.data(), want)) {
                err = "file data read failed: " + manifest[i].destPath;
                out.close();
                dec.close();
                in.close();
                return false;
            }
            if (out.write(reinterpret_cast<const char*>(chunk.data()),
                         static_cast<qint64>(want)) != static_cast<qint64>(want)) {
                err = "dest write failed: " + manifest[i].destPath;
                out.close();
                dec.close();
                in.close();
                return false;
            }
            remaining -= want;
        }
        out.close();
        tr.skipPadding(size);
        if (manifest[i].permission != 0) {
            FileDir::SetPermission(dest, manifest[i].permission);
        }
        extractedPaths.push_back(manifest[i].destPath);
    }

    dec.close();
    in.close();
    return true;
}
