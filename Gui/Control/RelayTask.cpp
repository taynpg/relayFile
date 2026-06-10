#include "RelayTask.h"

#include <QDateTime>
#include <QHeaderView>
#include <Utils/Common.h>

#include "Base/BaseHelper.h"
#include "Protocol/Serialize.hpp"
#include "ui_RelayTask.h"


RelayTask::RelayTask(QWidget* parent) : QDialog(parent), ui(new Ui::RelayTask)
{
    ui->setupUi(this);
    initControl();
    baseTask();
    initSignals();
}

RelayTask::~RelayTask()
{
    Quit();
    delete ui;
}

void RelayTask::Quit()
{
    workerThread_->stop();
    workerThread_->quit();
}

void RelayTask::closeEvent(QCloseEvent* event)
{
    Quit();
    QDialog::closeEvent(event);
}

void RelayTask::baseTask()
{
    doubleLinker_ = std::make_shared<DoubleLinker>();
    doubleLinker_->SetControlSession(GlobalData::getInstance()->getControlSession());
    doubleLinker_->SetFileSession(GlobalData::getInstance()->getFileSession());

    askLocalDf_ = BaseAskDF::Create(AskType::ASK_TYPE_LOCAL);
    askRemoteDf_ = BaseAskDF::Create(AskType::ASK_TYPE_REMOTE);
    workerThread_ = std::make_shared<WorkerThread<RelayTask>>(this);
    workerThread_->start();
}

void RelayTask::initControl()
{
    ui->edFrom->setEnabled(false);
    ui->edTo->setEnabled(false);
    ui->edLocalRoot->setEnabled(false);
    ui->edRemoteRoot->setEnabled(false);
    ui->rbDisconnect->setEnabled(false);
    ui->rbNormal->setEnabled(false);
    ui->pedLog->setEnabled(false);
    ui->lcdNumber->setEnabled(false);
    ui->rbDisconnect->setChecked(true);

    tableWidget_ = new QTableWidget();
    tableWidget_->setColumnCount(4);
    tableWidget_->setHorizontalHeaderLabels({"序号", "名称", "大小", "状态"});
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget_->setContextMenuPolicy(Qt::CustomContextMenu);

    tableWidget_->setColumnWidth(0, 50);
    tableWidget_->setColumnWidth(2, 100);
    tableWidget_->setColumnWidth(3, 100);
    tableWidget_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tableWidget_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);

    auto* layout = new QVBoxLayout();
    layout->addWidget(tableWidget_);
    layout->setContentsMargins(0, 0, 0, 0);
    ui->widget->setLayout(layout);
}

void RelayTask::initSignals()
{
    connect(this, &RelayTask::signalLog, this, &RelayTask::onAppendLog);
    connect(this, &RelayTask::signalCheckComplete, this, &RelayTask::onCheckComplete);
    connect(this, &RelayTask::signalCheckControlClient, this, &RelayTask::onCheckControlClient);
    connect(this, &RelayTask::signalCheckTransClient, this, &RelayTask::onCheckTransClient);
    connect(this, &RelayTask::signalCheckLocalRoot, this, &RelayTask::onCheckLocalRoot);
    connect(this, &RelayTask::signalCheckRemoteRoot, this, &RelayTask::onCheckRemoteRoot);
    connect(this, &RelayTask::signalCheckSameFile, this, &RelayTask::onCheckSameFile);
    auto* cliCore = doubleLinker_->GetFileSession()->getClientCore();
    connect(this, &RelayTask::signalDoTransConnect, cliCore, &ClientCore::connectToServer);
    connect(cliCore, &ClientCore::signalConnected, this, &RelayTask::onDoTransConnectDone);
    connect(cliCore, &ClientCore::signalDisconnected, this, &RelayTask::onDoTransConnectFailed);
    connect(cliCore, &ClientCore::signalDisconnected, this, &RelayTask::onDoTransDisconnect);
    connect(cliCore, &ClientCore::signalConnectting, this, &RelayTask::onDoTransConnecting);
    connect(ui->btnBasicCheck, &QPushButton::clicked, this, &RelayTask::onBaseCheck);
    connect(ui->btnStart, &QPushButton::clicked, this, &RelayTask::onStartRun);
    connect(this, &RelayTask::signalUpdateTable, this, &RelayTask::updateTable);
}

void RelayTask::onStartRun()
{
}

