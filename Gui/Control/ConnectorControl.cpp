#include "ConnectorControl.h"

#include "Base/BaseHelper.h"
#include "ui_ConnectorControl.h"

ConnectorControl::ConnectorControl(QWidget* parent) : QDialog(parent), ui(new Ui::ConnectorControl)
{
    ui->setupUi(this);
    setMaximumWidth(350);
    clientControl_ = GlobalData::getInstance()->getClientControl();
    clientWorker_ = new ClientWorker(clientControl_, nullptr);
    clientControl_->moveToThread(clientWorker_);
    initSignals();
    clientWorker_->start();

    // 临时调试设置
    ui->edServerIp->setText("127.0.0.1:9008");
}

void ConnectorControl::Quit()
{
    clientWorker_->quit();
    clientWorker_->wait();
    delete clientWorker_;
}

ConnectorControl::~ConnectorControl()
{
    delete ui;
}

void ConnectorControl::initSignals()
{
    connect(ui->btnConnect, &QPushButton::clicked, this, &ConnectorControl::connectToServer);
    connect(ui->btnDisconnect, &QPushButton::clicked, this, [this]() { emit signalDoDisConnect(); });
    connect(ui->btnRefresh, &QPushButton::clicked, this, &ConnectorControl::onRefresh);

    connect(clientControl_, &ClientCore::signalConnectting, this, &ConnectorControl::onConnectting);
    connect(clientControl_, &ClientCore::signalConnected, this, &ConnectorControl::onConnectSuccess);
    connect(clientControl_, &ClientCore::signalDisconnected, this, &ConnectorControl::onErrorOccurred);
    connect(clientControl_, &ClientCore::signalErrorOccurred, this, &ConnectorControl::onErrorOccurred);

    connect(this, &ConnectorControl::signalDoConnect, clientControl_, &ClientCore::connectToServer);
    connect(this, &ConnectorControl::signalDoDisConnect, clientControl_, &ClientCore::disconnectFromServer);
}

void ConnectorControl::onRefresh()
{
}

void ConnectorControl::connectToServer()
{
    auto serverIp = ui->edServerIp->text().trimmed();
    QString ip, port;
    if (!parseIpPort(serverIp, ip, port)) {
        qWarning() << "不合法的IPV4地址:" << serverIp;
        return;
    }
    int16_t portNum = port.toInt();
    emit signalDoConnect(ip, portNum);
}

bool ConnectorControl::parseIpPort(const QString& ipPort, QString& outIp, QString& outPort)
{
    QRegularExpression regex("^\\s*"
                             "("
                             "(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\."
                             "(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\."
                             "(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\."
                             "(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)"
                             ")"
                             "\\s*:\\s*"
                             "("
                             "\\d{1,5}"
                             ")"
                             "\\s*$");

    QRegularExpressionMatch match = regex.match(ipPort);
    if (!match.hasMatch()) {
        return false;
    }

    outIp = match.captured(1);
    outPort = match.captured(2);

    bool portOk;
    int portNum = outPort.toInt(&portOk);
    return portOk && portNum > 0 && portNum <= 65535;
}

void ConnectorControl::onConnectSuccess()
{
    ui->btnConnect->setEnabled(false);
    ui->btnDisconnect->setEnabled(true);
    ui->btnRefresh->setEnabled(true);
}

void ConnectorControl::onDisconnectSuccess()
{
    ui->btnConnect->setEnabled(true);
    ui->btnDisconnect->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
}

void ConnectorControl::onErrorOccurred()
{
    ui->btnConnect->setEnabled(true);
    ui->btnDisconnect->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
}

void ConnectorControl::onConnectting()
{
    ui->btnConnect->setEnabled(false);
    ui->btnDisconnect->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
}
