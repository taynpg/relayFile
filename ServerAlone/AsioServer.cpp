#include "AsioServer.h"

#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

using asio::ip::tcp;

namespace {
constexpr std::size_t kMaxClients = 30;
constexpr int kHeartTimeoutSec = 5;
constexpr int kMonitorIntervalSec = 15;
}   // namespace

// ========================== Session ==========================

AsioServer::Session::Session(tcp::socket socket, AsioServer& server)
    : socket_(std::move(socket)), server_(server), readBuf_(1024 * 1024)
{
}

void AsioServer::Session::start()
{
    // socket 绑定的是独立 strand，所有操作经其 executor 串行化
    auto self = shared_from_this();
    asio::dispatch(socket_.get_executor(), [this, self] {
        std::error_code ec;
        socket_.set_option(tcp::socket::receive_buffer_size(1024 * 1024), ec);
        socket_.set_option(tcp::socket::send_buffer_size(1024 * 1024), ec);
        doRead();
    });
}

void AsioServer::Session::close()
{
    auto self = shared_from_this();
    // 可能被心跳定时器（其他线程）调用，投递到本连接 strand 执行
    asio::dispatch(socket_.get_executor(), [this, self] {
        if (closed_) {
            return;
        }
        closed_ = true;
        std::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    });
}

void AsioServer::Session::doRead()
{
    auto self = shared_from_this();
    socket_.async_read_some(asio::buffer(readBuf_), [this, self](const std::error_code& ec, std::size_t n) {
        if (ec) {
            // 对端断开 / 本地 close()，统一走移除流程
            server_.removeSession(self);
            return;
        }
        if (feed(readBuf_.data(), n)) {
            doRead();
        }
    });
}

bool AsioServer::Session::feed(const char* data, std::size_t n)
{
    auto self = shared_from_this();
    try {
        buffer_.Append(data, n);
        while (true) {
            auto frame = Protocol::UnPack(buffer_);
            if (frame == nullptr) {
                break;
            }
            if (frame->type == FrameType::kMsgType_Ask_Heart) {
                server_.onUseHeart(self, frame);
                continue;
            }
            server_.useFrame(frame, self);
        }
    } catch (const std::exception& e) {
        // 协议解析可失败，但绝不允许异常逃逸进 io_context（否则连接状态未定义）
        SPDLOG_WARN("onRead 解析帧异常（可能是旧客户端协议不兼容）: {}", e.what());
        SPDLOG_WARN("踢掉异常客户端，断开连接");
        close();
        server_.removeSession(self);
        return false;
    } catch (...) {
        SPDLOG_WARN("onRead 未知异常，踢掉客户端");
        close();
        server_.removeSession(self);
        return false;
    }
    return true;
}

void AsioServer::Session::sendData(FramePtr frame)
{
    // frame 跨 strand 共享只读；Pack 在目标连接自己的 strand 内进行，产出独立缓冲
    auto self = shared_from_this();
    asio::dispatch(socket_.get_executor(), [this, self, frame] { enqueue(Protocol::Pack(frame)); });
}

void AsioServer::Session::sendData(std::vector<char> data)
{
    // 公开入口可能来自其他连接的 strand，统一投递到本连接 strand
    auto self = shared_from_this();
    asio::dispatch(socket_.get_executor(),
                   [this, self, d = std::move(data)]() mutable { enqueue(std::move(d)); });
}

void AsioServer::Session::enqueue(std::vector<char> data)
{
    if (closed_ || !socket_.is_open()) {
        return;
    }
    // QTcpSocket::write 内部自带排队；asio 同一 socket 不允许并发 async_write，这里手动排队。
    const bool idle = !writing_ && writeQueue_.empty();
    writeQueue_.push_back(std::move(data));
    if (idle) {
        doWrite();
    }
}

void AsioServer::Session::doWrite()
{
    auto self = shared_from_this();
    writing_ = true;

    // 把当前所有积压帧聚合成一次 scatter-gather 写：等价 Qt 在一个发送窗口内连续泵送，
    // 避免“每帧等一次 IOCP 完成”造成的发送空洞。
    inflight_.clear();
    inflight_.reserve(writeQueue_.size());
    while (!writeQueue_.empty()) {
        inflight_.push_back(std::move(writeQueue_.front()));
        writeQueue_.pop_front();
    }
    inflightSeq_.clear();
    inflightSeq_.reserve(inflight_.size());
    for (const auto& v : inflight_) {
        inflightSeq_.push_back(asio::buffer(v));
    }

    asio::async_write(socket_, inflightSeq_, [this, self](const std::error_code& ec, std::size_t) {
        inflight_.clear();
        inflightSeq_.clear();
        if (ec) {
            writing_ = false;
            server_.removeSession(self);
            return;
        }
        if (!writeQueue_.empty()) {
            doWrite();
        } else {
            writing_ = false;
        }
    });
}

