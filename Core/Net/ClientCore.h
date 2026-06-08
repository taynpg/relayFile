#pragma once

#include <QMutex>
#include <QTcpSocket>
#include <QTimer>
#include <atomic>

#include "Protocol/Message.h"
#include "Protocol/Protocol.h"
#include "Utils/miniUtil.h"

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

    bool Send(FramePtr frame);
    bool SendWithCall(FramePtr frame, std::function<void(FramePtr)> callback);
    bool Send(const char* data, size_t size);

public slots:
    bool connectToServer(const QString& server, int16_t port);
    void disconnectFromServer();

protected:
    void initSignals();

protected:
    void onReadyRead();
    void baseHandleFrame(FramePtr frame);
    virtual void handleFrame(FramePtr frame) = 0;

    uint64_t GetSessionId();

    struct WaiteFrame {
        uint64_t sessionId;
        QTimer* timer{};
        std::function<void(FramePtr)> callback{};
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
};