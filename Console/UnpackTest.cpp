// 协议编解码测试（第二批新帧格式）
// 覆盖：粘包/半包、数据体里出现 magic、前导垃圾重同步、超长伪帧头、
//       路由字段变长、body CRC 损坏、版本不符。
#include <File/FileDir.h>
#include <File/LocalHandle.h>
#include <Net/OneFileTrans.h>
#include <Protocol/Protocol.h>
#include <Protocol/Serialize.hpp>
#include <Utils/miniUtil.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QThread>
#include <cstring>
#include <iostream>
#include <vector>

static int gFails = 0;

static void check(const char* name, bool ok)
{
    std::cout << "[" << name << "] " << (ok ? "OK" : "FAIL") << "\n";
    if (!ok) {
        ++gFails;
    }
}

static std::shared_ptr<OneFrame> makeFrame(FrameType type, const std::vector<char>& data)
{
    auto f = OneFrame::Create();
    f->type = type;
    f->from = "127.0.0.1:5555";
    f->to = "127.0.0.1:6666";
    f->fuuid = "0f8fad5b-d9cb-469f-a165-70867728950e";
    f->data = data;
    return f;
}

// 解析一个完整缓存里的所有帧
static std::vector<FramePtr> drain(miniBuffer& buf, UnPackResult* last = nullptr)
{
    std::vector<FramePtr> out;
    while (true) {
        FramePtr f;
        auto r = Protocol::UnPack(buf, f);
        if (last) {
            *last = r;
        }
        if (r != UnPackResult::Ok) {
            break;
        }
        out.push_back(f);
    }
    return out;
}

static void testRoundTrip()
{
    for (std::size_t size : {0u, 1u, 1000u, 256u * 1024u}) {
        auto f = makeFrame(FrameType::kFileType_Request_Chuck, std::vector<char>(size, 'A'));
        f->sessionId = -1;
        f->index = 12345;
        auto packed = Protocol::Pack(f);
        miniBuffer buf;
        buf.Append(packed.data(), packed.size());
        auto got = drain(buf);
        bool ok = got.size() == 1 && got[0]->data == f->data && got[0]->index == f->index &&
                  got[0]->sessionId == f->sessionId && got[0]->from == f->from && got[0]->to == f->to &&
                  got[0]->fuuid == f->fuuid && got[0]->type == f->type;
        std::cout << "          size=" << size << " wire=" << packed.size() << "\n";
        check("roundtrip", ok);
    }
}

static void testMagicInBody()
{
    // 数据体故意包含新 magic（旧格式里是 0xFFFE 踩坑）
    std::vector<char> body(4096, 'B');
    const char magic[4] = {'R', 'F', 'M', '1'};
    for (std::size_t i = 0; i + 4 <= body.size(); i += 100) {
        std::memcpy(body.data() + i, magic, 4);
    }
    auto f = makeFrame(FrameType::kFileType_Request_Chuck, body);
    auto packed = Protocol::Pack(f);
    miniBuffer buf;
    buf.Append(packed.data(), packed.size());
    auto got = drain(buf);
    check("magic_in_body", got.size() == 1 && got[0]->data == body);
}

static void testLeadingGarbage()
{
    auto f = makeFrame(FrameType::kFileType_Request_Chuck, std::vector<char>(1000, 'C'));
    auto packed = Protocol::Pack(f);

    // 前导垃圾里塞一个"看起来像帧头但 CRC 不对"的假头
    std::vector<char> garbage;
    for (int i = 0; i < 37; ++i) {
        garbage.push_back(static_cast<char>(i * 7));
    }
    const char magic[4] = {'R', 'F', 'M', '1'};
    std::memcpy(garbage.data() + 5, magic, 4);
    // 伪造一个巨大的 frameLen（magic 从 offset 5 开始，frameLen 位于 5+28）
    for (int i = 0; i < 4; ++i) {
        garbage[5 + 28 + i] = static_cast<char>(0xFF);
    }

    miniBuffer buf;
    buf.Append(garbage.data(), garbage.size());
    buf.Append(packed.data(), packed.size());
    auto got = drain(buf);
    check("leading_garbage_resync", got.size() == 1 && got[0]->data == f->data);
}

