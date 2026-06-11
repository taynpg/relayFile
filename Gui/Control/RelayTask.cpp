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
    doubleLinker_ = GlobalData::getInstance()->getDoubleLinker();
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
    ui->btnStart->setEnabled(false);
    ui->btnRetryAll->setEnabled(false);

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
    connect(ui->btnBasicCheck, &QPushButton::clicked, this, &RelayTask::onBaseCheck);
    connect(ui->btnStart, &QPushButton::clicked, this, &RelayTask::onStartRun);
    connect(this, &RelayTask::signalUpdateTable, this, &RelayTask::updateTable);
}

void RelayTask::onStartRun()
{
    workerThread_->invoke([this]() { handleOneLine(0); });
}

void RelayTask::handleOneLine(int row)
{
    int id = tableWidget_->item(row, 0)->text().toInt();
    if (id >= fileList_.size()) {
        return;
    }

    CmdExecutor executor(nullptr, doubleLinker_);
    executor.Reset();

    const auto& fileMeta = fileList_[id];
    // 先请求到Server
    Message reqMsg;
    reqMsg.uuid = Common::GetUUID().toStdString();
    reqMsg.comStr = data_->remoteRoot.toStdString();
    reqMsg.mapData[""] = std::vector<FileMeta>{fileMeta};

    auto requestFrame = OneFrame::Create();
    requestFrame->data = serializeStruct(reqMsg);
    requestFrame->type = FrameType::kFileType_Request_Send;

    //  等待Server控制对方建立文件传输通道
    executor.AddStep([this](FramePtr frame) -> FramePtr {
        Message respMsg;
        deserializeStruct(frame->data, respMsg);
        if (respMsg.msgStateCode != MessageStateCode::kMessageStateCodeSuccess) {
            emit signalLog(
                QString("Server返回错误[%1]：%2").arg(int(respMsg.msgStateCode)).arg(QString::fromStdString(respMsg.comStr)));
            return nullptr;
        }
        // 如果成功，对方必须告知文件传输通道的ID号。
        qDebug() << "Server返回文件传输通道ID号：" << respMsg.comStr;
        return frame;
    });
    //  等待Server通知结果
    //  根据结果进行放弃或者传输
    qDebug() << "Executor.Execute(requestFrame): " << executor.Execute(requestFrame);
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
    checkRet_ = false;
    disableControls();

    workerThread_->invoke([this]() {
        // 1.检查传输TCP是否正常。
        // 2.检查控制TCP是否正常。
        if (!doubleLinker_->waitFileConnect()) {
            emit signalLog("传输TCP连接失败。");
            emit signalCheckUnComplete();
            return;
        }
        emit signalLog("传输TCP连接检查通过。");
        // 3.检查本地根目录是否存在。
        for (const auto& item : data_->fileList) {
            // 文件夹暂时不处理
            if (item.type == RFileType::mTypeDir) {
                continue;
            }
            auto path = FileDir::Join(data_->localRoot, item.name);
            emit signalLog(QString("检查本地文件：%1").arg(path));
            FileMeta meta;
            meta.sizeStr = item.sizeStr.toStdString();
            meta.name = path.toStdString();
            meta.size = item.size;
            fileList_.push_back(meta);
        }
        emit signalLog("本地文件存在检查完成。");
        // 4.检查远程根目录是否存在。
        // 5.如果是上传，检查远端是否已存在相同文件。
        // 6.如果是下载，检查本地是否已存在相同文件。
        emit signalUpdateTable();
        emit signalCheckComplete();
    });
}

void RelayTask::disableControls()
{
    ui->btnBasicCheck->setEnabled(false);
    ui->btnRetryAll->setEnabled(false);
    ui->btnStart->setEnabled(false);
}
void RelayTask::enableControls()
{
    ui->btnBasicCheck->setEnabled(true);
    ui->btnRetryAll->setEnabled(true);
    ui->btnStart->setEnabled(true);
}

void RelayTask::onAppendLog(const QString& log)
{
    auto dt = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    auto msg = "[" + dt + "] " + log;
    ui->pedLog->appendPlainText(msg);
}

void RelayTask::onCheckComplete()
{
    emit signalLog("检查完成");
    checkRet_ = true;
    enableControls();
}

void RelayTask::onCheckUnComplete()
{
    emit signalLog("检查未完成");
    checkRet_ = false;
    ui->btnBasicCheck->setEnabled(true);
    ui->btnRetryAll->setEnabled(false);
    ui->btnStart->setEnabled(false);
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