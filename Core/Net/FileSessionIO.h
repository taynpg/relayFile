#pragma once

#include <QMap>
#include <QMutex>
#include <QObject>
#include <atomic>
#include <string>

#include "OneFileTrans.h"

/*
### 传输引擎（跑在独立 IO 线程）

第三批把 `OneFileTrans` 从 GUI 线程搬到了这里：

    主线程(GUI)  ──信号──>  传输 IO 线程（读文件 / 写 .relaypart / ACK / 重传定时器）
        ^                        │
        └────── 队列信号 ─────────┘
    套接字线程 ── 收帧后直接投递 ──>  传输 IO 线程

以前每个 256KB 块都要：套接字线程 → GUI 线程排队 → 写盘 → 再排队回去发 ACK。
只要 UI 忙一下，ACK 就晚到，发送方就重传，于是才有了"ACK 优先"、"超时降到 5s"那套补丁。
现在写盘不在 GUI 线程上，这些补丁都可以拿掉了。

约束：
- 本类所有成员函数（除 getTransStatus 外）只在 IO 线程里执行；
- getTransStatus 只读 `transferMap_` 与 `OneFileTrans::state_`（atomic），可跨线程调用；
- 发出去的帧通过 signalRequestSend 交给 FileSession，再由 DoubleLinker 投到套接字线程。
*/
class FileSessionIO : public QObject
{
    Q_OBJECT

signals:
    void signalRequestSend(FramePtr frame);
    void signalCurFileProgress(std::uint64_t transed, std::uint64_t total);
    void signalCurFile(const QString& from, const QString& to);
    // 任务状态变化（uuid / 是否发送侧 / TransStatus 的整数值）
    void signalTaskStateChanged(const QString& uuid, bool isSend, int status);
    // 文件通道还没连上时，请上层帮忙发起连接
    void signalNeedFileConnect();

public:
    explicit FileSessionIO(ClientCore* clientCore, QObject* parent = nullptr);

public:
    // 可在任意线程调用：只读 atomic 状态 + 锁内取 shared_ptr
    bool getTransStatus(OneFileTrans::TransStatus& status, const std::string& baseUUID, bool isSend);

public slots:
    void handleFrame(FramePtr frame);
    void clearTask(const QString& uuid, bool isSend);
    void askOwnID();
    void setServerEndpoint(const QString& ip, qint16 port);
    // 停止所有传输并清空映射；在 IO 线程内执行
    void shutdown();

private:
    bool ensureConnected();
    void pushTask(const std::shared_ptr<OneFileTrans>& fileTrans, const Message& msg, const std::string& errMsg,
                  FrameType type, bool ret, bool needConnect);
    std::string getMapKeyUUIDByMode(const std::string& uuid, OneFileTrans::TransMode mode);
    std::string getMapKeyUUIDByDirection(const std::string& uuid, bool fromReceiver);
    void emitStateChanged(const std::string& uuid, OneFileTrans::TransMode mode, OneFileTrans::TransStatus status);

private:
    QMutex transferMapLock_;
    ClientCore* clientCore_{};
    bool quit_{false};
    QString serverIp_;
    qint16 serverPort_{};
    QMap<std::string, std::shared_ptr<OneFileTrans>> transferMap_;
};