static void testStickyAndPartial()
{
    auto f1 = makeFrame(FrameType::kFileType_Request_Chuck, std::vector<char>(2000, 'D'));
    auto f2 = makeFrame(FrameType::kFileType_Request_Chuck, std::vector<char>(3000, 'E'));
    f2->index = 7;
    auto p1 = Protocol::Pack(f1);
    auto p2 = Protocol::Pack(f2);

    // 粘包
    {
        miniBuffer buf;
        buf.Append(p1.data(), p1.size());
        buf.Append(p2.data(), p2.size());
        auto got = drain(buf);
        check("sticky_two_frames", got.size() == 2 && got[0]->data == f1->data && got[1]->data == f2->data &&
                                       got[1]->index == 7);
    }

    // 半包：逐个字节喂进去，最后必须恰好解出一帧
    {
        miniBuffer buf;
        FramePtr got;
        bool premature = false;
        for (std::size_t i = 0; i + 1 < p1.size(); ++i) {
            buf.Append(p1.data() + i, 1);
            FramePtr f;
            if (Protocol::UnPack(buf, f) != UnPackResult::NeedMoreData) {
                premature = true;   // 帧没齐就解出来了，说明长度字段有问题
                break;
            }
        }
        if (!premature) {
            buf.Append(p1.data() + p1.size() - 1, 1);
            auto r = Protocol::UnPack(buf, got);
            check("partial_then_full", r == UnPackResult::Ok && got && got->data == f1->data);
        } else {
            check("partial_then_full", false);
        }
    }
}

static void testRouteFieldLimits()
{
    auto f = makeFrame(FrameType::kMsgType_Ask_Home, {});
    f->from = std::string(255, 'x');
    auto packed = Protocol::Pack(f);
    bool ok255 = !packed.empty();

    f->from = std::string(256, 'x');
    auto oversize = Protocol::Pack(f);
    check("route_field_255_ok_256_reject", ok255 && oversize.empty());

    // 变长路由字段：中文路径不会被截断
    auto g = makeFrame(FrameType::kMsgType_Ask_FileMeta, std::vector<char>{'a'});
    g->from = "192.168.100.200:65535";
    g->to = "客户端-A:1234";
    g->fuuid = "uuid-中文-1234567890";
    auto p = Protocol::Pack(g);
    miniBuffer buf;
    buf.Append(p.data(), p.size());
    auto got = drain(buf);
    check("route_field_var_len", got.size() == 1 && got[0]->to == g->to && got[0]->fuuid == g->fuuid);
}

static void testCorruption()
{
    auto f = makeFrame(FrameType::kFileType_Request_Chuck, std::vector<char>(500, 'F'));
    auto packed = Protocol::Pack(f);

    // 1) body 损坏 → ProtocolError
    {
        auto bad = packed;
        const std::size_t bodyOffset = 36 + 3 + f->from.size() + f->to.size() + f->fuuid.size();
        bad[bodyOffset + 10] = static_cast<char>(bad[bodyOffset + 10] ^ 0x01);
        miniBuffer buf;
        buf.Append(bad.data(), bad.size());
        FramePtr got;
        check("body_crc_corrupt", Protocol::UnPack(buf, got) == UnPackResult::ProtocolError);
    }

    // 2) 头部字段损坏（CRC 不匹配）→ 当作假 magic 丢掉，不会解出错误帧
    {
        auto bad = packed;
        bad[20] = static_cast<char>(bad[20] ^ 0x01);   // 改 index
        miniBuffer buf;
        buf.Append(bad.data(), bad.size());
        FramePtr got;
        auto r = Protocol::UnPack(buf, got);
        check("header_crc_corrupt", r == UnPackResult::NeedMoreData && got == nullptr);
    }

    // 3) 版本不符（CRC 正确）→ ProtocolError
    {
        auto bad = packed;
        bad[4] = static_cast<char>(FrameProto::kVersion + 1);
        const std::uint32_t crc = Protocol::Crc32(bad.data(), 32);
        for (int i = 0; i < 4; ++i) {
            bad[32 + i] = static_cast<char>((crc >> (8 * i)) & 0xFF);
        }
        miniBuffer buf;
        buf.Append(bad.data(), bad.size());
        FramePtr got;
        check("version_mismatch", Protocol::UnPack(buf, got) == UnPackResult::ProtocolError);
    }
}

