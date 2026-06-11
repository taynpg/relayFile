#pragma once

#include <QMap>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <any>

#include "ClientCore.h"
#include "Protocol/Protocol.h"
#include "Utils/ThreadPoolSTD.hpp"
#include "Utils/TimerSTD.hpp"

// 我定义了一个叫 function_traits的模板，但我暂时不说它长什么样。
// 它现在是一个 不完整类型（incomplete type）。
// 强制使用者只能用“特化版本”
template <typename T> struct function_traits;

/*  模板特化（Template Specialization）。
    当 T 是 std::function<R(Arg)>这种形式时, 我才给它一个“定义”
                   写法                               含义
           std::function<R(Arg)>         一个只接受一个参数的 std::function
                    R                                返回值
                   Arg                              参数类型
        using argument_type = Arg;              对外暴露参数类型
*/
template <typename R, typename Arg> struct function_traits<std::function<R(Arg)>> {
    using argument_type = Arg;
};

enum class ClientType {
    ControlSession,
    FileSession,
};

enum class CallType {
    CT_Unknown,
    CT_Message,
    CT_Frame
};

class ControlSession : public QObject
{
    Q_OBJECT

signals:
    void signalHome(FramePtr frame);
    void signalFileList(FramePtr frame);
    void signalRequestSend(FramePtr frame);
    void signalOwnInfo(const ClientInfo& info);

public:
    ControlSession(QObject* parent = nullptr);
    ~ControlSession() override;

public:
    struct WaiteFrame : std::enable_shared_from_this<WaiteFrame> {
        uint64_t sessionId;
        CallType callType{};
        std::shared_ptr<TimerStd> timer;
        std::function<void(std::any)> call;
    };

    struct TaskWorker {
        uint64_t sessionId;
        FramePtr frame{};
        bool isDone{false};
        static std::shared_ptr<TaskWorker> CreateWorker(FramePtr frame);
    };

    template <typename Callback> bool SendCall(FramePtr frame, Callback callback);
    bool SendWithCall(FramePtr frame, std::function<void(MessagePtr)> callback);
    bool SendWithCall(FramePtr frame, std::function<void(FramePtr)> callback);
    bool SendWithCall(const Message& msg, FrameType type, std::function<void(MessagePtr)> callback);
    bool SendWithCall(const Message& msg, FrameType type, std::function<void(FramePtr)> callback);

public:
    void Quit();
    void handleFrame(FramePtr frame);
    ClientInfo getOtherInfo();
    ClientInfo getOwnInfo();
    ClientCore* getClientCore();

public slots:
    void AskOwnID();

private:
    void initSignals();
    void clearWorker();

private:
    ClientCore* clientCore_{};
    QTimer* clearWorkerTimer_{};
    QMutex requestWaitLock_;
    QMutex responseWaitLock_;
    ClientWorker* clientWorker_{};
    std::shared_ptr<ThreadPool> workerPool_{};
    QMap<uint64_t, std::shared_ptr<WaiteFrame>> requestWaitFrame_;
    QMap<uint64_t, std::shared_ptr<TaskWorker>> responseWaitWorker_;
};