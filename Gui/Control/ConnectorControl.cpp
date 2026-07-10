#include "ConnectorControl.h"

#include <Net/ClientHelper.h>
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

    ui->cbIp->setEditable(true);
    doubleLinker_ = GlobalData::getInstance()->getDoubleLinker();
    initSignals();
    baseConfig_ = GlobalData::getInstance()->getBaseConfig();
    initLoadIp();
    initReconnectTimer();
}

void ConnectorControl::initReconnectTimer()
{
    // 重连助手
    reconHelper_ = std::make_shared<ReconHelper>();
    RetryCon rc;
    baseConfig_->getReconInterval(rc);
    reconHelper_->setRetryCon(rc);
    reconHelper_->markConnected(false);
    reconHelper_->markNeedRecon(false);

    reconTimer_ = new QTimer(this);
    connect(reconTimer_, &QTimer::timeout, this, &ConnectorControl::onReconnectCheck);
    reconTimer_->start(1000);
}

void ConnectorControl::onReconnectCheck()
{
    if (reconHelper_->shouleReconnct()) {
        qWarning() << "Auto reconnect To => " << curIp_ << ":" << curPort_;
        emit signalDoConnect(curIp_, curPort_);
    }
}

void ConnectorControl::Quit()
{
    reconTimer_->stop();
}

void ConnectorControl::initUI()
{
    ui->lineEdit->setReadOnly(true);
    ui->edCurrentClient->setReadOnly(true);
    ui->btnDisconnect->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
    ui->lineEdit->setStyleSheet("color: red;");
}

void ConnectorControl::initLoadIp()
{
    ui->cbIp->clear();
    IpHistory history;
    if (baseConfig_->getIpHistory(history)) {
        auto curip = history.current;
        for (const auto& ip : history.history) {
            ui->cbIp->addItem(QString::fromStdString(ip));
        }
        ui->cbIp->setCurrentText(QString::fromStdString(curip));
    }
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

    tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
}

void ConnectorControl::initSignals()
{
    connect(ui->btnConnect, &QPushButton::clicked, this, &ConnectorControl::connectToServer);
    connect(ui->btnDisconnect, &QPushButton::clicked, this, [this]() {
        reconHelper_->markNeedRecon(false);
        emit signalDoDisConnect();
    });
    connect(ui->btnRefresh, &QPushButton::clicked, this, &ConnectorControl::onRefresh);

    auto* cliCore = doubleLinker_->GetControlSession()->getClientCore();
    connect(cliCore, &ClientCore::signalConnectting, this, &ConnectorControl::onConnectting);
    connect(cliCore, &ClientCore::signalConnected, this, &ConnectorControl::onConnectSuccess);
    connect(cliCore, &ClientCore::signalDisconnected, this, &ConnectorControl::onErrorOccurred);
    connect(cliCore, &ClientCore::signalErrorOccurred, this, &ConnectorControl::onErrorOccurred);

    connect(this, &ConnectorControl::signalDoConnect, cliCore, &ClientCore::connectToServer);
    connect(this, &ConnectorControl::signalDoDisConnect, cliCore, &ClientCore::disconnectFromServer);

    auto controlSession = doubleLinker_->GetControlSession();
    connect(controlSession.get(), &ControlSession::signalOwnInfo, this, &ConnectorControl::onOwnInfo);
    connect(this, &ConnectorControl::signalAskID, controlSession.get(),
            [this](const QString& name) { doubleLinker_->GetControlSession()->AskOwnID(name); });
    connect(ui->tableClients, &QTableWidget::customContextMenuRequested, this, &ConnectorControl::onTableContextMenu);
    connect(this, &ConnectorControl::signalCancelWaitMsg, doubleLinker_.get(), &DoubleLinker::onCancelWaitMsg);

    doubleLinker_->GetControlSession()->RegisterPubCall(FrameType::kMsgType_Notify_ClientList, [this](FramePtr frame) {
        if (!frame) {
            return;
        }
        MessagePtr answerMsg = std::make_shared<Message>();
        deserializeStruct(frame->data, *answerMsg);

        if (!doubleLinker_->GetControlSession()->getOtherInfo().clientId.empty()) {
            bool have = false;
            auto oId = doubleLinker_->GetControlSession()->getOtherInfo().clientId;
            for (auto& client : answerMsg->clientList) {
                if (client.clientId == oId) {
                    have = true;
                    break;
                }
            }
            if (!have) {
                emit signalCancelWaitMsg();
                emit signalNoticeClear();
                ClientInfo o;
                doubleLinker_->GetControlSession()->getClientCore()->setOtherClientInfo(o);
                ui->edCurrentClient->setText("");
            }
        }

        QMetaObject::invokeMethod(this, [this, answerMsg]() { updateClientList(answerMsg); });
    });
}

void ConnectorControl::onRefresh()
{
    Message msg;
    doubleLinker_->GetControlSession()->SendWithCall(msg, FrameType::kMsgType_Ask_ClientList, [this](FramePtr frame) {
        if (!frame) {
            return;
        }
        MessagePtr answerMsg = std::make_shared<Message>();
        deserializeStruct(frame->data, *answerMsg);
        QMetaObject::invokeMethod(this, [this, answerMsg]() { updateClientList(answerMsg); });
    });
}

void ConnectorControl::updateClientList(const MessagePtr& msg)
{
    if (!msg) {
        return;
    }
    ui->tableClients->clearContents();
    ui->tableClients->setRowCount(0);
    auto selfInfo = doubleLinker_->GetControlSession()->getOwnInfo();
    for (auto& client : msg->clientList) {
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
    auto serverIp = ui->cbIp->currentText().trimmed();
    QString ip, port;
    if (!parseIpPort(serverIp, ip, port)) {
        qWarning() << "不合法的IPV4地址:" << serverIp;
        return;
    }
    int16_t portNum = port.toInt();
    curIp_ = ip;
    curPort_ = portNum;
    emit signalDoConnect(curIp_, curPort_);
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
    reconHelper_->markConnected(true);
    reconHelper_->markNeedRecon(true);
    ui->btnConnect->setEnabled(false);
    ui->btnDisconnect->setEnabled(true);
    ui->btnRefresh->setEnabled(true);
    emit signalConnectDone();
    emit signalAskID(baseConfig_->getCurrentName());
    baseConfig_->pushOneIp(ui->cbIp->currentText().toStdString());
    initLoadIp();
}

void ConnectorControl::onDisconnectSuccess()
{
    onErrorOccurred();
}

void ConnectorControl::onErrorOccurred()
{
    reconHelper_->markConnected(false);
    ui->btnConnect->setEnabled(true);
    ui->btnDisconnect->setEnabled(false);
    ui->btnRefresh->setEnabled(false);
    ui->tableClients->clearContents();
    ui->tableClients->setRowCount(0);

    ui->edCurrentClient->setText("");
    ui->lineEdit->setText("");

    emit signalNoticeClear();
}

void ConnectorControl::onOwnInfo(const ClientInfo& info)
{
    QString infoMsg = QString("%1,%2").arg(QString::fromStdString(info.clientId)).arg(QString::fromStdString(info.clientName));
    ui->lineEdit->setText(infoMsg);
    onRefresh();
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
    ClientInfo o;
    o.clientId = id.toStdString();
    o.clientName = name.toStdString();
    doubleLinker_->GetControlSession()->getClientCore()->setOtherClientInfo(o);
    ui->edCurrentClient->setText(infoMsg);
    emit signalConfirmOther();
}
