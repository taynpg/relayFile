#pragma once

#include <QMap>
#include <QReadWriteLock>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <Utils/miniUtil.h>
#include <Protocol/Protocol.h>

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
        qint64 connectTime;
        miniBuffer buffer;
    };

private:
    void onNewConnection();
    void onRead();
    void useFrame(FramePtr frame);

private:
    QReadWriteLock rwLock_;
    QTimer* monitorTimer_{};
    QMap<QString, std::shared_ptr<ClientInfo>> clientMap_;
};