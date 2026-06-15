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
};
