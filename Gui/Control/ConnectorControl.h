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
    void signalConnectDone();

public:
    explicit ConnectorControl(QWidget* parent = nullptr);
    ~ConnectorControl();

public:
    void Quit();

private:
    void initTable();
    void initSignals();
    void initUI();

public slots:
    void onConnectSuccess();
    void onDisconnectSuccess();
    void onErrorOccurred();
    void onConnectting();

    void onRefresh();
    void onOwnInfo(const ClientInfo& info);
    void onTableContextMenu(const QPoint& pos);
    void onUseClient(const QString& id, const QString& name);

public:
    void connectToServer();

private:
    bool parseIpPort(const QString& ipPort, QString& outIp, QString& outPort);
    void updateClientList(const MessagePtr& msg);

private:
    std::shared_ptr<ControlSession> controlSession_{};
    Ui::ConnectorControl* ui;
};

#endif   // CONNECTORCONTROL_H
