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

enum class MessageType {
    kMessageAskId = 1,
    kMessageAnswerId,
    kMessageAskHome,
    kMessageAnswerHome,
    kMessageAskClientList,
    kMessageAnswerClientList,
    kMessageAskFileList,
    kMessageAnswerFileList,
    kMessageFileRequestDown,
    kMessageFileAnswerRequestDown,
    kMessageFileRequestSend,
    kMessageFileAnswerRequestSend,
    kMessageFileRequestConnect,
    kMessageFileAnswerRequestConnect,
    kMessageFileRequestDisconnect,
    kMessageFileAnswerRequestDisconnect,
    kMessageFileRequestComplete,
    kMessageFileAnswerRequestComplete,
    kMessageFileRequestCancel,
    kMessageFileAnswerRequestCancel,
    kMessageFileRequestStart,
    kMessageFileAnswerRequestStart,
};

struct Message {

    Message() = default;
    Message& operator=(const Message& o);
    Message(const Message& o);

    MessageType msType{};
    std::string msData;
    std::string errData;
    std::string transId;
    ClientInfo from;
    ClientInfo to;
    MessageStateCode msgStateCode{};

    FileMeta ff;
    FileMeta ft;
    std::vector<ClientInfo> clientList;
    std::unordered_map<std::string, std::vector<FileMeta>> mapData;

    template <class Archive> void serialize(Archive& ar)
    {
        ar(msType, msData, errData, transId, from, to, msgStateCode, ff, ft, mapData);
    }

    static std::shared_ptr<Message> Create();
};
using MessagePtr = std::shared_ptr<Message>;
