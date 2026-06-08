#pragma once

#include <QThread>

#include "ClientCore.h"
#include <QTimer>

// 客户端工作线程
class ClientWorker : public QThread
{
    Q_OBJECT
public:
    ClientWorker(ClientCore* core, QObject* parent = nullptr);
    ~ClientWorker();

protected:
    void run() override;

private:
    ClientCore* core_{};
};