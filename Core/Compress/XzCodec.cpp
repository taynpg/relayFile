#include "XzCodec.h"

#include <lzma.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace {
constexpr size_t kBufSize = 256 * 1024;

inline const char* lzmaErrStr(lzma_ret r)
{
    switch (r) {
        case LZMA_OK: return "ok";
        case LZMA_STREAM_END: return "stream end";
        case LZMA_NO_CHECK: return "no integrity check";
        case LZMA_UNSUPPORTED_CHECK: return "unsupported check";
        case LZMA_MEM_ERROR: return "memory allocation failed";
        case LZMA_MEMLIMIT_ERROR: return "memory usage limit reached";
        case LZMA_FORMAT_ERROR: return "invalid file format";
        case LZMA_OPTIONS_ERROR: return "invalid or unsupported options";
        case LZMA_DATA_ERROR: return "data is corrupt";
        case LZMA_BUF_ERROR: return "no progress possible";
        case LZMA_PROG_ERROR: return "programming error";
        case LZMA_SEEK_NEEDED: return "seek needed";
        default: return "unknown error";
    }
}
} // namespace

// ---------------------------------------------------------------------------
// XzEncoder
// ---------------------------------------------------------------------------
struct XzEncoder::State
{
    lzma_stream strm{};
    std::function<bool(const uint8_t*, size_t)> sink;
    std::vector<uint8_t> outBuf;
    bool active = false;
};

XzEncoder::XzEncoder() : s_(new State()) { s_->strm = LZMA_STREAM_INIT; }
XzEncoder::~XzEncoder() { if (s_->active) close(); }

bool XzEncoder::open(const std::function<bool(const uint8_t*, size_t)>& sink,
                     uint32_t preset, std::string* err)
{
    if (s_->active) {
        if (err) *err = "encoder already open";
        return false;
    }
    s_->sink = sink;
    s_->outBuf.assign(kBufSize, 0);
    s_->strm = LZMA_STREAM_INIT;
    // CRC64 is liblzma's default integrity check; single-threaded encoder.
    lzma_ret r = lzma_easy_encoder(&s_->strm, preset, LZMA_CHECK_CRC64);
    if (r != LZMA_OK) {
        if (err) *err = std::string("lzma_easy_encoder failed: ") + lzmaErrStr(r);
        return false;
    }
    s_->active = true;
    return true;
}

bool XzEncoder::write(const uint8_t* data, size_t len)
{
    if (!s_->active || len == 0) return len == 0;
    s_->strm.next_in = const_cast<uint8_t*>(data);
    s_->strm.avail_in = len;
    for (;;) {
        s_->strm.next_out = s_->outBuf.data();
        s_->strm.avail_out = s_->outBuf.size();
        lzma_ret r = lzma_code(&s_->strm, LZMA_RUN);
        if (s_->strm.avail_out < s_->outBuf.size()) {
            size_t produced = s_->outBuf.size() - s_->strm.avail_out;
            if (!s_->sink(s_->outBuf.data(), produced)) return false;
        }
        if (r == LZMA_STREAM_END) break; // unexpected for LZMA_RUN, tolerate
        if (r != LZMA_OK) return false;  // hard error
        if (s_->strm.avail_in == 0) break; // all input consumed
    }
    return true;
}

bool XzEncoder::close(std::string* err)
{
    if (!s_->active) return true;
    s_->strm.next_in = nullptr;
    s_->strm.avail_in = 0;
    bool ok = true;
    for (;;) {
        s_->strm.next_out = s_->outBuf.data();
        s_->strm.avail_out = s_->outBuf.size();
        lzma_ret r = lzma_code(&s_->strm, LZMA_FINISH);
        if (s_->strm.avail_out < s_->outBuf.size()) {
            size_t produced = s_->outBuf.size() - s_->strm.avail_out;
            if (!s_->sink(s_->outBuf.data(), produced)) { ok = false; break; }
        }
        if (r == LZMA_STREAM_END) break;
        if (r != LZMA_OK) {
            if (err) *err = std::string("lzma finish failed: ") + lzmaErrStr(r);
            ok = false;
            break;
        }
    }
    lzma_end(&s_->strm);
    s_->active = false;
    return ok;
}

