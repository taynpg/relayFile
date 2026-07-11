#include <Protocol/Protocol.h>
#include <QApplication>
#include <SingleApplication>
#include <relayFileVersion.h>

#include "relayFile.h"

int main(int argc, char* argv[])
{
    // Qt5兼容
    qRegisterMetaType<int16_t>("int16_t");
    qRegisterMetaType<FramePtr>("FramePtr");
    qRegisterMetaType<std::vector<FileMeta>>("std::vector<FileMeta>");

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << VERSION_GIT_COMMIT << "-v" << VERSION_NUM << "-" << VERSION_DEV << std::endl;
        return 0;
    }

    SingleApplication app(argc, argv);
    relayFile w;
    QObject::connect(&app, &SingleApplication::instanceStarted, &w, [&w]() {
        w.showNormal();
        w.raise();
        w.activateWindow();
    });
    w.show();
    return app.exec();
}
