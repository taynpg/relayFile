#include <Net/ClientHelper.h>
#include <Net/ServerCore.h>
#include <Protocol/Serialize.hpp>
#include <File/FileDir.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <Utils/Common.h>
#include <Utils/Logger.h>
#include <Utils/OwnLogger.h>
#include <chrono>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <relayFileVersion.h>

// 传输性能测试工具（滑动窗口吞吐基准）
// 用法:
//   relayFileTransTest server                       启动中继服务器(端口 9108)
//   relayFileTransTest sender <本地文件> <远端完整路径>   发送并输出吞吐
//   relayFileTransTest receiver <完整路径> <期望大小>    接收端，文件收齐后退出

static constexpr int16_t kPort = 9108;
static constexpr int kWaitMs = 120000;

struct TestClient {
    std::shared_ptr<ControlSession> control;
    std::shared_ptr<FileSession> file;
    std::shared_ptr<DoubleLinker> linker;

    void setup()
    {
        control = std::make_shared<ControlSession>();
        file = std::make_shared<FileSession>();
        linker = std::make_shared<DoubleLinker>();
        linker->SetControlSession(control);
        linker->SetFileSession(file);
    }

    bool pump(int timeoutMs)
    {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
            auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                            .count();
            if (cost > timeoutMs) {
                return false;
            }
            if (pred_ && pred_()) {
                return true;
            }
        }
    }

    template <typename Pred> bool wait(Pred pred, int timeoutMs, const char* what)
    {
        pred_ = pred;
        if (!pump(timeoutMs)) {
            qCritical() << "等待超时:" << what;
            return false;
        }
        return true;
    }

    bool connectAll(const QString& ip, int16_t port)
    {
        // ClientCore 及其 QTcpSocket 在工作线程，跨线程必须走队列调用
        auto* ccli = control->getClientCore();
        QMetaObject::invokeMethod(
            ccli, [ccli, ip, port]() { ccli->connectToServer(ip, port); }, Qt::QueuedConnection);
        control->AskOwnID("trans-test");
        if (!wait([&]() { return !control->getOwnInfo().clientId.empty(); }, 10000, "控制ID")) {
            return false;
        }
        // 文件通道连接
        emit linker->signalFileDoConnect(ip, port);
        auto* fcli = file->getClientCore();
        if (!wait([&]() { return fcli->isConnected(); }, 10000, "文件连接")) {
            return false;
        }
        file->AskOwnID();
        if (!wait([&]() { return !fcli->getOwnClientInfo().clientId.empty(); }, 10000, "文件ID")) {
            return false;
        }
        return true;
    }

private:
    std::function<bool()> pred_;
};

static FileMeta makeMeta(const std::string& fullPath, std::uint64_t size)
{
    FileMeta m;
    m.fullPath = fullPath;
    m.size = size;
    m.type = FileType::FILE_TYPE_FILE;
    m.mark = FileDir::GetMark();
    auto pos = fullPath.find_last_of("/\\");
    m.name = (pos == std::string::npos) ? fullPath : fullPath.substr(pos + 1);
    m.dir = (pos == std::string::npos) ? std::string(".") : fullPath.substr(0, pos);
    return m;
}

