#pragma once

#include <QThread>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "ControlSession.h"
#include "FileSession.h"

enum class FileControlState {
    FCS_Unconnected,
    FCS_Connected,
    FCS_Connecting
};

struct TransItem {
    FileMeta from;
    FileMeta to;
    bool isSend;
    // 压缩传输：true 表示本次传输为自描述 tar.xz 归档，接收方收完即解包。
    bool isArchive{false};
};

/*
          ----------------->  Server  <------------------
          |                  ^      ^                   |
          v                  |      |                   v
      MsgControl             |      |               MsgControl
        ^   |                |      |                 ^   |
        |   v                |      |                 |   v
      FileControl <-----------      --------------> FileControl

    文件控制相关的消息通过MsgContro转发是因为，对方的FileControl此时可能还不存在。
    文件体本身的数据，直接发给Server中转，不走MsgControl。
*/

// 客户端双链接助手
class DoubleLinker : public QObject
{
    Q_OBJECT

signals:
    void signalFileDoConnect(const QString& server, int16_t port);
    void signalSendControl(FramePtr frame);
    void signalSendFile(FramePtr frame);
    void signalAskFileID();
    void signalCancelWaitMsg();

signals:
    void signalCurFileItem(const QString& from, const QString& to);
    void signalCurFileProgress(std::uint64_t transed, std::uint64_t total);

public:
    DoubleLinker(QObject* parent = nullptr);
    ~DoubleLinker();

public:
    bool RunTask(const std::vector<std::shared_ptr<TransItem>>& tasks);
    // 阻塞直到任务结束（条件变量 + 总超时），不再 msleep 轮询 + processEvents
    bool RunTaskItem(const std::shared_ptr<TransItem>& item);
    void clearCurrentTaskItem();
    template <typename HandleResp> bool bRequest(FramePtr frame, HandleResp handleResp);
    template <typename HandleResp> void vRequest(FramePtr frame, HandleResp handleResp);

public:
    void Quit();
    bool waitFileConnect();
    // 心跳跑在控制连接自己的线程里：GUI 卡住也不能让服务器把连接踢掉
    void startHeartbeat();
    void SetControlSession(std::shared_ptr<ControlSession> session);
    void SetFileSession(std::shared_ptr<FileSession> session);

    std::shared_ptr<ControlSession> GetControlSession() const;
    std::shared_ptr<FileSession> GetFileSession() const;

public slots:
    void onDoFileConnectSuccess();
    void onDoFileConnectFailed();
    void onDoFileConnectError();

public slots:
    void onSendControl(FramePtr frame);
    void onSendFile(FramePtr frame);
    // 注意：这两个槽可能在套接字线程里被直接调用，只能做路由，不能碰 GUI 线程的对象
    void onDeliverControl(FramePtr frame);
    void onDeliverFile(FramePtr frame);
    void onCancelWaitMsg();
    void onNeedFileConnect();
    void onTaskStateChanged(const QString& uuid, bool isSend, int status);

private:
    std::shared_ptr<ControlSession> controlSession_{};
    std::shared_ptr<FileSession> fileSession_{};

private:
    QMutex fcStateLock_;
    FileControlState fcState_{};

    // curTask
private:
    // RunTaskItem 用条件变量等结果：状态变化由 FileSession 的 IO 线程直接通知
    struct TaskWait {
        bool registered{false};
        bool done{false};
        OneFileTrans::TransStatus status{OneFileTrans::TransStatus::Idle};
    };
    std::mutex taskMutex_;
    std::condition_variable taskCv_;
    TaskWait* taskWait_{};
    std::string taskWaitUUID_{};
    bool taskWaitIsSend_{};

    std::atomic<bool> isRunTaskItem_{false};
    bool curTaskIsSend_{};
    std::string curTaskUUID_{};

};

class CmdExecutor : public QObject
{
    using Step = std::function<FramePtr(FramePtr)>;
    Q_OBJECT

public:
    explicit CmdExecutor(QObject* parent, std::shared_ptr<DoubleLinker> doubleLinker);
    ~CmdExecutor() = default;

public:
    void Reset();
    void AddStep(Step step);
    bool Execute(FramePtr startFrame);

private:
    enum class ExecResult {
        Running,
        Success,
        Failed
    };
    void ExecuteStep(FramePtr frame);

private:
    std::vector<Step> steps_;
    size_t currentStep_ = 0;
    ExecResult execResult_ = ExecResult::Running;
    std::shared_ptr<DoubleLinker> doubleLinker_{};
};
