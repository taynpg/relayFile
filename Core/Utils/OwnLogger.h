#pragma once

#include <QDebug>

class OwnLogger
{
public:
    OwnLogger();
    ~OwnLogger();

public:
    static void ConsoleMsgHander(QtMsgType type, const QMessageLogContext& context, const QString& msg);
};
