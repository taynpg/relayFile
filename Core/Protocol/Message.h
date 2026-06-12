#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "FileMeta.h"

enum class MessageStateCode : std::uint16_t {
    kMessageStateCodeSuccess = 0,
    kMessageStateCodeFailed = 1,
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
    std::unordered_map<std::string, std::vector<FileMeta>> mapData;

    template <class Archive> void serialize(Archive& ar)
    {
        ar(mark, comStr, errMsg, transId, uuid, from, to, msgStateCode, ff, ft, clientList, mapData);
    }

    static std::shared_ptr<Message> Create();
};
using MessagePtr = std::shared_ptr<Message>;
