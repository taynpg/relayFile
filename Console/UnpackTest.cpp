// 协议解包健壮性对比测试：原 UnPack vs 新 UnPack
// 验证数据体中含 0xFFFE 时是否会解析失败/卡死
#include <File/LocalHandle.h>
#include <Protocol/Protocol.h>
#include <Utils/miniUtil.h>
#include <QCoreApplication>
#include <QDir>
#include <cstring>
#include <iostream>
#include <vector>

static std::shared_ptr<OneFrame> makeFrame(FrameType type, const std::vector<char>& data)
{
    auto f = OneFrame::Create();
    f->type = type;
    f->mark = 0;
    f->from = "sender";
    f->to = "receiver";
    f->fuuid = "uuid-1";
    f->data = data;
    return f;
}

// 原版 UnPack 逻辑（从 git 历史还原）
static std::shared_ptr<OneFrame> unpackOld(miniBuffer& buffer)
{
    constexpr char HEADER[] = {'\xFF', '\xFE'};
    constexpr char TAIL[] = {'\xFF', '\xFF'};
    constexpr size_t HEADER_SIZE = 2 + 2 + 2 + 8 + 8 + 32 + 32 + 36 + 4;

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
    std::memcpy(&len, data.data() + offset + 6 + 8 + 8 + 32 + 32 + 36, sizeof(len));
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
    frame->fuuid.assign(data.data() + offset + 6 + 8 + 8 + 32 + 32, 36);
    frame->from.erase(frame->from.find_last_not_of('\0') + 1);
    frame->to.erase(frame->to.find_last_not_of('\0') + 1);
    frame->fuuid.erase(frame->fuuid.find_last_not_of('\0') + 1);
    if (len > 0) {
        frame->data.resize(len);
        std::memcpy(frame->data.data(), data.data() + offset + HEADER_SIZE, len);
    }
    buffer.RemoveOf(0, static_cast<int>(offset + HEADER_SIZE + len + 2));
    return frame;
}

