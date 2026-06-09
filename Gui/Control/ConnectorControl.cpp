#include "ConnectorControl.h"

#include <QAbstractItemView>
#include <QMenu>

#include "Base/BaseHelper.h"
#include "Protocol/Serialize.hpp"
#include "ui_ConnectorControl.h"

ConnectorControl::ConnectorControl(QWidget* parent) : QDialog(parent), ui(new Ui::ConnectorControl)
{
    ui->setupUi(this);
    setMaximumWidth(350);
    initTable();
    initUI();

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

void ConnectorControl::initUI()
{
    ui->lineEdit->setEnabled(false);
    ui->edCurrentClient->setEnabled(false);
}

ConnectorControl::~ConnectorControl()
{
    delete ui;
}

void ConnectorControl::initTable()
{
    QTableWidget* tableWidget = ui->tableClients;

    tableWidget->setColumnCount(2);
    tableWidget->setHorizontalHeaderLabels({"ID", "名称"});
    tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    tableWidget->setColumnWidth(0, 150);
    tableWidget->setColumnWidth(1, 150);

    tableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
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
    connect(clientControl_, &ClientCore::signalOwnInfo, this, &ConnectorControl::onOwnInfo);
    connect(ui->tableClients, &QTableWidget::customContextMenuRequested, this, &ConnectorControl::onTableContextMenu);
}

void ConnectorControl::onRefresh()
{
    Message msg;
    msg.msType = MessageType::kMessageAskClientList;
    QMetaObject::invokeMethod(clientControl_, [this, msg]() {
        clientControl_->SendWithCall(msg, [this](FramePtr frame) {
            if (!frame) {
                return;
            }
            Message answerMsg;
            deserializeStruct(frame->data, answerMsg);
            if (answerMsg.msType != MessageType::kMessageAnswerClientList) {
                return;
            }
            QMetaObject::invokeMethod(this, [this, answerMsg]() { updateClientList(answerMsg); });
        });
    });
}

void ConnectorControl::updateClientList(const Message& msg)
{
    ui->tableClients->clearContents();
    ui->tableClients->setRowCount(0);
    auto selfInfo = clientControl_->getSelfInfo();
    for (auto& client : msg.clientList) {
        auto row = ui->tableClients->rowCount();
        ui->tableClients->insertRow(row);

        auto id = QString::fromStdString(client.clientId);
        auto* item = new QTableWidgetItem(id);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(client.clientName));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

        if (client.clientId == selfInfo.clientId) {
            item->setForeground(Qt::red);
            nameItem->setForeground(Qt::red);
        }

        ui->tableClients->setItem(row, 0, item);
        ui->tableClients->setItem(row, 1, nameItem);
    }
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

    onRefresh();
}

void ConnectorControl::onDisconnectSuccess()
{
    onErrorOccurred();
}

void ConnectorControl::onErrorOccurred()
{
    ui->btnConnect->setEnabled(true);
    ui->btnDisconnect->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
    ui->tableClients->clearContents();
    ui->tableClients->setRowCount(0);
}

void ConnectorControl::onOwnInfo(const ClientInfo& info)
{
    QString infoMsg = QString("%1,%2").arg(info.clientId).arg(info.clientName);
    ui->lineEdit->setText(infoMsg);
}

void ConnectorControl::onConnectting()
{
    ui->btnConnect->setEnabled(false);
    ui->btnDisconnect->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
}

void ConnectorControl::onTableContextMenu(const QPoint& pos)
{
    auto* item = ui->tableClients->itemAt(pos);
    if (!item) {
        return;
    }

    auto id = ui->tableClients->item(item->row(), 0)->text();
    auto name = ui->tableClients->item(item->row(), 1)->text();

    QMenu menu(this);
    QAction* useAction = menu.addAction("与该客户端通信");
    auto* selectAction = menu.exec(ui->tableClients->viewport()->mapToGlobal(pos));

    if (selectAction == useAction) {
        QMetaObject::invokeMethod(this, [this, id, name]() { onUseClient(id, name); });
    }
}

void ConnectorControl::onUseClient(const QString& id, const QString& name)
{
    QString infoMsg = QString("%1,%2").arg(id).arg(name);
    ui->edCurrentClient->setText(infoMsg);
}
