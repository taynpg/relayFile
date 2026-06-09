#include "Common.h"

#include <QUuid>

QString Common::GetUUID()
{
    return QUuid::createUuid().toString();
}
