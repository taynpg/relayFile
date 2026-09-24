#pragma once

#include <cstdint>
#include <string>
#include <vector>

// High-level tar.xz packer/unpacker façade. Builds a single .tar.xz from a
// list of files (preserving Unix mode bits) and embeds a manifest so the
// receiver knows where to write each file and which permissions to apply.
//
// Container: ustar tar (with PAX long-name support) + xz/LZMA2 compression.
// The manifest travels as the first tar entry ("__relay_manifest__") so the
// archive is fully self-describing; the receiver needs no side-channel data.

struct PackItem
{
    std::string srcPath;    // source file path (UTF-8)
    std::string destPath;   // destination path on the receiver (UTF-8)
    std::uint16_t permission{};  // unix mode bits (0 if unknown)
};

class TarXzPacker
{
public:
    // Pack `items` into a single .tar.xz at `outPath`. Returns the archive
    // byte size in `outSize`. Failures (e.g. a source file cannot be opened)
    // return false and set `err`.
    static bool pack(const std::vector<PackItem>& items, const std::string& outPath,
                     std::uint64_t& outSize, std::string& err);

    // Extract a .tar.xz archive at `archivePath`: read the embedded manifest
    // and write each file directly to its destination (parent dirs created,
    // permission applied). `extractedPaths` receives the written destPaths.
    static bool extract(const std::string& archivePath,
                        std::vector<std::string>& extractedPaths, std::string& err);
};
