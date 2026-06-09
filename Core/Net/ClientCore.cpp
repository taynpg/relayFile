#include "ClientCore.h"

#include "ControlSession.h"
#include "FileSession.h"
#include "Protocol/Serialize.hpp"

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

void ClientCore::disconnectFromServer()
{
    if (tcp_->state() == QAbstractSocket::ConnectedState) {
        tcp_->disconnectFromHost();
        tcp_->close();
        qWarning() << "断开服务器连接。";
    }
    emit signalDisconnected();
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

void ClientCore::setClientInfo(const ClientInfo& oInfo)
{
    qInfo() << "设置客户端信息：" << oInfo.clientId << oInfo.clientName;
    oInfo_ = oInfo;
}

ClientInfo ClientCore::getClientInfo() const
{
    return oInfo_;
}

QString ClientCore::getClientFullName() const
{
    QString name = QString("%1,%2").arg(oInfo_.clientId).arg(oInfo_.clientName);
    return name;
}

ClientInfo ClientCore::getSelfInfo() const
{
    return mInfo_;
}

bool ClientCore::Send(const Message& msg)
{
    auto frame = OneFrame::Create();
    frame->from = mInfo_.clientId;
    frame->to = oInfo_.clientId;
    frame->data = serializeStruct(msg);
    frame->sessionId = GetSessionId();
    return Send(frame);
}

bool ClientCore::Send(FramePtr frame)
{
    frame->from = mInfo_.clientId;
    frame->to = oInfo_.clientId;
    auto data = Protocol::Pack(frame);
    return Send(data.data(), data.size());
}

template <typename Callback> bool ClientCore::SendCall(FramePtr frame, Callback callback)
{
    auto sid = GetSessionId();
    frame->sessionId = sid;

    if (!Send(frame)) {
        return false;
    }

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);

    connect(timer, &QTimer::timeout, this, [this, sid]() {
        QMutexLocker locker(&waitLock_);
        auto it = waitFrame_.find(sid);
        if (it != waitFrame_.end()) {
            auto& waiter = *it.value();
            switch (waiter.callType) {
            case CallType::CT_Message: {
                MessagePtr msg = nullptr;
                waiter.call(msg);
                break;
            }
            case CallType::CT_Frame: {
                FramePtr f = nullptr;
                waiter.call(f);
                break;
            }
            }
            waiter.timer->deleteLater();
            waitFrame_.erase(it);
        }
    });

    auto waiter = std::make_shared<WaiteFrame>();
    waiter->sessionId = sid;
    using ArgType = typename function_traits<Callback>::argument_type;
    waiter->call = [cb = std::move(callback)](std::any arg) { cb(std::any_cast<ArgType>(arg)); };
    waiter->timer = timer;

    if constexpr (std::is_same_v<ArgType, MessagePtr>) {
        waiter->callType = CallType::CT_Message;
    } else if constexpr (std::is_same_v<ArgType, FramePtr>) {
        waiter->callType = CallType::CT_Frame;
    } else {
        static_assert(!sizeof(ArgType), "Unsupported callback argument type");
    }

    {
        QMutexLocker locker(&waitLock_);
        waitFrame_[sid] = waiter;
    }

    timer->start(5000);
    return true;
}

bool ClientCore::SendWithCall(const Message& msg, std::function<void(FramePtr)> callback)
{
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->to = oInfo_.clientId;
    return SendCall(frame, callback);
}

bool ClientCore::SendWithCall(FramePtr frame, std::function<void(MessagePtr)> callback)
{
    return SendCall(frame, std::move(callback));
}

bool ClientCore::SendWithCall(const Message& msg, std::function<void(MessagePtr)> callback)
{
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->to = oInfo_.clientId;
    return SendCall(frame, std::move(callback));
}

bool ClientCore::SendWithCall(FramePtr frame, std::function<void(FramePtr)> callback)
{
    return SendCall(frame, std::move(callback));
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