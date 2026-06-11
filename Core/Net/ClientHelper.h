#pragma once

#include <QThread>
#include <QTimer>

#include "ControlSession.h"
#include "FileSession.h"

enum class FileControlState {
    FCS_Unconnected,
    FCS_Connected,
    FCS_Connecting
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

public:
    DoubleLinker(QObject* parent = nullptr);
    ~DoubleLinker();

public:
    template <typename HandleResp> bool bRequest(FramePtr frame, HandleResp handleResp);
    template <typename HandleResp> void vRequest(FramePtr frame, HandleResp handleResp);

public:
    void Quit();
    
    void SetControlSession(std::shared_ptr<ControlSession> session);
    void SetFileSession(std::shared_ptr<FileSession> session);

    std::shared_ptr<ControlSession> GetControlSession() const;
    std::shared_ptr<FileSession> GetFileSession() const;

    bool waitFileConnect();

public slots:
    void onDoFileConnectSuccess();
    void onDoFileConnectFailed();
    void onDoFileConnectError();

public slots:
    void onSendControl(FramePtr frame);
    void onSendFile(FramePtr frame);
    void onDeliverControl(FramePtr frame);
    void onDeliverFile(FramePtr frame);

private:
    std::shared_ptr<ControlSession> controlSession_{};
    std::shared_ptr<FileSession> fileSession_{};

private:
    QMutex fcStateLock_;
    FileControlState fcState_{};
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