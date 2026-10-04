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
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief ServerCore 的无 Qt asio 实现（多线程版）。
 *
 * 线程模型：io_context 由多个线程 run；每个 Session 独占一个 strand，
 * 保证单连接的读写回调不并发。三张连接表用 shared_mutex 保护：
 * 转发是高频读（共享锁），连接注册/移除是低频写（独占锁）。
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
        void close();   // 线程安全：可从任意 strand/线程调用

        void sendData(FramePtr frame);   // 线程安全
        void sendData(std::vector<char> data);

        // 对应原 ClientInfo（Qt 版通过 socket->setProperty 挂载，这里直接内聚在会话里）
        std::string id;   // "ip:port"
        std::string name;
        std::string transId;
        std::int64_t connectTime{0};   // 秒，仅本连接 strand 与心跳快照访问

    private:
        void doRead();
        void doWrite();
        // 入队（调用时必须已在本连接 strand 上）
        void enqueue(std::vector<char> data);
        // 处理一段到达的字节流；返回 false 表示已因协议异常关闭本连接
        bool feed(const char* data, std::size_t n);

        asio::ip::tcp::socket socket_;
        AsioServer& server_;
        miniBuffer buffer_;
        // 大块读缓冲：1MB，减少大帧的读取完成次数
        std::vector<char> readBuf_;
        std::deque<std::vector<char>> writeQueue_;
        // 一次 scatter-gather 写在飞期间持有的帧与其 buffer 视图（生命周期覆盖异步写）
        std::vector<std::vector<char>> inflight_;
        std::vector<asio::const_buffer> inflightSeq_;
        bool writing_{false};
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
    void forwarData(FramePtr frame, const std::string& otherId);
    void forwarFileData(FramePtr frame, const std::string& otherId);

    static std::int64_t nowSec();

    asio::io_context& io_;
    asio::ip::tcp::acceptor acceptor_;
    asio::steady_timer monitorTimer_;

    // 连接表：转发高频读、注册/移除低频写
    std::shared_mutex mapMutex_;
    std::unordered_map<std::string, SessionPtr> clientMap_;
    std::unordered_map<std::string, SessionPtr> tempMap_;
    std::unordered_map<std::string, SessionPtr> transMap_;
};
