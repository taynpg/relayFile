#pragma once

#include <QDebug>
#include <QObject>

class OwnLogger : public QObject
{
    Q_OBJECT

public:
    OwnLogger(QObject* parent = nullptr);
    ~OwnLogger();

public:
    static void ConsoleMsgHander(QtMsgType type, const QMessageLogContext& context, const QString& msg);

public slots:
    void ShowInfo(const QString& data);
};