// 发送侧：SACK 命中的块不再重传，超时只重传真正的空洞
static void testSenderSack()
{
    const std::uint64_t blockSize = defBlockSize;   // 发送侧默认 256KB
    const std::uint64_t totalSize = blockSize * 8;

    QString srcPath = QDir::tempPath() + "/sack_src.bin";
    {
        QFile f(srcPath);
        if (!f.open(QIODevice::WriteOnly)) {
            check("sender_sack", false);
            return;
        }
        std::vector<char> buf(totalSize);
        for (std::uint64_t i = 0; i < totalSize; ++i) {
            buf[i] = static_cast<char>(i % 251);
        }
        f.write(buf.data(), static_cast<qint64>(totalSize));
    }

    OneFileTrans trans;
    std::vector<FramePtr> sent;
    QObject::connect(&trans, &OneFileTrans::signalRequestSend, [&sent](FramePtr f) { sent.push_back(f); });
    QObject::connect(&trans, &OneFileTrans::signalFailed,
                     [](const std::string&, const std::string& err) { std::cerr << "  failed: " << err << "\n"; });

    Message msg;
    msg.ff.fullPath = srcPath.toStdString();
    msg.ff.size = totalSize;
    msg.ff.mark = FileDir::GetMark();
    bool inited = trans.initTransfer(OneFileTrans::TransMode::Send, msg, "peer", "me", "uuid-sack");
    bool sentAll = inited && trans.nextSend() && sent.size() == 8;

    // 造一个 ACK：累计确认 index=0（块 0 没到），SACK 位图把块 1..7 全部标为已收到
    auto ack = OneFrame::Create();
    ack->type = FrameType::kFileType_Request_Ack;
    ack->index = 0;
    ack->data.assign(32, 0);
    for (std::size_t i = 1; i < 8; ++i) {
        ack->data[i / 8] = static_cast<char>(static_cast<unsigned char>(ack->data[i / 8]) | (1u << (i % 8)));
    }
    sent.clear();
    trans.handleAck(ack);
    const bool noSpurious = sent.empty();   // 没有新块可发，也不该立刻重传

    // 触发一次超时：只有块 0 是空洞，应该只重传块 0
    trans.onSendOrRecvTimeout();
    bool onlyHole = sent.size() == 1 && sent[0]->index == 0;
    std::cout << "          sent_after_timeout=" << sent.size();
    if (!sent.empty()) {
        std::cout << " index=" << sent[0]->index;
    }
    std::cout << "\n";
    check("sender_sack_only_retransmit_hole", sentAll && noSpurious && onlyHole);

    trans.stopTrans();
    QFile::remove(srcPath);
    QFile::remove(srcPath + ".relaypart");
}

// 接收侧：乱序到达先落盘、SACK 位图正确、空洞补齐后 curBlockIndex_ 一次推进
static void testReceiverOutOfOrder()
{
    const std::uint64_t totalSize = 5 * defMinBlockSize;   // 5 块 × 64KB
    QString dstPath = QDir::tempPath() + "/oo_dst.bin";
    QFile::remove(dstPath);
    QFile::remove(dstPath + ".relaypart");

    std::vector<char> content(totalSize);
    for (std::uint64_t i = 0; i < totalSize; ++i) {
        content[i] = static_cast<char>((i * 31 + 7) % 251);
    }

    OneFileTrans trans;
    std::vector<FramePtr> sent;
    QObject::connect(&trans, &OneFileTrans::signalRequestSend, [&sent](FramePtr f) { sent.push_back(f); });

    Message msg;
    msg.ft.fullPath = dstPath.toStdString();
    msg.ft.size = totalSize;
    msg.ft.mark = FileDir::GetMark();
    bool inited = trans.initTransfer(OneFileTrans::TransMode::Receive, msg, "peer", "me", "uuid-oo");
    bool bsOk = trans.getBlockSize() == defMinBlockSize;

    auto chunk = [&content](std::uint64_t idx, std::uint64_t bs) {
        auto f = OneFrame::Create();
        f->type = FrameType::kFileType_Request_Chuck;
        f->index = static_cast<std::int64_t>(idx);
        const auto off = idx * bs;
        const auto len = std::min<std::uint64_t>(bs, content.size() - off);
        f->data.assign(content.begin() + off, content.begin() + off + len);
        return f;
    };

    // 0,1,3,4 先到（3 是空洞后的乱序块），最后补 2
    std::uint64_t sackBitsAfter3 = 0;
    for (std::uint64_t idx : {0ull, 1ull, 3ull, 4ull, 2ull}) {
        QThread::msleep(25);   // 让累积 ACK 的 20ms 延迟规则生效，好检查位图
        sent.clear();
        trans.handleRecvChuck(chunk(idx, defMinBlockSize));
        if (idx == 3 && !sent.empty()) {
            const auto& bm = sent.back()->data;
            if (bm.size() > 0) {
                sackBitsAfter3 = static_cast<unsigned char>(bm[0]);
            }
        }
    }
    bool sackOk = (sackBitsAfter3 & 0x02) != 0;   // cur=2 时，块 3 → bit1

    // 收尾握手
    sent.clear();
    auto fin = OneFrame::Create();
    fin->type = FrameType::kFileType_Request_Complete;
    trans.handleFinish(fin);
    bool finished = trans.getTransStatus() == OneFileTrans::TransStatus::Finished;
    bool hasAnswer = !sent.empty() && sent.back()->type == FrameType::kFileType_Answer_Complete;

    QFile out(dstPath);
    bool contentOk = false;
    if (out.open(QIODevice::ReadOnly)) {
        auto got = out.readAll();
        contentOk = static_cast<std::uint64_t>(got.size()) == totalSize &&
                    std::memcmp(got.constData(), content.data(), totalSize) == 0;
        out.close();
    }
    bool noPart = !QFile::exists(dstPath + ".relaypart");

    std::cout << "          sack_byte0=" << sackBitsAfter3 << " blockSize=" << trans.getBlockSize() << "\n";
    check("receiver_out_of_order", inited && bsOk && sackOk && finished && hasAnswer && contentOk && noPart);

    QFile::remove(dstPath);
    QFile::remove(dstPath + ".relaypart");
}

