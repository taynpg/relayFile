#pragma once

#include <atomic>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <fstream>
#include <memory>

#include "ClientCore.h"
#include "Protocol/FileMeta.h"
#include "Protocol/Protocol.h"

/*
### 1. 接收方 ACK 优先（核心修复）— OneFileTrans.cpp handleRecvChuck
原来先写盘再发 ACK，写盘慢（尤其网络盘）会阻塞主线程，ACK 延迟触发发送方 15 秒超时重传。
而重传后 ACK 才到会重置重传计数，形成"超时→重传→ACK 到→重置→再超时"的死循环，表现为永久卡住。

改为：先立即回 ACK，再写盘。 ACK 不再被写盘阻塞，发送方窗口能持续推进。

### 2. state_ 原子化 — OneFileTrans.h
`state_` 从普通`TransStatus` 改为`std::atomic<TransStatus>` ，所有读写用`load()/store()` 。`getTransStatus` （workerThread_ 轮询）
和 OneFileTrans（主线程）之间无锁跨线程访问现在有了内存序保证，避免状态变化不可见导致 RunTaskItem 死循环。

### 3. 缩短发送超时 — CoreDefine.hpp
`defSendTimeout` 从 15000ms 降到 5000ms，加快故障检测和重传节奏，减少单次卡顿持续时间。

### 4. Send 返回值检查 — ClientCore.cpp + ClientHelper.cpp
`ClientCore::Send` 失败（连接断开/写入不完整）时打 warning 日志，不再静默丢帧；DoubleLinker 的控制/文件连接发送 lambda 都检查返回值。
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
    bool handleInterrupt(FramePtr frame);
    bool handleFinish(FramePtr frame);
    TransMode getTransMode();
    TransStatus getTransStatus() const { return state_.load(std::memory_order_acquire); }
    QString getTransName() const;

    void onSendOrRecvTimeout();
    void setTargetControlId(const std::string& targetControlId);
    FramePtr CreateFrame(FrameType type);

private:
    TransMode tMode_{};
    QMutex qMut_;
    QTimer* sendOrRecvTimeout_{};
    std::atomic<TransStatus> state_{TransStatus::Idle};

    FileMeta meta_;
    Message msg_;
    QString filePath_;
    std::string uuid_;
    std::string targetId_;
    std::string targetControlId_;
    std::string ownId_;

    QFile sendFile_;
    QFile recvFile_;

    std::uint64_t totalSize_{};
    std::uint64_t transSize_{};
    // 发送方：ackIndex_=最早未确认块号(窗口左沿)，sentIndex_=下一待发块号(右沿+1)
    // 接收方：curBlockIndex_=期望收到的块号
    std::uint64_t ackIndex_{};
    std::uint64_t sentIndex_{};
    std::uint64_t curBlockIndex_{};
    bool eofReached_{};
    int retransCount_{};
    std::uint64_t blockSize_{defBlockSize};
};