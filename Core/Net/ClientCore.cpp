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
        connected_.store(false);
        {
            QMutexLocker locker(&infoLock_);
            oInfo_.clientId.clear();
            oInfo_.clientName.clear();
            mInfo_.clientId.clear();
            mInfo_.clientName.clear();
        }
        emit signalDisconnected();
    });
}

void ClientCore::disconnectFromServer()
{
    connected_.store(false);
    if (tcp_->state() == QAbstractSocket::ConnectedState) {
        tcp_->disconnectFromHost();
        tcp_->close();
        qWarning() << "断开服务器连接。";
    }
    emit signalDisconnected();
}

void ClientCore::onRecordOwnInfo(const ClientInfo& info)
{
    QMutexLocker locker(&infoLock_);
    mInfo_ = info;
}

bool ClientCore::isConnected() const
{
    // 跨线程调用：tcp_ 可能还没被 worker 线程创建（instance() 尚未执行），
    // 而且 QTcpSocket::state() 不是线程安全的，统一看 connected_ 快照
    return connected_.load() && tcp_ != nullptr;
}

QString ClientCore::getServerIp() const
{
    QMutexLocker locker(&infoLock_);
    return serverIp_;
}

int16_t ClientCore::getServerPort() const
{
    QMutexLocker locker(&infoLock_);
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
    connected_.store(true);
    emit signalConnected();
    {
        QMutexLocker locker(&infoLock_);
        serverIp_ = server;
        serverPort_ = port;
    }
    qInfo() << "连接服务器成功=>" << server << ":" << port;
    emit signalRequestAskOwnID();
    return true;
}

void ClientCore::onReadyRead()
{
    QByteArray data = tcp_->readAll();
    buffer_.Append(data.data(), data.size());
    while (true) {
        FramePtr frame;
        UnPackResult ret = UnPackResult::NeedMoreData;
        try {
            ret = Protocol::UnPack(buffer_, frame);
        } catch (const std::exception& e) {
            // 帧头解析异常（畸形长度/截断）：丢弃当前缓冲，避免同一段坏数据反复抛
            qWarning() << "解析帧异常，清空接收缓冲:" << e.what();
            buffer_.RemoveOf(0, buffer_.Size());
            break;
        }
        if (ret == UnPackResult::NeedMoreData) {
            break;
        }
        if (ret == UnPackResult::ProtocolError) {
            qWarning() << "协议错误（版本不符或帧损坏），断开连接";
            tcp_->abort();
            return;
        }
        try {
            if (!CryptoHelper::instance().decryptFrame(frame)) {
                qWarning() << "帧解密失败（未配置密钥或校验失败），丢弃, type=" << static_cast<int>(frame->type);
                continue;
            }
            emit signalDeliverFrame(frame);
        } catch (const std::exception& e) {
            // cereal 反序列化遇到截断/畸形数据会抛；绝不能让异常穿过 Qt 事件循环
            qWarning() << "处理帧异常（已丢弃）, type=" << static_cast<int>(frame->type) << "err:" << e.what();
        } catch (...) {
            qWarning() << "处理帧未知异常（已丢弃）, type=" << static_cast<int>(frame->type);
        }
    }
}

void ClientCore::setOtherClientInfo(const ClientInfo& oInfo)
{
    qInfo() << "设置客户端信息：" << QString::fromStdString(oInfo.clientId) << QString::fromStdString(oInfo.clientName);
    QMutexLocker locker(&infoLock_);
    oInfo_ = oInfo;
}

ClientInfo ClientCore::getOtherClientInfo() const
{
    QMutexLocker locker(&infoLock_);
    return oInfo_;
}
ClientInfo ClientCore::getOwnClientInfo() const
{
    QMutexLocker locker(&infoLock_);
    return mInfo_;
}

QString ClientCore::getClientFullName() const
{
    QMutexLocker locker(&infoLock_);
    return QString("%1,%2")
        .arg(QString::fromStdString(oInfo_.clientId))
        .arg(QString::fromStdString(oInfo_.clientName));
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
        ClientInfo own;
        ClientInfo other;
        {
            QMutexLocker locker(&infoLock_);
            own = mInfo_;
            other = oInfo_;
        }
        if (frame->from.empty()) {
            frame->from = own.clientId;
        }
        if (frame->to.empty()) {
            frame->to = other.clientId;
        }
        if (frame->sessionId == 0) {
            frame->sessionId = GetSessionId();
        }
    }
    if (GIsEncryptableFrame(frame)) {
        CryptoHelper::instance().encryptFrame(frame);
    }
    auto data = Protocol::Pack(frame);
    if (data.empty()) {
        qWarning() << "ClientCore::Send 失败：帧序列化失败（路由字段过长或帧超过上限）, type="
                   << static_cast<int>(frame->type);
        return false;
    }
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