// 内容粗判采样：相同文件应判一致，修改后应判不一致
static void testSamples()
{
    QString dir = QDir::tempPath();
    QString pA = dir + "/sample_a.bin";
    QString pB = dir + "/sample_b.bin";
    QString pEmpty = dir + "/sample_empty.bin";
    const std::size_t fsz = 100 * 1024;
    std::vector<char> base(fsz);
    for (std::size_t i = 0; i < fsz; ++i) {
        base[i] = static_cast<char>(i % 251);
    }
    base[0] = '\xFF';
    base[1] = '\xFE';

    QFile::remove(pA);
    QFile::remove(pB);
    QFile::remove(pEmpty);
    {
        QFile fa(pA);
        if (!fa.open(QIODevice::WriteOnly)) {
            ++gFails;
        } else {
            fa.write(base.data(), static_cast<qint64>(fsz));
        }
        QFile fb(pB);
        if (!fb.open(QIODevice::WriteOnly)) {
            ++gFails;
        } else {
            fb.write(base.data(), static_cast<qint64>(fsz));
        }
        QFile fe(pEmpty);
        (void)fe.open(QIODevice::WriteOnly);
    }

    auto cmpSamples = [](const std::vector<SampleBlock>& a, const std::vector<SampleBlock>& b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].offset != b[i].offset || a[i].data != b[i].data) {
                return false;
            }
        }
        return true;
    };

    std::vector<SampleBlock> sA;
    std::vector<SampleBlock> sB;
    std::vector<SampleBlock> sEmpty;
    bool okA = LocalHandle::AskFileSamples(pA.toStdString(), sA);
    bool okB = LocalHandle::AskFileSamples(pB.toStdString(), sB);
    bool sameEq = okA && okB && cmpSamples(sA, sB);

    {
        QFile fb(pB);
        if (fb.open(QIODevice::ReadWrite)) {
            fb.seek(0);
            char c = 0x41;
            fb.write(&c, 1);
        }
    }
    std::vector<SampleBlock> sB2;
    LocalHandle::AskFileSamples(pB.toStdString(), sB2);
    bool diffDetected = !cmpSamples(sA, sB2);

    bool emptyOk = LocalHandle::AskFileSamples(pEmpty.toStdString(), sEmpty) && sEmpty.empty();
    check("sample_compare", sameEq && diffDetected && emptyOk);

    QFile::remove(pA);
    QFile::remove(pB);
    QFile::remove(pEmpty);
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    testRoundTrip();
    testMagicInBody();
    testLeadingGarbage();
    testStickyAndPartial();
    testRouteFieldLimits();
    testCorruption();
    testSenderSack();
    testReceiverOutOfOrder();
    testSamples();

    std::cout << "\n=== TOTAL FAILS: " << gFails << " ===\n";
    return gFails;
}
