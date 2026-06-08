#include <Net/ServerCore.h>
#include <QCoreApplication>
#include <Utils/Logger.h>
#include <Utils/OwnLogger.h>

int main(int argc, char* argv[])
{
    Logger logger;
    logger.setInfo("log/relayFileServer.log", "relayFileServer");
    if (!logger.initSimpleLogger(true)) {
        return 1;
    }

    OwnLogger ownLogger;
    qInstallMessageHandler(ownLogger.ConsoleMsgHander);

    auto server = std::make_shared<ServerCore>();

    QCoreApplication app(argc, argv);

    if (!server->startListen(9008)) {
        qCritical() << "relayFileServer启动失败，端口:" << 9008;
        return 1;
    }

    qInfo() << "relayFileServer已启动在端口:" << 9008 << "，按Ctrl+C退出。";
    return app.exec();
}