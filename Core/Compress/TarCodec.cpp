#include "TarCodec.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

constexpr size_t kBlock = 512;

// ustar header layout (512 bytes). Fields are named per POSIX 1003.1.
struct UstarHeader
{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];   // "ustar\0"
    char version[2]; // "00"
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};
static_assert(sizeof(UstarHeader) == kBlock, "ustar header must be 512 bytes");

void writeOctal(char* dst, size_t len, uint64_t value)
{
    // Field is len-1 octal digits + NUL.
    std::memset(dst, '0', len);
    char tmp[32];
    int n = std::snprintf(tmp, sizeof(tmp), "%llo", static_cast<unsigned long long>(value));
    if (n <= 0) { dst[len - 1] = '\0'; return; }
    int digits = n;
    if (digits > static_cast<int>(len - 1)) digits = static_cast<int>(len - 1);
    // Place digits right-aligned in field, pad leading with '0'.
    std::memset(dst, '0', len - 1);
    std::memcpy(dst + (len - 1 - digits), tmp, static_cast<size_t>(digits));
    dst[len - 1] = '\0';
}

void fillHeader(UstarHeader& h, const std::string& name, uint16_t mode,
                uint64_t size, char typeflag)
{
    std::memset(&h, 0, sizeof(h));
    // Name: fit into name[100] (caller guarantees <=100 here). For names that
    // don't fit, the writer emits a PAX 'x' header first and uses a truncated
    // name here per ustar rules.
    size_t n = std::min<size_t>(name.size(), 100);
    std::memcpy(h.name, name.data(), n);
    writeOctal(h.mode, sizeof(h.mode), mode & 07777);
    writeOctal(h.uid, sizeof(h.uid), 0);
    writeOctal(h.gid, sizeof(h.gid), 0);
    writeOctal(h.size, sizeof(h.size), size);
    writeOctal(h.mtime, sizeof(h.mtime), 0);
    h.typeflag = typeflag;
    std::memcpy(h.magic, "ustar", 6);   // "ustar\0"
    h.version[0] = '0';
    h.version[1] = '0';
    // Compute checksum with chksum field treated as spaces.
    std::memset(h.chksum, ' ', sizeof(h.chksum));
    uint32_t sum = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&h);
    for (size_t i = 0; i < sizeof(h); ++i) sum += p[i];
    // chksum: 6 octal digits, NUL, space.
    char cs[8];
    std::snprintf(cs, sizeof(cs), "%06o", static_cast<unsigned>(sum));
    std::memcpy(h.chksum, cs, 6);
    h.chksum[6] = '\0';
    h.chksum[7] = ' ';
}

// Build a PAX extended-header record: "LEN path=VALUE\n" where LEN includes
// its own digits. Returns the full record bytes.
std::string makePaxRecord(const std::string& key, const std::string& value)
{
    // body = key + "=" + value + "\n"
    std::string body = key + "=" + value + "\n";
    // Solve for total length L = digits(L) + 1 (space) + body.size().
    size_t len = 0;
    size_t approx = body.size() + 4; // start with 4-digit length estimate
    for (;;) {
        size_t digits = 0;
        for (size_t t = approx; t > 0; t /= 10) ++digits;
        if (digits == 0) digits = 1;
        size_t total = digits + 1 + body.size();
        if (total >= approx && total < approx + digits /* stable enough */) {
            len = total;
            break;
        }
        approx = total;
        if (approx > 100000000) break; // safety
    }
    // Recompute precisely: total = digits(total) + 1 + body.size().
    for (;;) {
        size_t t = len;
        size_t digits = 0;
        for (size_t v = t; v > 0; v /= 10) ++digits;
        if (digits == 0) digits = 1;
        size_t total = digits + 1 + body.size();
        if (total == t) { len = total; break; }
        len = total;
    }
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "%zu ", len);
    return std::string(prefix) + body;
}

} // namespace

// ---------------------------------------------------------------------------
// TarWriter
// ---------------------------------------------------------------------------
TarWriter::TarWriter() = default;
TarWriter::~TarWriter() { if (open_) { /* best-effort */ } }

bool TarWriter::open(const std::function<bool(const uint8_t*, size_t)>& sink)
{
    sink_ = sink;
    open_ = true;
    return true;
}

bool TarWriter::emitHeader(const std::string& name, uint16_t mode,
                           uint64_t size, char typeflag)
{
    UstarHeader h;
    fillHeader(h, name, mode, size, typeflag);
    return sink_(reinterpret_cast<const uint8_t*>(&h), sizeof(h));
}

bool TarWriter::emitPaxHeader(const std::string& name, uint16_t mode, uint64_t size)
{
    // PAX 'x' header carries the long path in a "path" record.
    std::string rec = makePaxRecord("path", name);
    uint64_t paxSize = rec.size();
    // Emit PAX extended header (typeflag 'x').
    UstarHeader ph;
    fillHeader(ph, "", mode, paxSize, 'x');
    if (!sink_(reinterpret_cast<const uint8_t*>(&ph), sizeof(ph))) return false;
    if (!sink_(reinterpret_cast<const uint8_t*>(rec.data()), rec.size())) return false;
    if (!padToBlock(paxSize)) return false;
    // Regular header with truncated name (path is taken from PAX record).
    std::string trunc = name.size() > 100 ? name.substr(0, 100) : name;
    return emitHeader(trunc, mode, size, '0');
}