// ---------------------------------------------------------------------------
// XzDecoder
// ---------------------------------------------------------------------------
struct XzDecoder::State
{
    lzma_stream strm{};
    std::function<size_t(uint8_t*, size_t)> source;
    std::vector<uint8_t> inBuf;
    std::vector<uint8_t> outBuf;  // decoded bytes ready for read()
    size_t outPos = 0;             // next byte to deliver from outBuf
    size_t outAvail = 0;           // valid bytes in outBuf
    bool eof = false;              // decoder reported STREAM_END
    bool active = false;
    bool inEof = false;            // source returned 0
};

XzDecoder::XzDecoder() : s_(new State()) { s_->strm = LZMA_STREAM_INIT; }
XzDecoder::~XzDecoder() { close(); }

bool XzDecoder::open(const std::function<size_t(uint8_t*, size_t)>& source, std::string* err)
{
    if (s_->active) {
        if (err) *err = "decoder already open";
        return false;
    }
    s_->source = source;
    s_->inBuf.assign(kBufSize, 0);
    s_->outBuf.assign(kBufSize, 0);
    s_->outPos = 0;
    s_->outAvail = 0;
    s_->eof = false;
    s_->inEof = false;
    s_->strm = LZMA_STREAM_INIT;
    // No memory limit; accept concatenated streams (robustness).
    lzma_ret r = lzma_stream_decoder(&s_->strm, UINT64_MAX, LZMA_CONCATENATED);
    if (r != LZMA_OK) {
        if (err) *err = std::string("lzma_stream_decoder failed: ") + lzmaErrStr(r);
        return false;
    }
    s_->active = true;
    return true;
}

size_t XzDecoder::read(uint8_t* buf, size_t len)
{
    if (!s_->active) return 0;
    size_t total = 0;
    while (total < len) {
        // Deliver any buffered decoded bytes first.
        if (s_->outAvail > 0) {
            size_t n = std::min(s_->outAvail, len - total);
            std::memcpy(buf + total, s_->outBuf.data() + s_->outPos, n);
            s_->outPos += n;
            s_->outAvail -= n;
            total += n;
            continue;
        }
        if (s_->eof) break; // no more decoded bytes coming

        // Refill output by pulling compressed input and decoding.
        s_->outPos = 0;
        s_->outAvail = 0;
        s_->strm.next_out = s_->outBuf.data();
        s_->strm.avail_out = s_->outBuf.size();

        for (;;) {
            // Ensure there is compressed input available.
            if (s_->strm.avail_in == 0 && !s_->inEof) {
                size_t got = s_->source(s_->inBuf.data(), s_->inBuf.size());
                if (got == 0) {
                    s_->inEof = true;
                } else {
                    s_->strm.next_in = s_->inBuf.data();
                    s_->strm.avail_in = got;
                }
            }
            size_t before = s_->strm.avail_out;
            lzma_ret r = lzma_code(&s_->strm, s_->inEof ? LZMA_FINISH : LZMA_RUN);
            s_->outAvail = s_->outBuf.size() - s_->strm.avail_out;
            if (s_->outAvail > 0) {
                s_->outPos = 0;
                break; // have decoded bytes to deliver
            }
            if (r == LZMA_STREAM_END) {
                s_->eof = true;
                break;
            }
            if (r != LZMA_OK) {
                // Hard decode error; treat as eof so caller stops.
                s_->eof = true;
                break;
            }
            if (s_->strm.avail_out == before && s_->inEof) {
                // No progress and no more input: truncated stream.
                s_->eof = true;
                break;
            }
        }
    }
    return total;
}

void XzDecoder::close()
{
    if (!s_->active) return;
    lzma_end(&s_->strm);
    s_->active = false;
    s_->outAvail = 0;
    s_->eof = true;
}
