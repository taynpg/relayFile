#include "Protocol.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

inline std::uint16_t ReadU16(const char* p)
{
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[0])) |
           (static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[1])) << 8);
}

inline std::uint32_t ReadU32(const char* p)
{
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[3])) << 24);
}

inline std::uint64_t ReadU64(const char* p)
{
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | static_cast<std::uint8_t>(p[i]);
    }
    return v;
}

inline void WriteU16(std::vector<char>& out, std::uint16_t v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

inline void WriteU32(std::vector<char>& out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

inline void WriteU64(std::vector<char>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

// 用 uint32 头 + u8 长度写路由字段；超长直接判失败（不静默截断）
bool WriteRouteField(std::vector<char>& out, const std::string& s)
{
    if (s.size() > FrameProto::kMaxRouteField) {
        return false;
    }
    out.push_back(static_cast<char>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
    return true;
}

bool ReadRouteField(const char* p, std::size_t avail, std::string& out, std::size_t& consumed)
{
    if (avail < 1) {
        return false;
    }
    const auto len = static_cast<std::size_t>(static_cast<std::uint8_t>(p[0]));
    if (avail < 1 + len) {
        return false;
    }
    out.assign(p + 1, len);
    consumed = 1 + len;
    return true;
}

}   // namespace

std::uint32_t Protocol::Crc32(const void* data, std::size_t len)
{
    static const auto table = []() {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    std::uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::shared_ptr<OneFrame> OneFrame::Create()
{
    return std::make_shared<OneFrame>();
}

void OneFrame::ExChangeIp(std::shared_ptr<OneFrame> frame)
{
    std::swap(frame->from, frame->to);
}

std::shared_ptr<OneFrame> OneFrame::Create(std::shared_ptr<OneFrame> frame, bool isChangeIp, bool isCopyData)
{
    auto f = OneFrame::Create();
    f->type = frame->type;
    f->flags = frame->flags;
    f->sessionId = frame->sessionId;
    f->index = frame->index;
    f->from = frame->from;
    f->to = frame->to;
    f->fuuid = frame->fuuid;

    if (isChangeIp) {
        OneFrame::ExChangeIp(f);
    }

    if (isCopyData) {
        f->data = frame->data;
    }

    return f;
}

std::vector<char> Protocol::Pack(const FramePtr& frame)
{
    if (!frame) {
        return {};
    }

    const bool withBodyCrc = frame->data.size() <= FrameProto::kBodyCrcLimit;
    const std::uint16_t flags = withBodyCrc
                                    ? static_cast<std::uint16_t>(frame->flags | kFrameFlag_BodyCrc)
                                    : static_cast<std::uint16_t>(frame->flags & ~kFrameFlag_BodyCrc);

    std::vector<char> out;
    out.reserve(FrameProto::kFixedHeaderSize + 3 + frame->from.size() + frame->to.size() + frame->fuuid.size() +
                frame->data.size() + 4);

    WriteU32(out, FrameProto::kMagic);
    WriteU16(out, FrameProto::kVersion);
    WriteU16(out, flags);
    WriteU16(out, static_cast<std::uint16_t>(frame->type));
    WriteU16(out, 0);   // reserved
    WriteU64(out, static_cast<std::uint64_t>(frame->sessionId));
    WriteU64(out, static_cast<std::uint64_t>(frame->index));
    WriteU32(out, 0);   // frameLen，先占位
    WriteU32(out, 0);   // headerCrc，先占位

    if (!WriteRouteField(out, frame->from) || !WriteRouteField(out, frame->to) ||
        !WriteRouteField(out, frame->fuuid)) {
        return {};
    }
    out.insert(out.end(), frame->data.begin(), frame->data.end());
    if (withBodyCrc) {
        WriteU32(out, Crc32(frame->data.data(), frame->data.size()));
    }

    const auto frameLen = static_cast<std::uint32_t>(out.size());
    if (frameLen > FrameProto::kMaxFrameSize) {
        return {};
    }
    // 回填 frameLen（offset 28）与 headerCrc（offset 32）
    for (int i = 0; i < 4; ++i) {
        out[28 + i] = static_cast<char>((frameLen >> (8 * i)) & 0xFF);
    }
    const std::uint32_t headerCrc = Crc32(out.data(), 32);
    for (int i = 0; i < 4; ++i) {
        out[32 + i] = static_cast<char>((headerCrc >> (8 * i)) & 0xFF);
    }
    return out;
}

UnPackResult Protocol::UnPack(miniBuffer& buffer, FramePtr& frame)
{
    for (;;) {
        const auto& data = buffer.GetBuffer();
        const std::size_t avail = data.size();

        // 1) 找 magic：没有就丢弃前导垃圾（保留末尾可能是半个 magic 的字节）
        std::size_t offset = 0;
        while (offset + 4 <= avail && ReadU32(data.data() + offset) != FrameProto::kMagic) {
            ++offset;
        }
        if (offset > 0) {
            if (offset + 4 > avail) {
                const std::size_t keep = std::min<std::size_t>(avail, 3);
                if (avail > keep) {
                    buffer.RemoveOf(0, static_cast<int>(avail - keep));
                }
                return UnPackResult::NeedMoreData;
            }
            buffer.RemoveOf(0, static_cast<int>(offset));
            continue;   // 重新取 data 再扫
        }

        if (avail < FrameProto::kFixedHeaderSize) {
            return UnPackResult::NeedMoreData;
        }

        const char* h = data.data();
        const std::uint32_t frameLen = ReadU32(h + 28);
        const std::uint32_t headerCrc = ReadU32(h + 32);

        // 2) 头部 sanity + CRC：不匹配说明是数据体里偶然出现的 magic，跳过 1 字节继续找
        if (frameLen < FrameProto::kFixedHeaderSize + 3 || frameLen > FrameProto::kMaxFrameSize ||
            Crc32(h, 32) != headerCrc) {
            buffer.RemoveOf(0, 1);
            continue;
        }
        const std::uint16_t version = ReadU16(h + 4);
        if (version != FrameProto::kVersion) {
            // 头部 CRC 都对了却版本不符，是真正的协议不匹配，继续等只会永远等下去
            return UnPackResult::ProtocolError;
        }
        // 3) 帧还没收全
        if (avail < frameLen) {
            return UnPackResult::NeedMoreData;
        }

        const std::uint16_t flags = ReadU16(h + 6);
        const bool withBodyCrc = (flags & kFrameFlag_BodyCrc) != 0;
        const std::size_t payloadLen = frameLen - FrameProto::kFixedHeaderSize;
        if (payloadLen < 3 + (withBodyCrc ? 4 : 0)) {
            buffer.RemoveOf(0, 1);
            continue;
        }

        std::size_t p = FrameProto::kFixedHeaderSize;
        const std::size_t payloadEnd = frameLen;
        std::string from;
        std::string to;
        std::string uuid;
        std::size_t used = 0;
        if (!ReadRouteField(h + p, payloadEnd - p, from, used)) {
            buffer.RemoveOf(0, 1);
            continue;
        }
        p += used;
        if (!ReadRouteField(h + p, payloadEnd - p, to, used)) {
            buffer.RemoveOf(0, 1);
            continue;
        }
        p += used;
        if (!ReadRouteField(h + p, payloadEnd - p, uuid, used)) {
            buffer.RemoveOf(0, 1);
            continue;
        }
        p += used;

        const std::size_t bodyEnd = payloadEnd - (withBodyCrc ? 4 : 0);
        if (p > bodyEnd) {
            buffer.RemoveOf(0, 1);
            continue;
        }
        const std::size_t bodyLen = bodyEnd - p;
        if (withBodyCrc) {
            const std::uint32_t expect = ReadU32(h + bodyEnd);
            if (Crc32(h + p, bodyLen) != expect) {
                // body 损坏：整帧丢弃（TCP 已经保证不丢字节，这里只可能是实现 bug 或内存问题）
                buffer.RemoveOf(0, static_cast<int>(frameLen));
                return UnPackResult::ProtocolError;
            }
        }

        auto f = std::make_shared<OneFrame>();
        f->type = static_cast<FrameType>(ReadU16(h + 8));
        f->flags = flags;
        f->sessionId = static_cast<std::int64_t>(ReadU64(h + 12));
        f->index = static_cast<std::int64_t>(ReadU64(h + 20));
        f->from = std::move(from);
        f->to = std::move(to);
        f->fuuid = std::move(uuid);
        if (bodyLen > 0) {
            f->data.assign(h + p, h + p + bodyLen);
        }

        buffer.RemoveOf(0, static_cast<int>(frameLen));
        frame = std::move(f);
        return UnPackResult::Ok;
    }
}
