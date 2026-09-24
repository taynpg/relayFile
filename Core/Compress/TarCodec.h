#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Minimal ustar + PAX extended-header tar reader/writer.
// Carries Unix mode bits so cross-platform file permission metadata
// (e.g. Linux executable bit) survives a same-platform transfer.
//
// ByteSink   = std::function<bool(const uint8_t*, size_t)>  (push)
// ByteSource = std::function<size_t(uint8_t*, size_t)>      (pull, 0 = eof)

class TarWriter
{
public:
    TarWriter();
    ~TarWriter();
    TarWriter(const TarWriter&) = delete;
    TarWriter& operator=(const TarWriter&) = delete;

    // Begin writing a tar stream; bytes pushed to `sink`.
    bool open(const std::function<bool(const uint8_t*, size_t)>& sink);

    // Add a regular file from an in-memory buffer.
    bool addBuffer(const std::string& name, uint16_t mode,
                   const uint8_t* data, size_t len);

    // Add a regular file by streaming its contents through `source`.
    // `size` is the exact byte count `source` will produce.
    bool addStream(const std::string& name, uint16_t mode, uint64_t size,
                   const std::function<size_t(uint8_t*, size_t)>& source);

    // Finalize: write two zero blocks (EOF marker).
    bool close();

private:
    bool emitHeader(const std::string& name, uint16_t mode, uint64_t size, char typeflag);
    bool emitPaxHeader(const std::string& name, uint16_t mode, uint64_t size);
    bool padToBlock(uint64_t bytesAlready);

    std::function<bool(const uint8_t*, size_t)> sink_;
    bool open_ = false;
};

class TarReader
{
public:
    TarReader();
    ~TarReader();
    TarReader(const TarReader&) = delete;
    TarReader& operator=(const TarReader&) = delete;

    // Begin reading; compressed/raw bytes pulled from `source`.
    bool open(const std::function<size_t(uint8_t*, size_t)>& source);

    // Advance to the next entry. Returns false on end-of-archive or error.
    // On success: outName = entry name, outMode = unix mode bits,
    // outSize = payload size, isPaxData indicates a PAX 'x' extended header
    // whose payload should be parsed instead of treated as file data.
    bool nextEntry(std::string& outName, uint16_t& outMode, uint64_t& outSize,
                   bool& outIsPax, std::string& err);

    // Read exactly `n` payload bytes into `out`. Returns false on short read.
    bool readData(uint8_t* out, size_t n);

    // Skip trailing block padding after `dataSize` payload bytes were read.
    bool skipPadding(uint64_t dataSize);

    // Skip `n` payload bytes plus trailing block padding. Returns false on error.
    bool skipData(uint64_t n);

    void close();

private:
    bool readBlock(uint8_t* buf);

    std::function<size_t(uint8_t*, size_t)> source_;
    bool open_ = false;
};
