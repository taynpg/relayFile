#pragma once

#include <QThread>

#include "ClientCore.h"

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