static int runCase(const char* name, const std::vector<char>& payload, bool expectSuccess)
{
    auto frame = makeFrame(FrameType::kFileType_Request_Chuck, payload);
    auto packed = Protocol::Pack(frame);

    // 原 UnPack
    miniBuffer bufOld;
    bufOld.Append(packed.data(), packed.size());
    auto fOld = unpackOld(bufOld);
    bool oldOk = (fOld != nullptr) && (fOld->data == payload);

    // 新 UnPack
    miniBuffer bufNew;
    bufNew.Append(packed.data(), packed.size());
    auto fNew = Protocol::UnPack(bufNew);
    bool newOk = (fNew != nullptr) && (fNew->data == payload);

    std::cout << "[" << name << "] payload_size=" << payload.size()
              << "  old=" << (oldOk ? "OK" : "FAIL")
              << "  new=" << (newOk ? "OK" : "FAIL") << "\n";
    return (oldOk == expectSuccess && newOk == expectSuccess) ? 0 : 1;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    int fails = 0;

    // 1. 普通数据
    {
        std::vector<char> p(1000, 'A');
        fails += runCase("plain_1k", p, true);
    }

    // 2. 数据体含 0xFFFE（UTF-16 BOM）
    {
        std::vector<char> p(1000, 'B');
        p[0] = '\xFF'; p[1] = '\xFE';
        fails += runCase("bom_at_start", p, true);
    }

    // 3. 数据体中间含多处 0xFFFE
    {
        std::vector<char> p(4096, 'C');
        for (size_t i = 100; i < p.size(); i += 500) {
            p[i] = '\xFF'; p[i + 1] = '\xFE';
        }
        fails += runCase("fffe_multi", p, true);
    }

    // 4. 数据体末尾是 0xFF 0xFE（紧邻尾标前）
    {
        std::vector<char> p(500, 'D');
        p[498] = '\xFF'; p[499] = '\xFE';
        fails += runCase("fffe_at_tail", p, true);
    }

    // 5. 256KB 大块，含多处 0xFFFE
    {
        std::vector<char> p(256 * 1024, 'E');
        for (size_t i = 0; i < p.size(); i += 4096) {
            p[i] = '\xFF'; p[i + 1] = '\xFE';
        }
        fails += runCase("block_256k_fffe", p, true);
    }

    // 6. 两帧粘包：帧1 data 含 0xFFFE，帧2 紧跟
    {
        auto f1 = makeFrame(FrameType::kFileType_Request_Chuck, std::vector<char>(2000, 'F'));
        f1->data[0] = '\xFF'; f1->data[1] = '\xFE';
        auto f2 = makeFrame(FrameType::kFileType_Request_Chuck, std::vector<char>(3000, 'G'));
        auto p1 = Protocol::Pack(f1);
        auto p2 = Protocol::Pack(f2);
        std::vector<char> combined = p1;
        combined.insert(combined.end(), p2.begin(), p2.end());

        miniBuffer bufOld;
        bufOld.Append(combined.data(), combined.size());
        auto o1 = unpackOld(bufOld);
        auto o2 = unpackOld(bufOld);
        bool oldOk = o1 && o2 && o1->data == f1->data && o2->data == f2->data;

        miniBuffer bufNew;
        bufNew.Append(combined.data(), combined.size());
        auto n1 = Protocol::UnPack(bufNew);
        auto n2 = Protocol::UnPack(bufNew);
        bool newOk = n1 && n2 && n1->data == f1->data && n2->data == f2->data;

        std::cout << "[sticky_two_frames] old=" << (oldOk ? "OK" : "FAIL")
                  << "  new=" << (newOk ? "OK" : "FAIL") << "\n";
        if (!oldOk || !newOk) fails++;
    }

    // 7. 关键场景：帧不完整（半包）+ data 含 0xFFFE，模拟 TCP 分段
    {
        auto f = makeFrame(FrameType::kFileType_Request_Chuck, std::vector<char>(5000, 'H'));
        f->data[100] = '\xFF'; f->data[101] = '\xFE';
        auto full = Protocol::Pack(f);
        // 只放前 60% 数据（半包）
        size_t half = full.size() * 6 / 10;

        miniBuffer bufOld;
        bufOld.Append(full.data(), half);
        auto o = unpackOld(bufOld);
        bool oldPartial = (o == nullptr);  // 半包应返回 nullptr
        // 补全后应能解析
        bufOld.Append(full.data() + half, full.size() - half);
        auto o2 = unpackOld(bufOld);
        bool oldFull = (o2 != nullptr) && (o2->data == f->data);

        miniBuffer bufNew;
        bufNew.Append(full.data(), half);
        auto n = Protocol::UnPack(bufNew);
        bool newPartial = (n == nullptr);
        bufNew.Append(full.data() + half, full.size() - half);
        auto n2 = Protocol::UnPack(bufNew);
        bool newFull = (n2 != nullptr) && (n2->data == f->data);

        std::cout << "[partial_then_full] old_partial_ok=" << oldPartial
                  << " old_full_ok=" << oldFull
                  << "  new_partial_ok=" << newPartial
                  << " new_full_ok=" << newFull << "\n";
        if (!oldFull || !newFull) fails++;
    }

    // 8. 内容粗判采样：相同文件应判一致，修改后应判不一致
    {
        QString dir = QDir::tempPath();
        QString pA = dir + "/sample_a.bin";
        QString pB = dir + "/sample_b.bin";
        QString pEmpty = dir + "/sample_empty.bin";
        const size_t fsz = 100 * 1024;
        std::vector<char> base(fsz);
        for (size_t i = 0; i < fsz; ++i) {
            base[i] = static_cast<char>(i % 251);
        }
        base[0] = '\xFF'; base[1] = '\xFE';   // 含 BOM 序列

        QFile::remove(pA); QFile::remove(pB); QFile::remove(pEmpty);
        {
            QFile fa(pA); if (!fa.open(QIODevice::WriteOnly)) { fails++; } else fa.write(base.data(), fsz);
            QFile fb(pB); if (!fb.open(QIODevice::WriteOnly)) { fails++; } else fb.write(base.data(), fsz);
            QFile fe(pEmpty); (void)fe.open(QIODevice::WriteOnly);   // 空文件
        }

        auto cmpSamples = [](const std::vector<SampleBlock>& a, const std::vector<SampleBlock>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (a[i].offset != b[i].offset || a[i].data != b[i].data) return false;
            }
            return true;
        };

        std::vector<SampleBlock> sA, sB, sEmpty;
        bool okA = LocalHandle::AskFileSamples(pA.toStdString(), sA);
        bool okB = LocalHandle::AskFileSamples(pB.toStdString(), sB);
        bool sameEq = okA && okB && cmpSamples(sA, sB);

        // 修改 B 的偏移 0 处（第一个采样点必覆盖）
        {
            QFile fb(pB);
            if (fb.open(QIODevice::ReadWrite)) {
                fb.seek(0); char c = 0x41; fb.write(&c, 1);
            }
        }
        std::vector<SampleBlock> sB2;
        LocalHandle::AskFileSamples(pB.toStdString(), sB2);
        bool diffDetected = !cmpSamples(sA, sB2);

        bool emptyOk = LocalHandle::AskFileSamples(pEmpty.toStdString(), sEmpty) && sEmpty.empty();

        std::cout << "[sample_compare] same_equal=" << sameEq
                  << " diff_detected=" << diffDetected
                  << " empty_ok=" << emptyOk
                  << " block_count=" << sA.size() << "\n";
        if (!sameEq || !diffDetected || !emptyOk) fails++;

        QFile::remove(pA); QFile::remove(pB); QFile::remove(pEmpty);
    }

    std::cout << "\n=== TOTAL FAILS: " << fails << " ===\n";
    return fails;
}