#pragma once

#include <Protocol/Message.h>
#include <Protocol/Protocol.h>
#include <Protocol/Serialize.hpp>
#include <Utils/miniUtil.h>

#include <array>
#include <asio.hpp>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief ServerCore 的无 Qt asio 实现。
 *
 * 线程模型：单线程 io_context，所有回调串行执行，
 * 因此原 Qt 版的三把 QReadWriteLock 在这里不需要。
 * 业务行为与 Net/ServerCore.cpp 保持 1:1。
 */
class AsioServer
{
public:
    explicit AsioServer(asio::io_context& io);
    ~AsioServer() = default;

    bool startListen(std::uint16_t port);
    bool startListen(const std::string& hostName, std::uint16_t port);
    void stopListen();

private:
    class Session : public std::enable_shared_from_this<Session>
    {
        friend class AsioServer;

    public:
        Session(asio::ip::tcp::socket socket, AsioServer& server);
        void start();
        void close();

        bool sendData(FramePtr frame);
        bool sendData(std::vector<char> data);

        // 对应原 ClientInfo（Qt 版通过 socket->setProperty 挂载，这里直接内聚在会话里）
        std::string id;   // "ip:port"
        std::string name;
        std::string transId;
        std::int64_t connectTime{0};   // 秒

    private:
        void doRead();
        void doWrite();

        asio::ip::tcp::socket socket_;
        AsioServer& server_;
        miniBuffer buffer_;
        std::array<char, 64 * 1024> readBuf_{};
        std::deque<std::vector<char>> writeQueue_;
        bool closed_{false};
        bool removed_{false};
    };
    using SessionPtr = std::shared_ptr<Session>;

    bool doListen(const asio::ip::tcp::endpoint& ep);
    void doAccept();
    void onNewConnection(asio::ip::tcp::socket socket);
    void onMonitorHeart();
    void onUseHeart(const SessionPtr& session, FramePtr frame);
    void useFrame(FramePtr frame, const SessionPtr& session);
    void removeSession(const SessionPtr& session);
    void getClientList(Message& msg);
    bool forwarData(FramePtr frame, const std::string& otherId);
    bool forwarFileData(FramePtr frame, const std::string& otherId);

    static std::int64_t nowSec();

    asio::io_context& io_;
    asio::ip::tcp::acceptor acceptor_;
    asio::steady_timer monitorTimer_;
    std::unordered_map<std::string, SessionPtr> clientMap_;
    std::unordered_map<std::string, SessionPtr> tempMap_;
    std::unordered_map<std::string, SessionPtr> transMap_;
};
