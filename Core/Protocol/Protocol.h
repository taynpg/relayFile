#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CoreDefine.hpp"
#include "Utils/miniUtil.h"

enum class FrameType : int16_t {
    kMsgType_Ask_ID = defConsoleMessageStart,
    kMsgType_Answer_ID,
    kMsgType_Ask_Home,
    kMsgType_Answer_Home,
    kMsgType_Ask_FileList,
    kMsgType_Answer_FileList,
    kMsgType_Ask_ClientList,
    kMsgType_Answer_ClientList,
    kMsgType_Ask_FileMeta,
    kMsgType_Answer_FileMeta,
    kMsgType_Ask_Delete,
    kMsgType_Answer_Delete,
    kMsgType_Ask_Sha256,
    kMsgType_Answer_Sha256,
    kMsgType_Ask_Rename,
    kMsgType_Answer_Rename,
    kMsgType_Ask_CreateDir,
    kMsgType_Answer_CreateDir,
    kMsgType_Ask_Archive,
    kMsgType_Answer_Archive,
    kMsgType_Ask_UnArchive,
    kMsgType_Answer_UnArchive,
    kMsgType_Ask_HomeAndDriver,
    kMsgType_Answer_HomeAndDriver,
    kMsgType_Ask_Heart,
    kMsgType_Answer_Heart,
    kMsgType_Notify_ClientList,
    kMsgType_Answer_Notify_ClientList,
    kMsgType_Ask_FileSamples,
    kMsgType_Answer_FileSamples,
    kMsgType_Ask_ArchivePack,
    kMsgType_Answer_ArchivePack,
    kMsgType_Ask_ArchiveReady,
    kMsgType_Answer_ArchiveReady,

    kFileType_Request_Send = defFileMessageStart,
    kFileType_Answer_Send,
    kFileType_Request_Down,
    kFileType_Answer_Down,
    kFileType_Request_Complete,
    kFileType_Answer_Complete,
    kFileType_Request_Cancel,
    kFileType_Answer_Cancel,
    kFileType_Request_Start,
    kFileType_Answer_Start,

    kFileType_Request_ID = defServerDirectFileStart,
    kFileType_Answer_ID,

    kFileType_Request_Ack = defDirectChuckAck,
    kFileType_Answer_Ack,
    kFileType_Request_Chuck,
    kFileType_Answer_Chuck,
};

// 帧标志位。
// 原来这些都是复用 `mark` 一个 int16 的不同位/不同取值，一旦新增用途就会串味
// （例如 mark 带上加密位以后，按 mark != 0 判方向就会取错会话槽）。现在显式拆开。
enum FrameFlags : std::uint16_t {
    kFrameFlag_None = 0,
    // 该帧由「接收文件数据的一侧」发出：0=发送侧产生，1=接收侧产生。
    // FileSession 用它决定去 transferMap_ 的 "S"（发送侧）还是 "R"（接收侧）槽找会话。
    kFrameFlag_FromReceiver = 1u << 0,
    // data 已加密，格式：nonce(12B) || ciphertext || tag(16B)
    kFrameFlag_Encrypted = 1u << 1,
    // 帧尾附带 body CRC32（小 body 才带，大块数据靠 TCP + 分块校验 + 结束时的长度校验）
    kFrameFlag_BodyCrc = 1u << 2,
};

