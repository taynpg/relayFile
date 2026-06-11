#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CoreDefine.hpp"
#include "Utils/miniUtil.h"

enum class FrameType : int16_t {
    kMsgType_Ask_ID = 0,
    kMsgType_Answer_ID,
    kMsgType_Ask_Home,
    kMsgType_Answer_Home,
    kMsgType_Ask_FileList,
    kMsgType_Answer_FileList,
    kMsgType_Ask_ClientList,
    kMsgType_Answer_ClientList,

    kFileType_Request_Send = defFileStartNum,
    kFileType_Answer_Send,
    kFileType_Request_Down,
    kFileType_Answer_Down,
    kFileType_Request_Complete,
    kFileType_Answer_Complete,
    kFileType_Request_Cancel,
    kFileType_Answer_Cancel,
    kFileType_Request_Start,
    kFileType_Answer_Start,

    kFileType_Request_Ack = defDirectTranStartNum,
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

inline bool GIsTurnFrame(FramePtr frame)
{
    if (static_cast<std::uint16_t>(frame->type) >= defFileStartNum &&
        static_cast<std::uint16_t>(frame->type) < defDirectTranStartNum) {
        return true;
    }
    return false;
}
inline bool GIsMsgFrame(FramePtr frame)
{
    if (static_cast<std::uint16_t>(frame->type) < defFileStartNum) {
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
