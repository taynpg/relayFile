#pragma once

#include <Protocol/Message.h>
#include <Protocol/Protocol.h>
#include <Protocol/Serialize.hpp>
#include <QMap>
#include <QReadWriteLock>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <Utils/miniUtil.h>

class ServerCore : public QTcpServer
{
    Q_OBJECT
public:
    explicit ServerCore(QObject* parent = nullptr);
    ~ServerCore();

public:
    bool startListen(quint16 port);
    bool startListen(const QString& hostName, quint16 port);
    void stopListen();

private:
    struct ClientInfo {
        QTcpSocket* socket;
        QString id;
        QString name;
        QString uuid;
        qint64 connectTime;
        miniBuffer buffer;
    };

private slots:
    void onRead();
    void onNewConnection();
    void onClearClient();

private:
    void GetClientList(Message& msg);

    void useFrame(FramePtr frame, QTcpSocket* socket, ClientInfo* cli);
    bool forwarData(FramePtr frame, const std::string& otherId);
    bool sendData(FramePtr frame, QTcpSocket* socket);
    bool sendData(const char* data, int len, QTcpSocket* socket);

private:
    QReadWriteLock rwLock_;
    QReadWriteLock tempLock_;
    QReadWriteLock transLock_;
    QTimer* monitorTimer_{};
    QMap<std::string, std::shared_ptr<ClientInfo>> clientMap_;
    QMap<std::string, std::shared_ptr<ClientInfo>> tempMap_;
    QMap<std::string, std::shared_ptr<ClientInfo>> transMap_;
};