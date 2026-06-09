#pragma once

#include <QMutex>
#include <QTcpSocket>
#include <QTimer>
#include <any>
#include <atomic>
#include <functional>

#include "Protocol/Message.h"
#include "Protocol/Protocol.h"
#include "Utils/miniUtil.h"

// 我定义了一个叫 function_traits的模板，但我暂时不说它长什么样。
// 它现在是一个 不完整类型（incomplete type）。
// 强制使用者只能用“特化版本”
template <typename T> struct function_traits;

/*  模板特化（Template Specialization）。
    当 T是 std::function<R(Arg)>这种形式时, 我才给它一个“定义”
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

class ClientCore : public QObject
{
    Q_OBJECT

signals:
    void signalConnectting();
    void signalConnected();
    void signalDisconnected();
    void signalErrorOccurred();
    void signalOwnInfo(const ClientInfo& info);

public:
    ClientCore(QObject* parent = nullptr);
    virtual ~ClientCore();

public:
    static ClientCore* ceateInstance(QObject* parent = nullptr, ClientType clientType = ClientType::ControlSession);
    void setClientType(ClientType clientType);
    void setClientInfo(const ClientInfo& oInfo);
    void instance();

public:
    ClientType getClientType() const;
    ClientInfo getClientInfo() const;
    ClientInfo getSelfInfo() const;

    bool Send(FramePtr frame);
    bool Send(const Message& msg);
    bool Send(const char* data, size_t size);
    bool SendWithCall(FramePtr frame, std::function<void(Message)> callback);
    bool SendWithCall(FramePtr frame, std::function<void(FramePtr)> callback);
    bool SendWithCall(const Message& msg, std::function<void(Message)> callback);
    bool SendWithCall(const Message& msg, std::function<void(FramePtr)> callback);
    template <typename Callback> bool SendCall(FramePtr frame, Callback callback);

public slots:
    bool connectToServer(const QString& server, int16_t port);
    void disconnectFromServer();

protected:
    void initSignals();

protected:
    void onReadyRead();
    virtual void handleFrame(FramePtr frame) = 0;

    uint64_t GetSessionId();

    struct WaiteFrame {
        uint64_t sessionId;
        QTimer* timer{};
        std::function<void(std::any)> call;
    };

protected:
    QTcpSocket* tcp_{};
    QMutex waitLock_;
    ClientType clientType_{};
    std::atomic_uint64_t sessionId_{0};
    QMap<uint64_t, std::shared_ptr<WaiteFrame>> waitFrame_;

protected:
    miniBuffer buffer_;
    ClientInfo oInfo_;
    ClientInfo mInfo_;
};