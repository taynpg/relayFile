#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <memory>

#include "ClientCore.h"
#include "Protocol/FileMeta.h"
#include "Protocol/Protocol.h"

/*
### 传输可靠性约定

1. 接收方必须"先落盘、成功后才 ACK"。写盘失败即终止传输并通知对端，
   绝不让发送方在数据未落盘的情况下推进窗口。
2. 接收数据写入 `<目标路径>.relaypart`，只有长度校验通过后才 flush + 原子 rename
   到目标路径；失败/取消时删除 `.relaypart`，不会破坏目标路径上原有的文件。
   该临时文件同时是后续断点续传的基础（长度 + 文件指纹 = 续传偏移）。
3. 完成语义是端到端握手：
   `Request_Complete` -> 接收方校验落盘 -> `Answer_Complete{ok,err}` -> 发送方才置 Finished。
   Answer 丢失时发送方会重发 `Request_Complete`，接收方对已完成的传输幂等重发 Answer。

### 窗口与 ACK（第二批）

- 窗口按**字节**计（`windowBytes_`），与块大小解耦；块大小由接收方在握手时决定
  （`Answer_Send` / `Request_Start` 的 Message.blockSize、windowBytes）。
- ACK 是**累积**的：`index` = 下一个期望的块号（所有更小的块都已落盘）。
- ACK 的 `data` 是 SACK 位图，bit i 表示块 `index + i` 已收到（允许乱序到达）。
  发送方据此只重传真正的空洞，而不是"从头重发"。
- ACK 不需要每块一个：累计收到 `defAckBytes` 或距上次 ACK 超过 `defAckDelayMs` 才回一次。

`state_` 使用 `std::atomic<TransStatus>`：`getTransStatus` 在 GUI 线程轮询，
写入发生在 IO/接收回调线程，需要内存序保证可见性。
*/

class OneFileTrans : public QObject
{
    Q_OBJECT

signals:
    void signalProcess(std::uint64_t transed, std::uint64_t total);
    void signalFinished(const std::string& transId);
    void signalFailed(const std::string& transId, const std::string& errMsg);
    void signalInterrupt(const std::string& transId);
    void signalRequestSend(FramePtr frame);

public:
    enum class TransStatus {
        Idle = 0,
        Sending,
        Receving,
        Finished,
        Interrupted
    };

    enum class TransMode {
        Send,
        Receive
    };

public slots:
    void onFrameReceive(FramePtr frame);

public:
    OneFileTrans(QObject* parent = nullptr);

    bool initTransfer(TransMode mode, const Message& msg, const std::string& targetId, const std::string& ownId,
                      const std::string& uuid);
    void initSignals();

    void stopTrans();
    bool nextSend();
    bool handleAck(FramePtr frame);
    bool handleRecvChuck(FramePtr frame);
    // discardPart=true：删除 .relaypart（显式取消 / 对端取消）；
    // discardPart=false：保留残片，留给下次断点续传（超时、网络中断）
    bool handleInterrupt(FramePtr frame, bool discardPart = true);
    bool handleFinish(FramePtr frame);
    bool handleCompleteAnswer(FramePtr frame);
    TransMode getTransMode();
    TransStatus getTransStatus() const { return state_.load(std::memory_order_acquire); }
    QString getTransName() const;

    // 握手协商：接收方决定块大小/窗口，发送方在开始发送前套用
    void applyTransferParams(std::uint32_t blockSize, std::uint32_t windowBytes);
    std::uint32_t getBlockSize() const { return static_cast<std::uint32_t>(blockSize_); }
    std::uint32_t getWindowBytes() const { return static_cast<std::uint32_t>(windowBytes_); }

    // 断点续传：接收方已连续落盘的字节数（块对齐） / 源文件快速指纹
    std::uint64_t getResumeOffset() const { return resumeOffset_; }
    std::string getFingerprint() const { return fingerprint_; }
    void applyResumeOffset(std::uint64_t offset);

    void onSendOrRecvTimeout();
    void setTargetControlId(const std::string& targetControlId);
    FramePtr CreateFrame(FrameType type);

private:
    struct BlockState {
        qint64 lastSendMs{};
        int retries{};
    };

    void sendCompleteFrame();
    void answerComplete(bool ok, const std::string& err);
    void tryReusePartFile(const Message& msg);
    bool finalizeRecvFile(QString& err);
    void discardPartFile();
    void failAndInterrupt(const std::string& err);

    void sendBlock(std::uint64_t idx);
    void sendAck();
    std::vector<char> buildSackBitmap() const;
    void applySackBitmap(const std::vector<char>& bitmap);
    std::uint64_t inflightBytes() const;
    bool writeAll(const char* data, qint64 size);
    bool shouldSendAck() const;

private:
    TransMode tMode_{};
    QMutex qMut_;
    QTimer* sendOrRecvTimeout_{};
    std::atomic<TransStatus> state_{TransStatus::Idle};

    FileMeta meta_;
    Message msg_;
    QString filePath_;
    // 接收侧实际写入的临时文件：<filePath_>.relaypart，校验通过后才 rename 到 filePath_
    QString partPath_;
    std::string uuid_;
    std::string targetId_;
    std::string targetControlId_;
    std::string ownId_;

    QFile sendFile_;
    QFile recvFile_;

    std::uint64_t totalSize_{};
    std::uint64_t transSize_{};
    std::uint64_t blockSize_{defBlockSize};
    std::uint64_t windowBytes_{defWindowBytes};
    std::uint64_t totalBlocks_{};

    // 发送方：ackIndex_=累计已确认（下一个期望块），nextIndex_=下一待发块
    // 接收方：curBlockIndex_=累计连续收到的块数（下一个期望块）
    std::uint64_t ackIndex_{};
    std::uint64_t nextIndex_{};
    std::uint64_t curBlockIndex_{};
    // 已发送但还没被"累计确认"或 SACK 命中的块
    std::map<std::uint64_t, BlockState> inflight_;
    // 接收方：乱序到达、已经落盘的块（等空洞补齐后提升为 curBlockIndex_）
    std::set<std::uint64_t> recvOutOfOrder_;

    // 累积 ACK 节流
    std::uint64_t bytesSinceAck_{};
    qint64 lastAckMs_{};

    // 完成握手：发送方已发 Complete 并等待 Answer_Complete；接收方已回过 Answer（幂等重发用）
    bool completeSent_{};
    int completeRetry_{};
    bool completeAnswered_{};
    bool completeAnswerOk_{};
    std::string completeAnswerErr_;
    // .relaypart 已成功 rename 到目标路径
    bool fileFinalized_{};

    // 断点续传：接收方复用了多长的 .relaypart；发送方要跳过的字节数
    std::uint64_t resumeOffset_{};
    std::string fingerprint_;

    // 压缩传输：msg 带 kArchive 标志，本传输为一个自描述 tar.xz 归档包，
    // 接收方收完后就地解包并按内嵌清单写入各目的路径，随后删除临时归档。
    bool isArchive_{};
};
