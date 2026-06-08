#include <QApplication>

#include "relayFile.h"

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    relayFile w;
    w.show();
    return a.exec();
}
