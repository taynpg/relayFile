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
    constexpr size_t HEADER_SIZE = 2 + 2 + 2 + 8 + 8 + 32 + 32 + 4;

    const auto& data = buffer.GetBuffer();
    if (data.size() < HEADER_SIZE) {
        return nullptr;
    }

    auto it = std::search(data.begin(), data.end(), std::begin(HEADER), std::end(HEADER));
    if (it == data.end()) {
        return nullptr;
    }

    size_t offset = std::distance(data.begin(), it);

    if (offset + HEADER_SIZE > data.size()) {
        return nullptr;
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
    std::memcpy(&len, data.data() + offset + 6 + 8 + 8 + 32 + 32, sizeof(len));

    if (len < 0 || offset + HEADER_SIZE + static_cast<size_t>(len) > data.size()) {
        return nullptr;
    }

    size_t tailPos = offset + HEADER_SIZE + len;
    if (std::memcmp(data.data() + tailPos, TAIL, 2) != 0) {
        return nullptr;
    }

    auto frame = std::make_shared<OneFrame>();
    frame->type = static_cast<FrameType>(type);
    frame->mark = mark;
    frame->sessionId = sessionId;
    frame->index = index;
    frame->from.assign(data.data() + offset + 6 + 8 + 8, 32);
    frame->to.assign(data.data() + offset + 6 + 8 + 8 + 32, 32);
    frame->from.erase(frame->from.find_last_not_of('\0') + 1);
    frame->to.erase(frame->to.find_last_not_of('\0') + 1);

    if (len > 0) {
        frame->data.resize(len);
        std::memcpy(frame->data.data(), data.data() + offset + HEADER_SIZE, len);
    }

    buffer.RemoveOf(0, static_cast<int>(offset + HEADER_SIZE + len + 2));

    return frame;
}

std::vector<char> Protocol::Pack(const std::shared_ptr<OneFrame>& frame)
{
    if (!frame) {
        return {};
    }

    constexpr char HEADER[] = {'\xFF', '\xFE'};
    constexpr char TAIL[] = {'\xFF', '\xFF'};
    constexpr size_t HEADER_SIZE = 2 + 2 + 2 + 8 + 8 + 32 + 32 + 4;   // 90

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

    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&len), reinterpret_cast<const char*>(&len) + sizeof(len));

    if (len > 0) {
        buffer.insert(buffer.end(), frame->data.begin(), frame->data.end());
    }

    buffer.insert(buffer.end(), std::begin(TAIL), std::end(TAIL));
    return buffer;
}
