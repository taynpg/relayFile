#include <QCoreApplication>
#include <Utils/Logger.h>
#include <Utils/OwnLogger.h>

int main(int argc, char* argv[])
{
    Logger logger;
    logger.setInfo("log/relayFileClient.log", "relayFileClient");
    if (!logger.initSimpleLogger(true)) {
        return 1;
    }

    OwnLogger ownLogger;
    qInstallMessageHandler(ownLogger.ConsoleMsgHander);

    QCoreApplication app(argc, argv);

    return app.exec();
}