#include "ServerCore.h"

#include <Protocol/Protocol.h>
#include <QDateTime>


ServerCore::ServerCore(QObject* parent) : QTcpServer(parent)
{
}

ServerCore::~ServerCore()
{
}

bool ServerCore::startListen(quint16 port)
{
    if (!listen(QHostAddress::Any, port)) {
        return false;
    }
    return true;
}

bool ServerCore::startListen(const QString& hostName, quint16 port)
{
    if (!listen(QHostAddress(hostName), port)) {
        return false;
    }
    return true;
}

void ServerCore::stopListen()
{
    close();
}

void ServerCore::onNewConnection()
{
    auto* socket = nextPendingConnection();
    QHostAddress peerAddress = socket->peerAddress();
    quint32 ipv4 = peerAddress.toIPv4Address();
    QString ipStr = QHostAddress(ipv4).toString();
    QString clientId = QString("%1:%2").arg(ipStr).arg(socket->peerPort());

    if (clientMap_.size() >= 30) {
        qWarning() << "客户端连接数已达上限，拒绝连接：" << clientId;
        socket->disconnectFromHost();
        return;
    }

    auto clientInfo = std::make_shared<ClientInfo>();
    clientInfo->socket = socket;
    clientInfo->id = clientId;
    clientInfo->socket->setProperty("ID", clientId);
    clientInfo->socket->setProperty("INFO", QVariant::fromValue(clientInfo.get()));
    clientInfo->connectTime = QDateTime::currentMSecsSinceEpoch() / 1000;

    {
        QWriteLocker locker(&rwLock_);
        clientMap_[clientId] = clientInfo;
    }
}

void ServerCore::onRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    auto* cli = socket->property("INFO").value<ClientInfo*>();
    auto data = socket->readAll();
    cli->buffer.Append(data.data(), data.size());
    while (true) {
        auto frame = Protocol::UnPack(cli->buffer);
        if (frame == nullptr) {
            break;
        }
        useFrame(frame);
    }
}

void ServerCore::useFrame(FramePtr frame)
{
}