#ifndef CONNECTORCONTROL_H
#define CONNECTORCONTROL_H

#include <Net/ClientCore.h>
#include <Net/ClientHelper.h>
#include <QDialog>

#include "Base/BaseHelper.h"

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
    void signalAskID(const QString& name);
    void signalConfirmOther();
    void signalCancelWaitMsg();
    void signalNoticeClear();

public:
    explicit ConnectorControl(QWidget* parent = nullptr);
    ~ConnectorControl();

public:
    void Quit();

private:
    void initTable();
    void initSignals();
    void initUI();
    void initLoadIp();
    void initReconnectTimer();

public slots:
    void onConnectSuccess();
    void onDisconnectSuccess();
    void onErrorOccurred();
    void onConnectting();

    void onRefresh();
    void onOwnInfo(const ClientInfo& info);
    void onTableContextMenu(const QPoint& pos);
    void onUseClient(const QString& id, const QString& name);
    void onReconnectCheck();

public:
    void connectToServer();

private:
    bool parseIpPort(const QString& ipPort, QString& outIp, QString& outPort);
    void updateClientList(const MessagePtr& msg);

private:
    QString curIp_;
    int16_t curPort_;
    QTimer* reconTimer_{};
    Ui::ConnectorControl* ui;
    std::shared_ptr<BaseConfig> baseConfig_{};
    std::shared_ptr<ReconHelper> reconHelper_{};
    std::shared_ptr<DoubleLinker> doubleLinker_{};
};

#endif   // CONNECTORCONTROL_H