bool TarWriter::padToBlock(uint64_t bytesAlready)
{
    uint64_t rem = (kBlock - (bytesAlready % kBlock)) % kBlock;
    if (rem == 0) return true;
    uint8_t zero[kBlock] = {0};
    return sink_(zero, static_cast<size_t>(rem));
}

bool TarWriter::addBuffer(const std::string& name, uint16_t mode,
                          const uint8_t* data, size_t len)
{
    if (!open_) return false;
    if (name.size() > 100) {
        if (!emitPaxHeader(name, mode, len)) return false;
    } else {
        if (!emitHeader(name, mode, len, '0')) return false;
    }
    if (len > 0 && !sink_(data, len)) return false;
    return padToBlock(len);
}

bool TarWriter::addStream(const std::string& name, uint16_t mode, uint64_t size,
                          const std::function<size_t(uint8_t*, size_t)>& source)
{
    if (!open_) return false;
    if (name.size() > 100) {
        if (!emitPaxHeader(name, mode, size)) return false;
    } else {
        if (!emitHeader(name, mode, size, '0')) return false;
    }
    uint8_t buf[kBlock];
    uint64_t remaining = size;
    while (remaining > 0) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, sizeof(buf)));
        size_t got = source(buf, want);
        if (got == 0) return false; // short stream
        if (!sink_(buf, got)) return false;
        remaining -= got;
    }
    return padToBlock(size);
}

bool TarWriter::close()
{
    if (!open_) return false;
    // Two zero blocks mark end of archive.
    uint8_t zero[kBlock * 2] = {0};
    bool ok = sink_(zero, sizeof(zero));
    open_ = false;
    return ok;
}

// ---------------------------------------------------------------------------
// TarReader
// ---------------------------------------------------------------------------
TarReader::TarReader() = default;
TarReader::~TarReader() { close(); }

bool TarReader::open(const std::function<size_t(uint8_t*, size_t)>& source)
{
    source_ = source;
    open_ = true;
    return true;
}

bool TarReader::readBlock(uint8_t* buf)
{
    size_t need = kBlock;
    size_t total = 0;
    while (total < need) {
        size_t got = source_(buf + total, need - total);
        if (got == 0) return false; // short block / eof
        total += got;
    }
    return true;
}

bool TarReader::nextEntry(std::string& outName, uint16_t& outMode,
                          uint64_t& outSize, bool& outIsPax, std::string& err)
{
    if (!open_) { err = "not open"; return false; }
    UstarHeader h;
    if (!readBlock(reinterpret_cast<uint8_t*>(&h))) {
        // End of archive without a zero block: treat as clean EOF.
        outName.clear();
        return false;
    }
    // Two consecutive zero blocks = EOF marker.
    bool allZero = true;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&h);
    for (size_t i = 0; i < sizeof(h); ++i) {
        if (p[i] != 0) { allZero = false; break; }
    }
    if (allZero) { outName.clear(); return false; }

    // Parse size (octal, possibly with embedded NUL).
    char sizeBuf[13];
    std::memcpy(sizeBuf, h.size, 12);
    sizeBuf[12] = '\0';
    uint64_t size = 0;
    if (std::sscanf(sizeBuf, "%llo", reinterpret_cast<unsigned long long*>(&size)) != 1) {
        err = "bad size field";
        return false;
    }
    // Parse mode (octal).
    char modeBuf[9];
    std::memcpy(modeBuf, h.mode, 8);
    modeBuf[8] = '\0';
    unsigned long long mode = 0;
    if (std::sscanf(modeBuf, "%llo", &mode) != 1) mode = 0;

    outMode = static_cast<uint16_t>(mode & 07777);
    outSize = size;
    outIsPax = (h.typeflag == 'x' || h.typeflag == 'X');

    if (outIsPax) {
        // Caller will read PAX payload and parse; name comes from the PAX
        // record, so leave outName empty here.
        outName.clear();
        return true;
    }

    // Regular entry: name = prefix + '/' + name when prefix present.
    std::string name;
    if (h.prefix[0] != '\0') {
        size_t plen = ::strnlen(h.prefix, sizeof(h.prefix));
        name.assign(h.prefix, plen);
        name.push_back('/');
    }
    size_t nlen = ::strnlen(h.name, sizeof(h.name));
    name.append(h.name, nlen);
    outName = name;
    return true;
}

bool TarReader::readData(uint8_t* out, size_t n)
{
    size_t total = 0;
    while (total < n) {
        size_t got = source_(out + total, n - total);
        if (got == 0) return false;
        total += got;
    }
    return true;
}

bool TarReader::skipPadding(uint64_t dataSize)
{
    uint64_t rem = (kBlock - (dataSize % kBlock)) % kBlock;
    if (rem == 0) return true;
    uint8_t buf[kBlock];
    while (rem > 0) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(rem, sizeof(buf)));
        size_t got = source_(buf, want);
        if (got == 0) return false;
        rem -= got;
    }
    return true;
}

bool TarReader::skipData(uint64_t n)
{
    // Skip data + padding to 512.
    uint64_t total = (n + (kBlock - 1)) / kBlock * kBlock;
    uint8_t buf[kBlock];
    while (total > 0) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(total, sizeof(buf)));
        size_t got = source_(buf, want);
        if (got == 0) return false;
        total -= got;
    }
    return true;
}

void TarReader::close()
{
    open_ = false;
    source_ = nullptr;
}
