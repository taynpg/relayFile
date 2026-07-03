#include <Net/ServerCore.h>
#include <Protocol/Protocol.h>
#include <QCoreApplication>
#include <Utils/Logger.h>
#include <Utils/OwnLogger.h>
#include <fmt/format.h>
#include <relayFileVersion.h>

int main(int argc, char* argv[])
{
    // Qt5兼容
    qRegisterMetaType<int16_t>("int16_t");
    qRegisterMetaType<FramePtr>("FramePtr");

    auto versionMsg = fmt::format("{}-v{}-{}", VERSION_GIT_COMMIT, VERSION_NUM, VERSION_DEV);

    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << versionMsg << std::endl;
        return 0;
    }

    Logger logger;
    logger.setInfo("log/relayFileServer.log", "relayFileServer");
    if (!logger.initSimpleLogger(true)) {
        return 1;
    }

    qInfo() << "\n============================================";
    qInfo() << "Version:" << QString::fromStdString(versionMsg);
    qInfo() << "============================================\n";

    OwnLogger ownLogger;
    qInstallMessageHandler(ownLogger.ConsoleMsgHander);

    QCoreApplication app(argc, argv);
    auto server = std::make_shared<ServerCore>();

    if (!server->startListen(9008)) {
        qCritical() << "relayFileServer启动失败，端口:" << 9008;
        return 1;
    }

    qInfo() << "relayFileServer已启动在端口:" << 9008 << "，按Ctrl+C退出。";
    return app.exec();
}