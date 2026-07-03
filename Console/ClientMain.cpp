#include <Protocol/Protocol.h>
#include <QCoreApplication>
#include <QTimer>
#include <Utils/Logger.h>
#include <Utils/OwnLogger.h>
#include <fmt/format.h>
#include <iostream>
#include <relayFileVersion.h>

#include "Client.h"

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

    std::string ip("127.0.0.1");
    int16_t port{9008};

    if (argc == 3) {
        ip = argv[1];
        port = static_cast<int16_t>(std::stoi(argv[2]));
    } else if (argc == 2) {
        ip = argv[1];
    }

    Logger logger;
    logger.setInfo("log/relayFileClient.log", "relayFileClient");
    if (!logger.initSimpleLogger(true)) {
        return 1;
    }

    qInfo() << "\n============================================";
    qInfo() << "Version:" << QString::fromStdString(versionMsg);
    qInfo() << "Connect to" << QString::fromStdString(ip) << port;
    qInfo() << "============================================\n";

    OwnLogger ownLogger;
    qInstallMessageHandler(ownLogger.ConsoleMsgHander);

    QCoreApplication app(argc, argv);

    auto bc = std::make_shared<BaseClient>();

    QTimer::singleShot(0, [bc, ip, port]() { bc->Work(QString::fromStdString(ip), port); });

    return app.exec();
}