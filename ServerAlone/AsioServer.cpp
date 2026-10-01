#include "AsioServer.h"

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
    : socket_(std::move(socket)), server_(server)
{
}

void AsioServer::Session::start()
{
    doRead();
}

void AsioServer::Session::close()
{
    if (closed_) {
        return;
    }
    closed_ = true;
    std::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);
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
        try {
            buffer_.Append(readBuf_.data(), n);
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
            return;
        } catch (...) {
            SPDLOG_WARN("onRead 未知异常，踢掉客户端");
            close();
            server_.removeSession(self);
            return;
        }
        doRead();
    });
}

bool AsioServer::Session::sendData(FramePtr frame)
{
    return sendData(Protocol::Pack(frame));
}

bool AsioServer::Session::sendData(std::vector<char> data)
{
    if (closed_ || !socket_.is_open()) {
        return false;
    }
    // QTcpSocket::write 内部自带排队；asio 同一 socket 不允许并发 async_write，这里手动排队。
    const bool idle = writeQueue_.empty();
    writeQueue_.push_back(std::move(data));
    if (idle) {
        doWrite();
    }
    return true;
}

void AsioServer::Session::doWrite()
{
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(writeQueue_.front()),
                      [this, self](const std::error_code& ec, std::size_t) {
                          if (ec) {
                              server_.removeSession(self);
                              return;
                          }
                          writeQueue_.pop_front();
                          if (!writeQueue_.empty()) {
                              doWrite();
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
    acceptor_.async_accept([this](const std::error_code& ec, tcp::socket socket) {
        if (ec) {
            if (acceptor_.is_open()) {
                SPDLOG_WARN("accept 失败: {}", ec.message());
                doAccept();
            }
            return;
        }
        onNewConnection(std::move(socket));
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

        if (clientMap_.size() >= kMaxClients) {
            SPDLOG_WARN("客户端连接数已达上限，拒绝连接：{}", clientId);
            socket.close(ec);
            return;
        }
        SPDLOG_INFO("客户端连接成功：{}", clientId);

        auto session = std::make_shared<Session>(std::move(socket), *this);
        session->id = clientId;
        session->connectTime = nowSec();
        tempMap_[clientId] = session;
        session->start();
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
        // 关联的文件传输连接一并踢掉
        for (const auto& cli : toKick) {
            if (cli->transId.empty()) {
                continue;
            }
            auto it = transMap_.find(cli->transId);
            if (it != transMap_.end()) {
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

        auto it = tempMap_.find(session->id);
        if (it != tempMap_.end()) {
            auto moveCli = it->second;
            tempMap_.erase(it);
            if (frame->type == FrameType::kMsgType_Ask_ID) {
                clientMap_.emplace(moveCli->id, moveCli);
            } else {
                transMap_.emplace(moveCli->id, moveCli);
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

    // 通知在线客户端，有变动。
    Message tellMsg;
    getClientList(tellMsg);

    auto f = OneFrame::Create();
    f->type = FrameType::kMsgType_Notify_ClientList;
    f->data = serializeStruct(tellMsg);

    for (const auto& [id, cli] : clientMap_) {
        cli->sendData(f);
    }
}

bool AsioServer::forwarData(FramePtr frame, const std::string& otherId)
{
    auto it = clientMap_.find(otherId);
    if (it == clientMap_.end()) {
        SPDLOG_WARN("{}转发到:{}，但是不存在。", frame->from, otherId);
        return false;
    }
    return it->second->sendData(frame);
}

bool AsioServer::forwarFileData(FramePtr frame, const std::string& otherId)
{
    // 注意：原版用 transMap_[otherId] 查找会插入空元素，这里用 find
    auto it = transMap_.find(otherId);
    if (it == transMap_.end()) {
        return false;
    }
    return it->second->sendData(frame);
}

void AsioServer::getClientList(Message& msg)
{
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
