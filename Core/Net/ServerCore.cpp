#include "ServerCore.h"

#include <Protocol/Protocol.h>
#include <QDateTime>

#include "Utils/Common.h"

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
    connect(socket, &QTcpSocket::disconnected, this, &ServerCore::onClearClient);

    auto clientInfo = std::make_shared<ClientInfo>();
    clientInfo->socket = socket;
    clientInfo->id = clientId;
    clientInfo->socket->setProperty("ID", clientId);
    clientInfo->socket->setProperty("INFO", QVariant::fromValue(clientInfo.get()));
    clientInfo->connectTime = QDateTime::currentMSecsSinceEpoch() / 1000;

    {
        QWriteLocker locker(&tempLock_);
        tempMap_[clientId.toStdString()] = clientInfo;
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
        useFrame(frame, socket, cli);
    }
}

void ServerCore::useFrame(FramePtr frame, QTcpSocket* socket, ClientInfo* cli)
{
    qDebug() << "处理消息：" << static_cast<int>(frame->type);

    // 文件帧不处理
    if (GIsChuckAckFrame(frame)) {
        forwarFileData(frame, frame->to);
        return;
    }
    if (GIsFileMessageFrame(frame)) {
        forwarData(frame, frame->to);
        return;
    }
    Message msg;
    deserializeStruct(frame->data, msg);

    switch (frame->type) {
    case FrameType::kFileType_Request_ID:
    case FrameType::kMsgType_Ask_ID: {
        Message sourceMsg;
        deserializeStruct(frame->data, sourceMsg);
        Message idMsg(sourceMsg);
        idMsg.to.clientId = cli->id.toStdString();
        idMsg.to.clientName = sourceMsg.from.clientName;
        cli->name = QString::fromStdString(idMsg.to.clientName);
        auto f = OneFrame::Create();
        f->type = static_cast<FrameType>(static_cast<std::uint16_t>(frame->type) + 1);
        f->data = serializeStruct(idMsg);
        sendData(f, socket);
        std::shared_ptr<ClientInfo> moveCli = nullptr;
        {
            QWriteLocker locker(&tempLock_);
            if (tempMap_.contains(cli->id.toStdString())) {
                moveCli = tempMap_[cli->id.toStdString()];
                tempMap_.remove(cli->id.toStdString());
            }
        }
        if (moveCli) {
            if (frame->type == FrameType::kMsgType_Ask_ID) {
                QWriteLocker locker(&rwLock_);
                if (!clientMap_.contains(moveCli->id.toStdString())) {
                    clientMap_[moveCli->id.toStdString()] = moveCli;
                }
            } else {
                QWriteLocker locker(&rwLock_);
                if (!transMap_.contains(moveCli->id.toStdString())) {
                    transMap_[moveCli->id.toStdString()] = moveCli;
                }
            }
        }
        break;
    }
    case FrameType::kMsgType_Ask_ClientList: {
        Message asg(msg);
        GetClientList(asg);
        auto f = OneFrame::Create(frame);
        f->type = FrameType::kMsgType_Answer_ClientList;
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

bool ServerCore::forwarFileData(FramePtr frame, const std::string& otherId)
{
    std::shared_ptr<ClientInfo> o = nullptr;
    {
        QReadLocker locker(&transLock_);
        o = transMap_[otherId];
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

void ServerCore::onClearClient()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }
    auto* cli = socket->property("INFO").value<ClientInfo*>();
    if (!cli) {
        return;
    }
    qWarning() << "客户端连接关闭：" << cli->id;
    socket->disconnectFromHost();
    socket->close();

    bool isRemove = false;
    {
        QWriteLocker locker(&rwLock_);
        if (clientMap_.contains(cli->uuid.toStdString())) {
            clientMap_.remove(cli->uuid.toStdString());
            isRemove = true;
        }
    }
    if (!isRemove) {
        QWriteLocker locker(&tempLock_);
        if (tempMap_.contains(cli->id.toStdString())) {
            tempMap_.remove(cli->id.toStdString());
            isRemove = true;
        }
    }
    if (!isRemove) {
        QWriteLocker locker(&transLock_);
        if (transMap_.contains(cli->id.toStdString())) {
            transMap_.remove(cli->id.toStdString());
        }
    }
    socket->deleteLater();
}
