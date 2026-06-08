#pragma once

#include <QReadWriteLock>
#include <QTcpSocket>

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
    ~ClientCore();

public:
    static ClientCore* ceateInstance(QObject* parent = nullptr, ClientType clientType = ClientType::ControlSession);
    void setClientType(ClientType clientType);
    void setClientInfo(const ClientInfo& oInfo);
    void instance();

public:
    ClientType getClientType() const;

    bool Send(FramePtr frame);
    bool Send(const char* data, size_t size);

public slots:
    bool connectToServer(const QString& server, int16_t port);

protected:
    void initSignals();

protected:
    void onReadyRead();
    void baseHandleFrame(FramePtr frame);
    virtual void handleFrame(FramePtr frame) = 0;

protected:
    QTcpSocket* tcp_{};
    ClientType clientType_{};

protected:
    miniBuffer buffer_;
    ClientInfo oInfo_;
};