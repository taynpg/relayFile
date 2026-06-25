#include <QCoreApplication>
#include <Utils/Logger.h>
#include <Utils/OwnLogger.h>
#include <fmt/format.h>
#include <iostream>
#include <relayFileVersion.h>

int main(int argc, char* argv[])
{
    auto versionMsg = fmt::format("{}-v{}-{}", VERSION_GIT_COMMIT, VERSION_NUM, VERSION_DEV);

    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << VERSION_GIT_COMMIT << "-v" << VERSION_NUM << "-" << VERSION_DEV << std::endl;
        return 0;
    }
    Logger logger;
    logger.setInfo("log/relayFileClient.log", "relayFileClient");
    if (!logger.initSimpleLogger(true)) {
        return 1;
    }

    qInfo() << "\n============================================";
    qInfo() << "Version:" << QString::fromStdString(versionMsg);
    qInfo() << "============================================\n";

    OwnLogger ownLogger;
    qInstallMessageHandler(ownLogger.ConsoleMsgHander);

    QCoreApplication app(argc, argv);

    return app.exec();
}