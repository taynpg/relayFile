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
    Message& operator=(const Message& o);
    Message(const Message& o);

    int32_t mark{};
    std::string comStr;
    std::string errMsg;
    std::string transId;
    std::string uuid;
    ClientInfo from;
    ClientInfo to;
    MessageStateCode msgStateCode{};

    FileMeta ff;
    FileMeta ft;
    std::vector<ClientInfo> clientList;
    std::vector<std::string> strVec;
    std::unordered_map<std::string, std::vector<FileMeta>> mapData;
    std::vector<SampleBlock> samples;

    template <class Archive> void serialize(Archive& ar)
    {
        ar(mark, comStr, errMsg, transId, uuid, from, to, msgStateCode, ff, ft, clientList, strVec, mapData, samples);
    }

    static std::shared_ptr<Message> Create();
};
using MessagePtr = std::shared_ptr<Message>;
