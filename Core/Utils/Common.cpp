#include "Common.h"

#include <QCryptographicHash>
#include <QFile>
#include <QStorageInfo>
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

QVector<QString> Common::GetLocalDrivers()
{
    QVector<QString> drivers;
    auto mountedVolumes = QStorageInfo::mountedVolumes();
    for (const auto& driver : mountedVolumes) {
        if (driver.isValid() && driver.isReady()) {
            drivers.push_back(driver.rootPath());
        }
    }
    return drivers;
}