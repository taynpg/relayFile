#include "RelayTask.h"

#include <QDateTime>
#include <QHeaderView>
#include <Utils/Common.h>

#include "Base/BaseHelper.h"
#include "Protocol/Serialize.hpp"
#include "ui_RelayTask.h"

constexpr int SPEED_TIMER_INTERVAL = 500;

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
    speedTimer_ = new QTimer(this);
    speedTimer_->setInterval(SPEED_TIMER_INTERVAL);
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
    // ui->lbSpeed->setEnabled(false);
    ui->rbDisconnect->setChecked(true);
    ui->btnStart->setEnabled(false);
    ui->btnRetryAll->setEnabled(false);

    tableWidget_ = new QTableWidget();
    tableWidget_->setColumnCount(6);
    tableWidget_->setHorizontalHeaderLabels({"序号", "名称", "大小", "状态", "平均速度", "用时"});
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget_->setContextMenuPolicy(Qt::CustomContextMenu);

    tableWidget_->setColumnWidth(0, 50);
    tableWidget_->setColumnWidth(2, 100);
    tableWidget_->setColumnWidth(3, 100);
    tableWidget_->setColumnWidth(4, 130);
    tableWidget_->setColumnWidth(5, 120);
    tableWidget_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tableWidget_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);

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
    connect(this, &RelayTask::signalTransComplete, this, &RelayTask::onTransComplete);
    connect(this, &RelayTask::signalTransFail, this, &RelayTask::onTransFail);
    connect(doubleLinker_.get(), &DoubleLinker::signalCurFileProgress, this, &RelayTask::onCurFileProgress);
    connect(doubleLinker_.get(), &DoubleLinker::signalCurFileItem, this, &RelayTask::onCurFileItem);
    connect(speedTimer_, &QTimer::timeout, this, &RelayTask::onRefreshSpeed);
}

void RelayTask::onTransComplete()
{
    speedTimer_->stop();
    enableControls();
}

void RelayTask::onTransFail()
{
    speedTimer_->stop();
    ui->btnStart->setEnabled(true);
}

void RelayTask::onStartRun()
{
    disableControls();
    clearData();
    speedTimer_->start();
    startTime_ = std::chrono::steady_clock::now();
    workerThread_->invoke([this]() { handleOneLine(0); });
}

std::shared_ptr<TransItem> RelayTask::getTransItem(const FileMeta& meta)
{
    auto taskItem = std::make_shared<TransItem>();
    taskItem->from = meta;
    taskItem->to = meta;
    taskItem->isSend = data_->isUpload;

    auto oPath = FileDir::GenOutPath(data_->isUpload ? data_->localRoot : data_->remoteRoot, taskItem->from.fullPath,
                                     data_->isUpload ? data_->remoteRoot : data_->localRoot);

    taskItem->to.fullPath = oPath.toStdString();
    taskItem->to.name = FileDir::GenFileName(oPath).toStdString();
    taskItem->to.dir = FileDir::GenDir(oPath).toStdString();
    return taskItem;
}

void RelayTask::handleOneLine(int row)
{
    int id = tableWidget_->item(row, 0)->text().toInt();
    if (id >= fileList_.size()) {
        return;
    }

    const auto& fileMeta = fileList_[id];
    auto taskItem = getTransItem(fileMeta);

    //  等待Server通知结果
    //  根据结果进行放弃或者传输
    auto execRet = doubleLinker_->RunTaskItem(taskItem);
    qDebug() << "handleOneLine: " << execRet;

    if (execRet) {
        emit signalLog("传输执行成功。");
        onSuccessFresh(row);
        emit signalTransComplete();
    } else {
        emit signalLog("传输执行失败。");
        onFailFresh(row);
        emit signalTransFail();
    }
}

void RelayTask::onFailFresh(int row)
{
    QMetaObject::invokeMethod(this, [this, row]() { tableWidget_->item(row, 3)->setText("失败"); });
}

void RelayTask::onSuccessFresh(int row)
{
    auto stopPoint = std::chrono::steady_clock::now();
    auto useTime = std::chrono::duration_cast<std::chrono::milliseconds>(stopPoint - startTime_);
    auto speedSize = totalSize_ * 1.0 / useTime.count();
    auto speedStr = getSpeedStr(speedSize * 1000);
    auto useTimeStr = miniUtil::GetTimeInfo(useTime.count());
    QMetaObject::invokeMethod(this, [this, row, speedStr]() { tableWidget_->item(row, 4)->setText(speedStr); });
    QMetaObject::invokeMethod(
        this, [this, row, useTimeStr]() { tableWidget_->item(row, 5)->setText(QString::fromStdString(useTimeStr)); });
    QMetaObject::invokeMethod(this, [this, row]() { tableWidget_->item(row, 3)->setText("已完成"); });
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
            meta.dir = data_->localRoot.toStdString();
            meta.sizeStr = item.sizeStr.toStdString();
            meta.name = item.name.toStdString();
            meta.fullPath = path.toStdString();
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

void RelayTask::onCurFileProgress(std::uint64_t transed, std::uint64_t total)
{
    auto cur = transed * 1.0 / total;
    ui->curProgress->setValue(int(cur * 100));
    totalSize_ = total;
    curTransed_ = transed;
}

void RelayTask::clearData()
{
    preTransed_ = 0;
    totalSize_ = 0;
    curTransed_ = 0;
}

void RelayTask::onCurFileItem(const QString& from, const QString& to)
{
}

QString RelayTask::getSpeedStr(uint64_t transed)
{
    double speed = static_cast<double>(transed);
    QString unit = "B/s";

    if (speed >= 1024.0) {
        speed /= 1024.0;
        unit = "KB/s";

        if (speed >= 1024.0) {
            speed /= 1024.0;
            unit = "MB/s";

            if (speed >= 1024.0) {
                speed /= 1024.0;
                unit = "GB/s";
            }
        }
    }
    return QString("%1 %2").arg(speed, 0, 'f', 2).arg(unit);
}

void RelayTask::onRefreshSpeed()
{
    auto speedStr = getSpeedStr((curTransed_ - preTransed_) * (1000 / SPEED_TIMER_INTERVAL));
    ui->lbSpeed->setText(speedStr);
    preTransed_ = curTransed_;
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

    auto* speedItem = new QTableWidgetItem("N/A");
    speedItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 4, speedItem);

    auto* useItem = new QTableWidgetItem("N/A");
    useItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 5, useItem);
}