void RelayTask::handleOneLine(int row)
{
    int id = tableWidget_->item(row, 0)->text().toInt();
    if (id >= fileList_.size()) {
        return;
    }

    CmdExecutor executor(doubleLinker_);
    executor.Reset();

    const auto& fileMeta = fileList_[id];
    // 先请求到Server
    Message reqMsg;
    reqMsg.uuid = Common::GetUUID().toStdString();
    reqMsg.msType = MessageType::kMessageFileRequestSend;
    reqMsg.msData = data_->remoteRoot.toStdString();
    reqMsg.mapData[""] = std::vector<FileMeta>{fileMeta};

    auto requestFrame = OneFrame::Create();
    requestFrame->data = serializeStruct(reqMsg);

    //  等待Server控制对方建立文件传输通道
    executor.AddStep([this](FramePtr frame) -> FramePtr {
        Message respMsg;
        deserializeStruct(frame->data, respMsg);
        if (respMsg.msgStateCode != MessageStateCode::kMessageStateCodeSuccess) {
            emit signalLog(
                QString("Server返回错误[%1]：%2").arg(int(respMsg.msgStateCode)).arg(QString::fromStdString(respMsg.errData)));
            return nullptr;
        }
        // 如果成功，对方必须告知文件传输通道的ID号。
        return frame;
    });
    //  等待Server通知结果
    //  根据结果进行放弃或者传输
}

void RelayTask::setData(std::shared_ptr<RelayTaskData> data)
{
    data_ = data;
}

void RelayTask::showEvent(QShowEvent* event)
{
    if (data_) {
        ui->edLocalRoot->setText(data_->localRoot);
        ui->edRemoteRoot->setText(data_->remoteRoot);
    }
    QDialog::showEvent(event);
}

void RelayTask::onBaseCheck()
{
    emit signalLog("开始检查基础条件...");
    emit signalCheckControlClient();
    // 1.检查传输TCP是否正常。
    // 2.检查控制TCP是否正常。
    // 3.检查本地根目录是否存在。
    // 4.检查远程根目录是否存在。
    // 5.如果是上传，检查远端是否已存在相同文件。
    // 6.如果是下载，检查本地是否已存在相同文件。
}

void RelayTask::onCheckControlClient()
{
    auto* cliCore = doubleLinker_->GetControlSession()->getClientCore();
    if (!cliCore->isConnected()) {
        emit signalLog("控制端未连接");
        return;
    }
    emit signalLog("控制端已连接");
    emit signalCheckTransClient();
}

void RelayTask::onAppendLog(const QString& log)
{
    auto dt = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    auto msg = "[" + dt + "] " + log;
    ui->pedLog->appendPlainText(msg);
}

void RelayTask::onCheckTransClient()
{
    auto* cliCore = doubleLinker_->GetFileSession()->getClientCore();
    if (!cliCore->isConnected()) {
        emit signalLog("传输端未连接，尝试自动连接。");
        emit signalDoTransConnect(cliCore->getServerIp(), cliCore->getServerPort());
        return;
    }
    emit signalLog("传输端已连接");
    emit signalCheckLocalRoot();
}

void RelayTask::onCheckLocalRoot()
{
    emit signalLog("本地根目录存在");

    fileList_.clear();
    for (const auto& item : data_->fileList) {
        // 文件夹暂时不处理
        if (item.type == RFileType::mTypeDir) {
            continue;
        }
        auto path = FileDir::Join(data_->localRoot, item.name);
        FileMeta meta;
        meta.sizeStr = item.sizeStr.toStdString();
        meta.name = path.toStdString();
        meta.size = item.size;
        fileList_.push_back(meta);
    }
    emit signalUpdateTable();
    emit signalCheckRemoteRoot();
}

void RelayTask::onCheckRemoteRoot()
{
    emit signalLog("远程根目录存在");
    emit signalCheckSameFile();
}

void RelayTask::onCheckSameFile()
{
    emit signalLog("检查相同文件");
    emit signalCheckComplete();
}

void RelayTask::onDoTransConnectDone()
{
    emit signalLog("传输端连接成功");
    emit signalCheckControlClient();
}

void RelayTask::onDoTransConnectFailed()
{
    emit signalLog("传输端连接失败");
}

void RelayTask::onDoTransDisconnect()
{
    emit signalLog("传输端已断开连接");
}

void RelayTask::onDoTransConnecting()
{
    emit signalLog("传输端正在连接中...");
}

void RelayTask::onCheckComplete()
{
    emit signalLog("检查完成");
}

void RelayTask::updateTable()
{
    tableWidget_->clearContents();
    tableWidget_->setRowCount(0);

    for (int i = 0; i < fileList_.size(); ++i) {
        auto row = tableWidget_->rowCount();
        tableWidget_->insertRow(row);
        setFileItem(fileList_[i], row, i);
        if (i != 0 && i % 30 == 0) {
            QGuiApplication::processEvents();
        }
    }
}

void RelayTask::setFileItem(const FileMeta& meta, int row, int index)
{
    auto* indexItem = new QTableWidgetItem(QString::number(index));
    indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 0, indexItem);

    auto* nameItem = new QTableWidgetItem(QString::fromStdString(meta.name));
    nameItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 1, nameItem);

    auto* sizeItem = new QTableWidgetItem(QString::fromStdString(meta.sizeStr));
    sizeItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 2, sizeItem);

    auto* stateItem = new QTableWidgetItem("等待");
    stateItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 3, stateItem);
}