#include "RelayTask.h"

#include <QDateTime>
#include <QHeaderView>

#include "Base/BaseHelper.h"
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

template <typename HandleResp> bool RelayTask::Request(ClientCore* cli, FramePtr frame, HandleResp handleResp)
{
    auto promise = std::make_shared<std::promise<FramePtr>>();
    auto future = promise->get_future();

    QMetaObject::invokeMethod(
        cli, [this, cli, promise, frame]() { cli->SendWithCall(frame, [promise](FramePtr f) { promise->set_value(f); }); });

    FramePtr f = future.get();
    if (!f) {
        qWarning() << "请求远端" << clientControl_->getClientFullName() << "失败";
        return false;
    }

    return handleResp(f);
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
    clientTrans_ = GlobalData::getInstance()->getClientFile();
    clientControl_ = GlobalData::getInstance()->getClientControl();

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
    tableWidget_->setColumnCount(3);
    tableWidget_->setHorizontalHeaderLabels({"名称", "大小", "状态"});
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget_->setContextMenuPolicy(Qt::CustomContextMenu);

    tableWidget_->setColumnWidth(1, 100);
    tableWidget_->setColumnWidth(2, 100);
    tableWidget_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tableWidget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);

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
    connect(this, &RelayTask::signalDoTransConnect, clientTrans_, &ClientCore::connectToServer);
    connect(clientTrans_, &ClientCore::signalConnected, this, &RelayTask::onDoTransConnectDone);
    connect(clientTrans_, &ClientCore::signalDisconnected, this, &RelayTask::onDoTransConnectFailed);
    connect(clientTrans_, &ClientCore::signalDisconnected, this, &RelayTask::onDoTransDisconnect);
    connect(clientTrans_, &ClientCore::signalConnectting, this, &RelayTask::onDoTransConnecting);
    connect(ui->btnBasicCheck, &QPushButton::clicked, this, &RelayTask::onBaseCheck);
    connect(ui->btnStart, &QPushButton::clicked, this, &RelayTask::onStartRun);
    connect(this, &RelayTask::signalUpdateTable, this, &RelayTask::updateTable);
}

void RelayTask::onStartRun()
{
    // 先请求到Server
    // 等待Server控制对方建立文件传输通道
    // 等待Server通知结果
    // 根据结果进行放弃或者传输
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
    if (!clientControl_->isConnected()) {
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
    if (!clientTrans_->isConnected()) {
        emit signalLog("传输端未连接，尝试自动连接。");
        emit signalDoTransConnect(clientControl_->getServerIp(), clientControl_->getServerPort());
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
        setFileItem(fileList_[i], row);
        if (i != 0 && i % 30 == 0) {
            QGuiApplication::processEvents();
        }
    }
}

void RelayTask::setFileItem(const FileMeta& meta, int row)
{
    auto* nameItem = new QTableWidgetItem(QString::fromStdString(meta.name));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 0, nameItem);

    auto* sizeItem = new QTableWidgetItem(QString::fromStdString(meta.sizeStr));
    sizeItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 1, sizeItem);

    auto* stateItem = new QTableWidgetItem("等待");
    stateItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 2, stateItem);
}