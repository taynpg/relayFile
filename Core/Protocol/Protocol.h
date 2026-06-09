#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Utils/miniUtil.h"


enum class FrameType : int16_t {
    FrameMessage = 0,
    FrameFileChuck,
    FrameFileAccept,
    FrameFileAck,
    FrameFileFinish,
    FrameFileInterrupt
};

struct OneFrame {
    FrameType type{};
    int16_t mark{};
    int64_t sessionId{};
    int64_t index{};
    std::string from;
    std::string to;
    std::vector<char> data;
    static std::shared_ptr<OneFrame> Create();
    static void ExChangeIp(std::shared_ptr<OneFrame> frame);
    static std::shared_ptr<OneFrame> Create(std::shared_ptr<OneFrame> frame, bool isChangeIp = true, bool isCopyData = false);
};

using FramePtr = std::shared_ptr<OneFrame>;

class Protocol
{
public:
    Protocol() = default;
    ~Protocol() = default;

public:
    static std::shared_ptr<OneFrame> UnPack(miniBuffer& buffer);
    static std::vector<char> Pack(const std::shared_ptr<OneFrame>& frame);
};
