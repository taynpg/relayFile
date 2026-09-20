#include "Protocol.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

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
    f->mark = frame->mark;
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

std::shared_ptr<OneFrame> Protocol::UnPack(miniBuffer& buffer)
{
    constexpr char HEADER[] = {'\xFF', '\xFE'};
    constexpr char TAIL[] = {'\xFF', '\xFF'};
    constexpr size_t HEADER_SIZE = 2 + 2 + 2 + 8 + 8 + 32 + 32 + 36 + 4;

    const auto& data = buffer.GetBuffer();

    // 顺序解析：从 buffer 头部开始找帧头，避免在数据体内部盲搜导致假帧头卡死。
    size_t offset = 0;
    while (offset + HEADER_SIZE <= data.size()) {
        // 1) 校验帧头
        if (data[offset] != HEADER[0] || data[offset + 1] != HEADER[1]) {
            // 前导垃圾字节，跳过 1 字节继续找
            ++offset;
            continue;
        }

        int16_t type{};
        int16_t mark{};
        int64_t sessionId{};
        int64_t index{};
        int32_t len{};

        std::memcpy(&type, data.data() + offset + 2, sizeof(type));
        std::memcpy(&mark, data.data() + offset + 4, sizeof(mark));
        std::memcpy(&sessionId, data.data() + offset + 6, sizeof(sessionId));
        std::memcpy(&index, data.data() + offset + 6 + 8, sizeof(index));
        std::memcpy(&len, data.data() + offset + 6 + 8 + 8 + 32 + 32 + 36, sizeof(len));

        // 2) 长度非法（数据体里误匹配到的假帧头常出现巨大 len）→ 跳过该帧头继续找
        if (len < 0) {
            offset += 2;
            continue;
        }

        // 3) 帧体还没收全，等待更多数据（不消费 buffer）
        if (offset + HEADER_SIZE + static_cast<size_t>(len) + 2 > data.size()) {
            return nullptr;
        }

        // 4) 校验帧尾；不匹配说明这是数据体里的假帧头，跳过继续找
        size_t tailPos = offset + HEADER_SIZE + static_cast<size_t>(len);
        if (data[tailPos] != TAIL[0] || data[tailPos + 1] != TAIL[1]) {
            offset += 2;
            continue;
        }

        // 5) 解析成功，消费 buffer
        auto frame = std::make_shared<OneFrame>();
        frame->type = static_cast<FrameType>(type);
        frame->mark = mark;
        frame->sessionId = sessionId;
        frame->index = index;
        frame->from.assign(data.data() + offset + 6 + 8 + 8, 32);
        frame->to.assign(data.data() + offset + 6 + 8 + 8 + 32, 32);
        frame->fuuid.assign(data.data() + offset + 6 + 8 + 8 + 32 + 32, 36);
        frame->from.erase(frame->from.find_last_not_of('\0') + 1);
        frame->to.erase(frame->to.find_last_not_of('\0') + 1);
        frame->fuuid.erase(frame->fuuid.find_last_not_of('\0') + 1);

        if (len > 0) {
            frame->data.resize(len);
            std::memcpy(frame->data.data(), data.data() + offset + HEADER_SIZE, len);
        }

        buffer.RemoveOf(0, static_cast<int>(offset + HEADER_SIZE + static_cast<size_t>(len) + 2));

        return frame;
    }

    return nullptr;
}

std::vector<char> Protocol::Pack(const std::shared_ptr<OneFrame>& frame)
{
    if (!frame) {
        return {};
    }

    constexpr char HEADER[] = {'\xFF', '\xFE'};
    constexpr char TAIL[] = {'\xFF', '\xFF'};
    constexpr size_t HEADER_SIZE = 2 + 2 + 2 + 8 + 8 + 32 + 32 + 36 + 4;   // 90

    int32_t len = static_cast<int32_t>(frame->data.size());

    std::vector<char> buffer;
    buffer.reserve(HEADER_SIZE + len);

    int16_t type = static_cast<int16_t>(frame->type);
    buffer.insert(buffer.end(), std::begin(HEADER), std::end(HEADER));
    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&type), reinterpret_cast<const char*>(&type) + sizeof(type));

    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&frame->mark),
                  reinterpret_cast<const char*>(&frame->mark) + sizeof(frame->mark));

    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&frame->sessionId),
                  reinterpret_cast<const char*>(&frame->sessionId) + sizeof(frame->sessionId));
    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&frame->index),
                  reinterpret_cast<const char*>(&frame->index) + sizeof(frame->index));

    for (int i = 0; i < 32; ++i) {
        buffer.push_back(i < static_cast<int>(frame->from.size()) ? frame->from[i] : '\0');
    }
    for (int i = 0; i < 32; ++i) {
        buffer.push_back(i < static_cast<int>(frame->to.size()) ? frame->to[i] : '\0');
    }
    for (int i = 0; i < 36; ++i) {
        buffer.push_back(i < static_cast<int>(frame->fuuid.size()) ? frame->fuuid[i] : '\0');
    }

    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&len), reinterpret_cast<const char*>(&len) + sizeof(len));

    if (len > 0) {
        buffer.insert(buffer.end(), frame->data.begin(), frame->data.end());
    }

    buffer.insert(buffer.end(), std::begin(TAIL), std::end(TAIL));
    return buffer;
}
