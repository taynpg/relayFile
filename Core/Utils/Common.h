#pragma once

#include <QMutex>
#include <QString>

class Common
{
private:
    Common() = default;
    ~Common() = default;

public:
    static QString GetUUID();
    static QString GenSha256(const QString& str, bool isFile);
    static QVector<QString> GetLocalDrivers();
};
