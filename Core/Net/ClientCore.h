#pragma once

#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <functional>

#include "Protocol/Message.h"
#include "Protocol/Protocol.h"
#include "Utils/miniUtil.h"

class ClientCore;
// 客户端工作线程
class ClientWorker : public QThread
{
    Q_OBJECT
public:
    ClientWorker(ClientCore* core, QObject* parent = nullptr);
    ~ClientWorker();

protected:
    void run() override;

private:
    ClientCore* core_{};
};

class ClientCore : public QObject
{
    Q_OBJECT

signals:
    void signalConnectting();
    void signalConnected();
    void signalDisconnected();
    void signalErrorOccurred();
    void signalRequestAskOwnID();
    void signalDeliverFrame(FramePtr frame);

public:
    ClientCore(QObject* parent = nullptr);
    virtual ~ClientCore();

public:
    void instance();
    void Quit();

    bool isConnected() const;

public:
    ClientInfo getClientInfo() const;
    QString getClientFullName() const;

    void setOtherClientInfo(const ClientInfo& oInfo);

    QString getServerIp() const;
    int16_t getServerPort() const;

    uint64_t GetSessionId();

    bool Send(FramePtr frame);
    bool Send(const Message& msg);
    bool Send(const char* data, size_t size);

public slots:
    bool connectToServer(const QString& server, int16_t port);
    void disconnectFromServer();
    void onRecordOwnInfo(const ClientInfo& info);

private:
    void initSignals();
    void onReadyRead();

public:
    ClientInfo mInfo_;
    ClientInfo oInfo_;

private:
    QTcpSocket* tcp_{};
    std::atomic_uint64_t sessionId_{0};

private:
    miniBuffer buffer_;
    QString serverIp_;
    int16_t serverPort_;
};