// ========================== AsioServer ==========================

AsioServer::AsioServer(asio::io_context& io)
    : io_(io), acceptor_(io), monitorTimer_(io)
{
    monitorTimer_.expires_after(std::chrono::seconds(kMonitorIntervalSec));
    monitorTimer_.async_wait([this](const std::error_code& ec) {
        if (!ec) {
            onMonitorHeart();
        }
    });
}

bool AsioServer::startListen(std::uint16_t port)
{
    return doListen(tcp::endpoint(tcp::v4(), port));
}

bool AsioServer::startListen(const std::string& hostName, std::uint16_t port)
{
    std::error_code ec;
    const auto addr = asio::ip::make_address(hostName, ec);
    if (ec) {
        return false;
    }
    return doListen(tcp::endpoint(addr, port));
}

void AsioServer::stopListen()
{
    std::error_code ec;
    acceptor_.close(ec);
}

bool AsioServer::doListen(const tcp::endpoint& ep)
{
    std::error_code ec;
    acceptor_.open(ep.protocol(), ec);
    if (ec) {
        return false;
    }
    acceptor_.set_option(tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        return false;
    }
    acceptor_.bind(ep, ec);
    if (ec) {
        return false;
    }
    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        return false;
    }
    doAccept();
    return true;
}

void AsioServer::doAccept()
{
    // 每个新连接独占一个 strand，使不同连接可在不同线程并行收发
    auto socket = std::make_shared<tcp::socket>(asio::make_strand(io_));
    acceptor_.async_accept(*socket, [this, socket](const std::error_code& ec) {
        if (ec) {
            if (acceptor_.is_open()) {
                SPDLOG_WARN("accept 失败: {}", ec.message());
                doAccept();
            }
            return;
        }
        onNewConnection(std::move(*socket));
        doAccept();
    });
}

void AsioServer::onNewConnection(tcp::socket socket)
{
    try {
        std::error_code ec;
        const auto ep = socket.remote_endpoint(ec);
        if (ec) {
            return;
        }
        const std::string clientId = ep.address().to_string() + ":" + std::to_string(ep.port());

        {
            std::unique_lock lock(mapMutex_);
            if (clientMap_.size() >= kMaxClients) {
                SPDLOG_WARN("客户端连接数已达上限，拒绝连接：{}", clientId);
                std::error_code ignore;
                socket.close(ignore);
                return;
            }
            SPDLOG_INFO("客户端连接成功：{}", clientId);

            auto session = std::make_shared<Session>(std::move(socket), *this);
            session->id = clientId;
            session->connectTime = nowSec();
            tempMap_[clientId] = session;
            session->start();
        }
    } catch (const std::exception& e) {
        SPDLOG_WARN("onNewConnection 异常: {}", e.what());
    } catch (...) {
        SPDLOG_WARN("onNewConnection 未知异常，已忽略");
    }
}

void AsioServer::onMonitorHeart()
{
    try {
        std::vector<SessionPtr> toKick;
        const std::int64_t deadline = nowSec() - kHeartTimeoutSec;

        // 快照超时的控制连接
        {
            std::shared_lock lock(mapMutex_);
            for (const auto& [id, cli] : tempMap_) {
                if (cli->connectTime < deadline) {
                    SPDLOG_WARN("客户端{}超时未发送心跳包", cli->id);
                    toKick.push_back(cli);
                }
            }
            for (const auto& [id, cli] : clientMap_) {
                if (cli->connectTime < deadline) {
                    SPDLOG_WARN("客户端{}超时未发送心跳包", cli->id);
                    toKick.push_back(cli);
                }
            }
        }
        // 关联的文件传输连接一并踢掉
        for (const auto& cli : toKick) {
            if (cli->transId.empty()) {
                continue;
            }
            std::shared_lock lock(mapMutex_);
            if (auto it = transMap_.find(cli->transId); it != transMap_.end()) {
                toKick.push_back(it->second);
            }
        }
        // close 后由读写错误回调统一走 removeSession（幂等）
        for (const auto& cli : toKick) {
            cli->close();
        }
    } catch (const std::exception& e) {
        SPDLOG_WARN("onMonitorHeart 异常: {}", e.what());
    } catch (...) {
        SPDLOG_WARN("onMonitorHeart 未知异常，已忽略");
    }

    monitorTimer_.expires_after(std::chrono::seconds(kMonitorIntervalSec));
    monitorTimer_.async_wait([this](const std::error_code& ec) {
        if (!ec) {
            onMonitorHeart();
        }
    });
}

