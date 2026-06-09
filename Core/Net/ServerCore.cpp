#include "ServerCore.h"

#include <Protocol/Protocol.h>
#include <QDateTime>

ServerCore::ServerCore(QObject* parent) : QTcpServer(parent)
{
    connect(this, &QTcpServer::newConnection, this, &ServerCore::onNewConnection);
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
    qInfo() << "客户端连接成功：" << clientId;

    connect(socket, &QTcpSocket::readyRead, this, &ServerCore::onRead);

    auto clientInfo = std::make_shared<ClientInfo>();
    clientInfo->socket = socket;
    clientInfo->id = clientId;
    clientInfo->socket->setProperty("ID", clientId);
    clientInfo->socket->setProperty("INFO", QVariant::fromValue(clientInfo.get()));
    clientInfo->connectTime = QDateTime::currentMSecsSinceEpoch() / 1000;

    {
        QWriteLocker locker(&rwLock_);
        clientMap_[clientId.toStdString()] = clientInfo;
    }

    Message idMsg;
    idMsg.msType = MessageType::kMessageAnswerId;
    idMsg.to.clientId = clientId.toStdString();
    auto f = OneFrame::Create();
    f->data = serializeStruct(idMsg);
    sendData(f, socket);
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
        useFrame(frame, socket);
    }
}

void ServerCore::useFrame(FramePtr frame, QTcpSocket* socket)
{
    Message msg;
    deserializeStruct(frame->data, msg);
    qDebug() << "处理消息：" << static_cast<int>(msg.msType);

    switch (msg.msType) {
    case MessageType::kMessageAskClientList: {
        Message asg(msg);
        GetClientList(asg);
        auto f = OneFrame::Create(frame);
        asg.msType = MessageType::kMessageAnswerClientList;
        f->data = serializeStruct(asg);
        sendData(f, socket);
        break;
    }
    default:
        forwarData(frame, frame->to);
        break;
    }
}

bool ServerCore::forwarData(FramePtr frame, const std::string& otherId)
{
    std::shared_ptr<ClientInfo> o = nullptr;
    {
        QReadLocker locker(&rwLock_);
        o = clientMap_[otherId];
    }
    if (!o) {
        return false;
    }
    return sendData(frame, o->socket);
}

bool ServerCore::sendData(FramePtr frame, QTcpSocket* socket)
{
    auto data = Protocol::Pack(frame);
    return sendData(data.data(), data.size(), socket);
}

bool ServerCore::sendData(const char* data, int len, QTcpSocket* socket)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    return socket->write(data, len) == len;
}

void ServerCore::GetClientList(Message& msg)
{
    msg.clientList.clear();
    {
        QReadLocker locker(&rwLock_);
        for (auto& cli : clientMap_) {
            msg.clientList.push_back({cli->id.toStdString(), cli->name.toStdString()});
        }
    }
}
