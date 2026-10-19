#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "FileMeta.h"

enum class MessageStateCode : std::uint16_t {
    kMessageStateCodeSuccess = 0,
    kMessageStateCodeFailed = 1,
};

// 消息语义标志。原来这些都挤在 `int32_t mark` 里：1=普通、2=归档，
// 还被 pushTask 临时改成 0 当作"要不要上报进度"的开关，读代码时基本靠猜。
enum class MessageFlag : std::uint32_t {
    kNone = 0,
    kRecursiveList = 1u << 0,   // 列目录时递归
    kArchive = 1u << 1,         // 本次传输是一个自描述 tar.xz 归档包
};

// 内容粗判采样块：文件内偏移 + 该偏移处的少量字节
struct SampleBlock {
    std::uint64_t offset{};
    std::vector<char> data;
    template <class Archive> void serialize(Archive& ar)
    {
        ar(offset, data);
    }
};

struct ClientInfo {
    std::string clientId;
    std::string clientName;
    std::string uuid;
    template <class Archive> void serialize(Archive& ar)
    {
        ar(clientId, clientName, uuid);
    }
};

struct Message {

    Message() = default;
    Message(const Message&) = default;
    Message& operator=(const Message&) = default;
    Message(Message&&) = default;
    Message& operator=(Message&&) = default;

    std::uint32_t flags{};
    std::string comStr;
    std::string errMsg;
    std::string transId;
    std::string uuid;
    ClientInfo from;
    ClientInfo to;
    MessageStateCode msgStateCode{};

    // 传输参数协商（由接收方决定，发送方照做）：0 表示未协商，用默认值
    std::uint32_t blockSize{};
    std::uint32_t windowBytes{};

    // 断点续传：
    //   resumeOffset      —— 接收方已连续落盘的字节数（已按块对齐），发送方从这里开始发；
    //                        接收方在 Answer_Send / Request_Start 里填写。
    //   resumeFingerprint —— 源文件快速指纹（size + 首 64KB 的 SHA-256），发送方填写；
    //                        接收方用它确认本地 .relaypart 对应的确实是同一个源文件，
    //                        指纹不符就丢弃残片从 0 重传，绝不把两个文件拼在一起。
    std::uint64_t resumeOffset{};
    std::string resumeFingerprint;

    FileMeta ff;
    FileMeta ft;
    std::vector<ClientInfo> clientList;
    std::vector<std::string> strVec;
    std::unordered_map<std::string, std::vector<FileMeta>> mapData;
    std::vector<SampleBlock> samples;
    // 批量采样查询的回包：路径 → 该路径的采样块（单条查询仍走 samples）
    std::unordered_map<std::string, std::vector<SampleBlock>> sampleMap;

    bool hasFlag(MessageFlag f) const { return (flags & static_cast<std::uint32_t>(f)) != 0; }
    void setFlag(MessageFlag f, bool on = true)
    {
        if (on) {
            flags |= static_cast<std::uint32_t>(f);
        } else {
            flags &= ~static_cast<std::uint32_t>(f);
        }
    }

    template <class Archive> void serialize(Archive& ar)
    {
        ar(flags, comStr, errMsg, transId, uuid, from, to, msgStateCode, blockSize, windowBytes, resumeOffset,
           resumeFingerprint, ff, ft, clientList, strVec, mapData, samples, sampleMap);
    }

    static std::shared_ptr<Message> Create();
};
using MessagePtr = std::shared_ptr<Message>;
