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

struct OneFrame {
    FrameType type{};
    int16_t mark{};
    int64_t sessionId{};
    int64_t index{};
    std::string from;
    std::string to;
    std::string fuuid;
    std::vector<char> data;
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

class Protocol
{
public:
    Protocol() = default;
    ~Protocol() = default;

public:
    static std::shared_ptr<OneFrame> UnPack(miniBuffer& buffer);
    static std::vector<char> Pack(const std::shared_ptr<OneFrame>& frame);
};
