#include <Net/ServerCore.h>
#include <QCoreApplication>
#include <Utils/Logger.h>
#include <Utils/OwnLogger.h>
#include <relayFileVersion.h>

int main(int argc, char* argv[])
{
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << VERSION_GIT_COMMIT << "-v" << VERSION_NUM << "-" << VERSION_DEV << std::endl;
        return 0;
    }

    Logger logger;
    logger.setInfo("log/relayFileServer.log", "relayFileServer");
    if (!logger.initSimpleLogger(true)) {
        return 1;
    }

    qInfo() << "Version:" << VERSION_GIT_COMMIT << "-v" << VERSION_NUM << "-" << VERSION_DEV;

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