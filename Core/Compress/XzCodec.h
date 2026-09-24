#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// Streaming xz (LZMA2) codec wrapping liblzma. File-agnostic; I/O goes
// through caller-provided byte sinks/sources so the codec can be wired
// directly into a tar<->xz pipeline without intermediate temp files.
//
// ByteSink   = std::function<bool(const uint8_t*, size_t)>  (push: returns false to abort)
// ByteSource = std::function<size_t(uint8_t*, size_t)>      (pull: returns bytes read, 0 on eof)

class XzEncoder
{
public:
    XzEncoder();
    ~XzEncoder();
    XzEncoder(const XzEncoder&) = delete;
    XzEncoder& operator=(const XzEncoder&) = delete;

    // Begin a new .xz stream; compressed bytes are pushed to sink.
    // preset: 0..9 (6 = default, good ratio/speed balance).
    bool open(const std::function<bool(const uint8_t*, size_t)>& sink,
              uint32_t preset = 6, std::string* err = nullptr);
    // Feed raw (uncompressed) bytes to be compressed.
    bool write(const uint8_t* data, size_t len);
    // Finalize the .xz stream and flush all remaining compressed bytes.
    bool close(std::string* err = nullptr);

private:
    struct State;
    State* s_;
};

class XzDecoder
{
public:
    XzDecoder();
    ~XzDecoder();
    XzDecoder(const XzDecoder&) = delete;
    XzDecoder& operator=(const XzDecoder&) = delete;

    // Begin decoding; compressed bytes are pulled from `source` on demand.
    bool open(const std::function<size_t(uint8_t*, size_t)>& source, std::string* err = nullptr);
    // Pull up to `len` decompressed bytes into `buf`. Returns bytes read (0 = eof).
    size_t read(uint8_t* buf, size_t len);
    // Release the decoder state.
    void close();

private:
    struct State;
    State* s_;
};
