#include "ClientCore.h"

#include "CoreDefine.hpp"
#include "Crypto/CryptoHelper.h"
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
    connect(tcp_, &QTcpSocket::disconnected, this, [this]() {
        oInfo_.clientId.clear();
        oInfo_.clientName.clear();
        mInfo_.clientId.clear();
        mInfo_.clientName.clear();
        emit signalDisconnected();
    });
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
        if (!CryptoHelper::instance().decryptFrame(frame)) {
            qWarning() << "帧解密失败（未配置密钥或校验失败），丢弃, type=" << static_cast<int>(frame->type);
            continue;
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
    QString name = QString("%1,%2").arg(QString::fromStdString(oInfo_.clientId)).arg(QString::fromStdString(oInfo_.clientName));
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
    frame->data = serializeStruct(msg);
    return Send(frame);
}

// 判断帧是否需要加密。只加密文件传输相关帧（含数据块）：
// 服务器对控制帧在转发前会先反序列化 data（ServerCore::useFrame / AsioServer），
// 加密会导致服务器解析失败踢掉连接；文件帧服务器不解析 data、纯按头部转发，可安全加密。
static bool GIsEncryptableFrame(FramePtr frame)
{
    return GIsFileMessageFrame(frame) || GIsChuckAckFrame(frame);
}

bool ClientCore::Send(FramePtr frame)
{
    if (isControl_) {
        if (frame->from.empty()) {
            frame->from = mInfo_.clientId;
        }
        if (frame->to.empty()) {
            frame->to = oInfo_.clientId;
        }
        if (frame->sessionId == 0) {
            frame->sessionId = GetSessionId();
        }
    }
    if (GIsEncryptableFrame(frame)) {
        CryptoHelper::instance().encryptFrame(frame);
    }
    auto data = Protocol::Pack(frame);
    return Send(data.data(), data.size());
}

bool ClientCore::Send(const char* data, size_t size)
{
    if (tcp_->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "ClientCore::Send 失败：连接已断开, size=" << size;
        return false;
    }
    qint64 written = tcp_->write(data, static_cast<qint64>(size));
    if (written != static_cast<qint64>(size)) {
        qWarning() << "ClientCore::Send 写入不完整: expected=" << size << "written=" << written
                   << "state=" << static_cast<int>(tcp_->state());
        return false;
    }
    return true;
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