// 线上格式（全部小端）：
//
//   offset size  field
//   0      4     magic        'R','F','M','1'
//   4      2     version
//   6      2     flags        FrameFlags
//   8      2     type
//   10     2     reserved
//   12     8     sessionId
//   20     8     index
//   28     4     frameLen     整帧字节数（含 36 字节固定头）
//   32     4     headerCrc    crc32(前 32 字节)
//   36     ...   payload:
//                  u8 fromLen, from[fromLen]
//                  u8 toLen,   to[toLen]
//                  u8 uuidLen, uuid[uuidLen]
//                  body[...]              // 长度 = frameLen - 36 - 3 - 三个长度 - (bodyCrc?4:0)
//                  u32 bodyCrc             // 仅当 flags & kFrameFlag_BodyCrc
//
// 与旧格式的区别：没有帧尾标记、没有定长字符串、没有盲扫；帧长写在头部并且受
// kMaxFrameSize 约束，头部带 CRC，因此"假帧头 / 半截帧 / 错位"都能立刻识别。
namespace FrameProto {
constexpr std::uint32_t kMagic = 0x314D4652;   // 小端字节序列 R F M 1
constexpr std::uint16_t kVersion = 2;
constexpr std::size_t kFixedHeaderSize = 36;
constexpr std::uint32_t kMaxFrameSize = 32u * 1024 * 1024;   // 单帧上限，超过即协议错误
constexpr std::size_t kMaxRouteField = 255;                  // from/to/fuuid 单字段上限（u8 长度）
constexpr std::uint32_t kBodyCrcLimit = 64u * 1024;          // 小于该值的 body 附带 CRC32
}   // namespace FrameProto

struct OneFrame {
    FrameType type{};
    std::uint16_t flags{kFrameFlag_None};
    std::int64_t sessionId{};
    std::int64_t index{};
    std::string from;
    std::string to;
    std::string fuuid;
    std::vector<char> data;

    bool isFromReceiver() const { return (flags & kFrameFlag_FromReceiver) != 0; }
    void setFromReceiver(bool on)
    {
        if (on) {
            flags |= kFrameFlag_FromReceiver;
        } else {
            flags &= static_cast<std::uint16_t>(~kFrameFlag_FromReceiver);
        }
    }
    bool isEncrypted() const { return (flags & kFrameFlag_Encrypted) != 0; }

    static std::shared_ptr<OneFrame> Create();
    static void ExChangeIp(std::shared_ptr<OneFrame> frame);
    static std::shared_ptr<OneFrame> Create(std::shared_ptr<OneFrame> frame, bool isChangeIp = true, bool isCopyData = false);
};
using FramePtr = std::shared_ptr<OneFrame>;

inline bool GIsChuckAckFrame(FramePtr frame)
{
    if (static_cast<std::uint16_t>(frame->type) >= defDirectChuckAck) {
        return true;
    }
    return false;
}

inline bool GIsFileMessageFrame(FramePtr frame)
{
    if (static_cast<std::uint16_t>(frame->type) >= defFileMessageStart &&
        static_cast<std::uint16_t>(frame->type) < defServerDirectFileStart) {
        return true;
    }
    return false;
}

inline bool GIsControlMessageFrame(FramePtr frame)
{
    if (static_cast<std::uint16_t>(frame->type) < defFileMessageStart) {
        return true;
    }
    return false;
}

inline bool GIsMessageFrame(FramePtr frame)
{
    if (static_cast<std::uint16_t>(frame->type) < defServerDirectFileStart) {
        return true;
    }
    return false;
}

enum class UnPackResult {
    NeedMoreData,   // 数据还不够，等下一次 readyRead
    Ok,             // 成功解出一帧（frame 非空）
    ProtocolError,  // 流已无法恢复（版本不符等），调用方应断开连接
};

class Protocol
{
public:
    Protocol() = default;
    ~Protocol() = default;

public:
    // 从 buffer 头部解出一帧；成功时消费掉对应字节。
    // 前导垃圾会被丢弃；帧头 CRC 不匹配时按"假 magic"跳过 1 字节重新同步，
    // 不会出现"一个巨大的正长度把解析器永久挂住"的情况。
    static UnPackResult UnPack(miniBuffer& buffer, FramePtr& frame);

    // 序列化。路由字段超过 kMaxRouteField 或帧超过 kMaxFrameSize 时返回空 vector。
    static std::vector<char> Pack(const FramePtr& frame);

    static std::uint32_t Crc32(const void* data, std::size_t len);
};
