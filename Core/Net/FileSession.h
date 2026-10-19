#pragma once

#include <QPointer>
#include <QThread>
#include <atomic>
#include <memory>

#include "OneFileTrans.h"

class FileSessionIO;

/*
### 文件会话（对外的门面）

真正的传输逻辑在 `FileSessionIO`，它跑在独立的 IO 线程里：
读源文件、写 `.relaypart`、回 ACK、跑重传定时器，全都不占用 GUI 线程。

本类只做三件事：
1. 持有文件通道的 `ClientCore`（套接字线程）与传输 IO 线程；
2. 把收到的帧、清任务、请求 ID 等调用**投递**到 IO 线程（本类的方法可以从任意线程调用）；
3. 把 IO 线程发出来的进度/任务状态信号转发给 GUI。

### 线程模型

    GUI 主线程 ──信号──> 传输 IO 线程（FileSessionIO / OneFileTrans）
        ^                     │
        └───── 队列信号 ───────┘
    套接字线程 ── 收帧后直接投递 ──> 传输 IO 线程
*/
class FileSession : public QObject
{
    Q_OBJECT

signals:
    void signalRequestSend(FramePtr frame);

signals:
    void signalCurFileProgress(std::uint64_t transed, std::uint64_t total);
    void signalCurFile(const QString& from, const QString& to);
    // 任务状态变化：uuid / 是否发送侧 / TransStatus 整数值
    void signalTaskStateChanged(const QString& uuid, bool isSend, int status);
    void signalNeedFileConnect();

public:
    FileSession(QObject* parent = nullptr);
    ~FileSession();

public:
    void Quit();
    ClientCore* getClientCore();

    // 以下均可在任意线程调用：内部只做投递/只读状态
    void handleFrame(FramePtr frame);
    bool getTransStatus(OneFileTrans::TransStatus& status, const std::string& baseUUID, bool isSend);
    void clearTask(const std::string& uuid, bool isSend);

public:
    // 传输引擎对象（生命周期由本类管理）。DoubleLinker 直连它的信号，
    // 用 DirectConnection 绕开 GUI 线程，否则 GUI 一阻塞通知就断。
    FileSessionIO* io() const;

public slots:
    void AskOwnID();
    void setServerEndpoint(const QString& ip, qint16 port);

private:
    QPointer<FileSessionIO> io_;
    QThread* ioThread_{};
    ClientCore* clientCore_{};
    ClientWorker* clientWorker_{};
    // Quit 可能被显式调用，也可能来自析构，必须幂等
    std::atomic<bool> quit_{false};
};
