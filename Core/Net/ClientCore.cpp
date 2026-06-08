#include "ClientCore.h"

#include "ControlSession.h"
#include "FileSession.h"

ClientCore::ClientCore(QObject* parent) : QObject(parent)
{
}

ClientCore::~ClientCore()
{
}

void ClientCore::instance()
{
    tcp_ = new QTcpSocket(this);
    initSignals();
}

ClientCore* ClientCore::ceateInstance(QObject* parent, ClientType clientType)
{
    ClientCore* clientCore = nullptr;
    switch (clientType) {
    case ClientType::ControlSession:
        clientCore = new ControlSession(parent);
        break;
    case ClientType::FileSession:
        clientCore = new FileSession(parent);
        break;
    default:
        clientCore = nullptr;
        break;
    }
    if (clientCore) {
        clientCore->setClientType(clientType);
    }
    return clientCore;
}

void ClientCore::initSignals()
{
    connect(tcp_, &QTcpSocket::readyRead, this, &ClientCore::onReadyRead);
}

void ClientCore::setClientType(ClientType clientType)
{
    clientType_ = clientType;
}

ClientType ClientCore::getClientType() const
{
    return clientType_;
}

bool ClientCore::connectToServer(const QString& server, int16_t port)
{
        if (tcp_->state() == QAbstractSocket::ConnectedState) {
            return true;
        }

    qInfo() << "尝试连接服务器：" << server << ":" << port;
    emit signalConnectting();
    tcp_->connectToHost(server, port);
    if (!tcp_->waitForConnected(3000)) {
        emit signalDisconnected();
        qWarning() << "连接服务器失败:" << tcp_->errorString();
        return false;
    }
    emit signalConnected();
    qInfo() << "连接服务器成功=>" << server << ":" << port;
    return true;
}

void ClientCore::onReadyRead()
{
    QByteArray data = tcp_->readAll();
    buffer_.Append(data.data(), data.size());
    while (true) {
        auto frame = Protocol::UnPack(buffer_);
        if (!frame) {
            break;
        }
        // 处理帧
        handleFrame(frame);
    }
}

void ClientCore::baseHandleFrame(FramePtr frame)
{
}

void ClientCore::setClientInfo(const ClientInfo& oInfo)
{
    oInfo_ = oInfo;
}

bool ClientCore::Send(FramePtr frame)
{
    auto data = Protocol::Pack(frame);
    return Send(data.data(), data.size());
}

bool ClientCore::Send(const char* data, size_t size)
{
    if (tcp_->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    return tcp_->write(data, size) == size;
}
