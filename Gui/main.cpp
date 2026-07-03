#include <Protocol/Protocol.h>
#include <QApplication>
#include <relayFileVersion.h>

#include "relayFile.h"

int main(int argc, char* argv[])
{
    // Qt5兼容
    qRegisterMetaType<int16_t>("int16_t");
    qRegisterMetaType<FramePtr>("FramePtr");
    qRegisterMetaType<std::vector<FileMeta>>("std::vector<FileMeta>");

    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << VERSION_GIT_COMMIT << "-v" << VERSION_NUM << "-" << VERSION_DEV << std::endl;
        return 0;
    }
    QApplication a(argc, argv);
    relayFile w;
    w.show();
    return a.exec();
}
