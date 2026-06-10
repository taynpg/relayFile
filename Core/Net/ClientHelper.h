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

// 客户端双链接助手
class DoubleLinker : public QObject
{
    Q_OBJECT

signals:
    void signalDoConnect(const QString& server, int16_t port);

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
    void onDoConnectSuccess();
    void onDoConnectFailed();
    void onDoConnectError();

public slots:
    void onDeliverControl(FramePtr frame);
    void onDeliverFile(FramePtr frame);

private:
    std::shared_ptr<ControlSession> controlSession_{};
    std::shared_ptr<FileSession> fileSession_{};

private:
    QMutex fcStateLock_;
    FileControlState fcState_{};
};

class CmdExecutor
{
    using Step = std::function<FramePtr(FramePtr)>;

public:
    explicit CmdExecutor(std::shared_ptr<DoubleLinker> doubleLinker);
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