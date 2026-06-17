#include "Common.h"

#include <QCryptographicHash>
#include <QFile>
#include <QUuid>

QString Common::GetUUID()
{
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return uuid;
}

QString Common::GenSha256(const QString& str, bool isFile)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);

    if (isFile) {
        QFile file(str);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }

        if (!hash.addData(&file)) {
            return QString();
        }
    } else {
        hash.addData(str.toUtf8());
    }
    return QString(hash.result().toHex());
}