int runSender(const QString& localFile, const QString& remotePath)
{
    QFileInfo fi(localFile);
    if (!fi.exists() || !fi.isFile()) {
        qCritical() << "本地文件不存在:" << localFile;
        return 1;
    }

    TestClient tc;
    tc.setup();
    if (!tc.connectAll("127.0.0.1", kPort)) {
        return 1;
    }
    // 请求客户端列表并选中对端（排除自己），随后设置 otherInfo 供 RunTaskItem 使用
    QMutex peerLock;
    ClientInfo peer;
    std::atomic_bool gotPeer{false};
    tc.control->SendWithCall(Message{}, FrameType::kMsgType_Ask_ClientList, [&](MessagePtr resp) {
        if (!resp) {
            return;
        }
        auto own = tc.control->getOwnInfo().clientId;
        QMutexLocker locker(&peerLock);
        for (auto& c : resp->clientList) {
            if (!c.clientId.empty() && c.clientId != own) {
                peer = c;
                gotPeer = true;
                break;
            }
        }
    });
    if (!tc.wait([&]() { return gotPeer.load(); }, kWaitMs, "对端上线")) {
        return 1;
    }
    {
        QMutexLocker locker(&peerLock);
        tc.control->getClientCore()->setOtherClientInfo(peer);
    }
    qInfo() << "对端:" << QString::fromStdString(peer.clientId);

    auto item = std::make_shared<TransItem>();
    item->isSend = true;
    item->from = makeMeta(localFile.toStdString(), static_cast<std::uint64_t>(fi.size()));
    item->to = makeMeta(remotePath.toStdString(), static_cast<std::uint64_t>(fi.size()));

    std::uint64_t lastTransed = 0;
    QObject::connect(tc.linker.get(), &DoubleLinker::signalCurFileProgress,
        [&lastTransed](std::uint64_t transed, std::uint64_t) { lastTransed = transed; });

    qInfo() << "开始发送:" << localFile << "=>" << remotePath << "大小:" << fi.size();
    auto start = std::chrono::steady_clock::now();
    bool ok = tc.linker->RunTaskItem(item);
    auto costMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    if (!ok) {
        qCritical() << "传输失败。已发送:" << lastTransed;
        return 1;
    }
    double mb = static_cast<double>(fi.size()) / (1024.0 * 1024.0);
    double mbps = costMs > 0 ? mb / (static_cast<double>(costMs) / 1000.0) : 0;
    std::cout << fmt::format("SEND_OK cost={}ms size={}MB speed={:.2f}MB/s", costMs, mb, mbps) << std::endl;
    qInfo() << "传输完成:" << costMs << "ms," << mb << "MB," << mbps << "MB/s";
    return 0;
}

int runReceiver(const QString& fullPath, std::uint64_t expectSize)
{
    TestClient tc;
    tc.setup();
    if (!tc.connectAll("127.0.0.1", kPort)) {
        return 1;
    }

    qInfo() << "接收端就绪，等待文件:" << fullPath << "期望大小:" << expectSize;
    QFile f(fullPath);
    if (!tc.wait([&]() {
            QFileInfo info(fullPath);
            return info.exists() && static_cast<std::uint64_t>(info.size()) >= expectSize;
        }, kWaitMs, "接收完成")) {
        qCritical() << "接收超时，当前大小:" << (f.exists() ? f.size() : -1);
        return 1;
    }
    std::cout << fmt::format("RECV_OK size={}", expectSize) << std::endl;
    return 0;
}

int main(int argc, char* argv[])
{
    qRegisterMetaType<int16_t>("int16_t");
    qRegisterMetaType<FramePtr>("FramePtr");

    auto versionMsg = fmt::format("{}-v{}-{}", VERSION_GIT_COMMIT, VERSION_NUM, VERSION_DEV);

    Logger logger;
    logger.setInfo("log/relayFileTransTest.log", "relayFileTransTest");
    if (!logger.initSimpleLogger(true)) {
        return 1;
    }

    OwnLogger ownLogger;
    qInstallMessageHandler(ownLogger.ConsoleMsgHander);

    QCoreApplication app(argc, argv);

    if (argc >= 2 && std::strcmp(argv[1], "server") == 0) {
        qInfo() << "Version:" << QString::fromStdString(versionMsg);
        // server 必须存活于整个 main 作用域，函数局部对象会随返回析构导致端口关闭
        auto server = std::make_shared<ServerCore>();
        if (!server->startListen(kPort)) {
            qCritical() << "服务器启动失败，端口:" << kPort;
            return 1;
        }
        qInfo() << "trans-test server 已启动，端口:" << kPort;
        return app.exec();
    }
    if (argc == 4 && std::strcmp(argv[1], "sender") == 0) {
        return runSender(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    if (argc == 4 && std::strcmp(argv[1], "receiver") == 0) {
        return runReceiver(QString::fromLocal8Bit(argv[2]), std::stoull(argv[3]));
    }

    std::cout << "用法:\n"
              << "  relayFileTransTest server\n"
              << "  relayFileTransTest sender <本地文件> <远端完整路径>\n"
              << "  relayFileTransTest receiver <完整路径> <期望大小>" << std::endl;
    return 1;
}