void AsioServer::onUseHeart(const SessionPtr& session, FramePtr frame)
{
    Message heartMsg;
    deserializeStruct(frame->data, heartMsg);
    session->transId = heartMsg.comStr;
    session->connectTime = nowSec();
}

void AsioServer::useFrame(FramePtr frame, const SessionPtr& session)
{
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
        frame->from = session->id;
        idMsg.to.clientId = session->id;
        idMsg.to.clientName = sourceMsg.from.clientName;

        session->name = idMsg.to.clientName;
        auto f = OneFrame::Create(frame);
        f->type = static_cast<FrameType>(static_cast<std::uint16_t>(frame->type) + 1);
        f->data = serializeStruct(idMsg);
        session->sendData(f);

        {
            std::unique_lock lock(mapMutex_);
            if (auto it = tempMap_.find(session->id); it != tempMap_.end()) {
                auto moveCli = it->second;
                tempMap_.erase(it);
                if (frame->type == FrameType::kMsgType_Ask_ID) {
                    clientMap_.emplace(moveCli->id, moveCli);
                } else {
                    transMap_.emplace(moveCli->id, moveCli);
                }
            }
        }
        break;
    }
    case FrameType::kMsgType_Ask_ClientList: {
        Message asg(msg);
        getClientList(asg);
        auto f = OneFrame::Create(frame);
        f->type = FrameType::kMsgType_Answer_ClientList;
        f->data = serializeStruct(asg);
        session->sendData(f);
        break;
    }
    default:
        forwarData(frame, frame->to);
        break;
    }
}

void AsioServer::removeSession(const SessionPtr& session)
{
    if (session->removed_) {
        return;
    }
    session->removed_ = true;
    session->close();

    // 与原 onClearClient 一致：clientMap_ -> tempMap_ -> transMap_，命中即停
    bool isRemove = false;
    {
        std::unique_lock lock(mapMutex_);
        if (clientMap_.erase(session->id) > 0) {
            SPDLOG_DEBUG("clientMap_移除客户端：{}", session->id);
            isRemove = true;
        }
        if (!isRemove && tempMap_.erase(session->id) > 0) {
            SPDLOG_DEBUG("tempMap_移除客户端：{}", session->id);
            isRemove = true;
        }
        if (!isRemove && transMap_.erase(session->id) > 0) {
            SPDLOG_DEBUG("transMap_移除客户端：{}", session->id);
        }
    }

    // 通知在线客户端，有变动。快照出当前控制连接，锁外逐个发送（避免持锁跨 strand）。
    Message tellMsg;
    getClientList(tellMsg);
    auto f = OneFrame::Create();
    f->type = FrameType::kMsgType_Notify_ClientList;
    f->data = serializeStruct(tellMsg);

    std::vector<SessionPtr> snapshot;
    {
        std::shared_lock lock(mapMutex_);
        snapshot.reserve(clientMap_.size());
        for (const auto& [id, cli] : clientMap_) {
            snapshot.push_back(cli);
        }
    }
    for (const auto& cli : snapshot) {
        cli->sendData(f);
    }
}

void AsioServer::forwarData(FramePtr frame, const std::string& otherId)
{
    SessionPtr target;
    {
        std::shared_lock lock(mapMutex_);
        auto it = clientMap_.find(otherId);
        if (it == clientMap_.end()) {
            SPDLOG_WARN("{}转发到:{}，但是不存在。", frame->from, otherId);
            return;
        }
        target = it->second;
    }
    target->sendData(frame);
}

void AsioServer::forwarFileData(FramePtr frame, const std::string& otherId)
{
    SessionPtr target;
    {
        std::shared_lock lock(mapMutex_);
        // 注意：原版用 transMap_[otherId] 查找会插入空元素，这里用 find
        auto it = transMap_.find(otherId);
        if (it == transMap_.end()) {
            return;
        }
        target = it->second;
    }
    target->sendData(frame);
}

void AsioServer::getClientList(Message& msg)
{
    std::shared_lock lock(mapMutex_);
    msg.clientList.clear();
    for (const auto& [id, cli] : clientMap_) {
        msg.clientList.push_back({cli->id, cli->name});
    }
}

std::int64_t AsioServer::nowSec()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
