#ifndef CONNECTORCONTROL_H
#define CONNECTORCONTROL_H

#include <QDialog>

#include "Net/ClientCore.h"
#include "Net/ClientHelper.h"

namespace Ui {
class ConnectorControl;
}

class ConnectorControl : public QDialog
{
    Q_OBJECT

signals:
    void signalDoConnect(const QString& ip, int16_t port);
    void signalDoDisConnect();

public:
    explicit ConnectorControl(QWidget* parent = nullptr);
    ~ConnectorControl();

private:
    void initSignals();

public slots:
    void onConnectSuccess();
    void onDisconnectSuccess();
    void onErrorOccurred();
    void onConnectting();

public:
    void connectToServer();

private:
    bool parseIpPort(const QString& ipPort, QString& outIp, QString& outPort);

private:
    ClientWorker* clientWorker_{};
    ClientCore* clientControl_{};
    Ui::ConnectorControl* ui;
};

#endif   // CONNECTORCONTROL_H
