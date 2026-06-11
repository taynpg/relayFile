#include "Common.h"

#include <QUuid>

QString Common::GetUUID()
{
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return uuid;
}
