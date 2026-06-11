#include "ClientCore.h"

#include "CoreDefine.hpp"
#include "Protocol/Serialize.hpp"

ClientCore::ClientCore(QObject* parent) : QObject(parent)
{
}

ClientCore::~ClientCore()
{
}

void ClientCore::Quit()
{
}

void ClientCore::instance()
{
    tcp_ = new QTcpSocket(this);
    initSignals();
}

void ClientCore::initSignals()
{
    connect(tcp_, &QTcpSocket::readyRead, this, &ClientCore::onReadyRead);
}

void ClientCore::disconnectFromServer()
{
    if (tcp_->state() == QAbstractSocket::ConnectedState) {
        tcp_->disconnectFromHost();
        tcp_->close();
        qWarning() << "断开服务器连接。";
    }
    emit signalDisconnected();
}

void ClientCore::onRecordOwnInfo(const ClientInfo& info)
{
    mInfo_ = info;
}

bool ClientCore::isConnected() const
{
    return tcp_->state() == QAbstractSocket::ConnectedState;
}

QString ClientCore::getServerIp() const
{
    return serverIp_;
}

int16_t ClientCore::getServerPort() const
{
    return serverPort_;
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
    serverIp_ = server;
    serverPort_ = port;
    qInfo() << "连接服务器成功=>" << server << ":" << port;
    emit signalRequestAskOwnID();
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
        emit signalDeliverFrame(frame);
    }
}

void ClientCore::setOtherClientInfo(const ClientInfo& oInfo)
{
    qInfo() << "设置客户端信息：" << QString::fromStdString(oInfo.clientId) << QString::fromStdString(oInfo.clientName);
    oInfo_ = oInfo;
}

ClientInfo ClientCore::getOtherClientInfo() const
{
    return oInfo_;
}
ClientInfo ClientCore::getOwnClientInfo() const
{
    return mInfo_;
}

QString ClientCore::getClientFullName() const
{
    QString name = QString("%1,%2").arg(oInfo_.clientId).arg(oInfo_.clientName);
    return name;
}

void ClientCore::setIsControl(bool isControl)
{
    isControl_ = isControl;
}

bool ClientCore::isControl() const
{
    return isControl_;
}

bool ClientCore::Send(const Message& msg)
{
    auto frame = OneFrame::Create();
    if (isControl_) {
        frame->from = mInfo_.clientId;
        frame->to = oInfo_.clientId;
    }
    frame->data = serializeStruct(msg);
    frame->sessionId = GetSessionId();
    return Send(frame);
}

bool ClientCore::Send(FramePtr frame)
{
    if (isControl_) {
        frame->from = mInfo_.clientId;
        frame->to = oInfo_.clientId;
    }
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

uint64_t ClientCore::GetSessionId()
{
    return ++sessionId_;
}

ClientWorker::ClientWorker(ClientCore* core, QObject* parent) : QThread(parent), core_(core)
{
}

ClientWorker::~ClientWorker()
{
}

void ClientWorker::run()
{
    core_->instance();
    exec();
}