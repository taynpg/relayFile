#include "RelayTask.h"

#include <QDateTime>
#include <QHeaderView>
#include <Utils/Common.h>

#include "Base/BaseHelper.h"
#include "Base/GuiDefine.hpp"
#include "Base/MessageBoxHelper.h"
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
    if (status_ == RelayTaskStatus::Transing) {
        if (MessageBoxHelper::questionYesNo(this, "确认", "正在传输中，是否确认退出？")) {
            doubleLinker_->Interrupt();
            Quit();
            QDialog::closeEvent(event);
            return;
        }
        event->ignore();
        return;
    }
    Quit();
    QDialog::closeEvent(event);
}

void RelayTask::baseTask()
{
    doubleLinker_ = GlobalData::getInstance()->getDoubleLinker();
    askLocalDf_ = GlobalData::getInstance()->getAskDfLocal();
    askRemoteDf_ = GlobalData::getInstance()->getAskDfRemote();
    workerThread_ = std::make_shared<WorkerThread<RelayTask>>(this);
    workerThread_->start();
    speedTimer_ = new QTimer(this);
    speedTimer_->setInterval(SPEED_TIMER_INTERVAL);
}

void RelayTask::initControl()
{
    ui->edFrom->setEnabled(false);
    ui->edTo->setEnabled(false);
    ui->rbDisconnect->setEnabled(false);
    ui->rbNormal->setEnabled(false);
    ui->pedLog->setEnabled(false);
    // ui->lbSpeed->setEnabled(false);
    ui->rbDisconnect->setChecked(true);
    ui->btnStart->setEnabled(false);
    ui->btnRetryAll->setEnabled(false);
    ui->curProgress->setValue(0);
    ui->lbSpeed->setText("--");

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
    connect(this, &RelayTask::signalNeedConfirmFiles, this, &RelayTask::onConfirmFiles);
    connect(this, &RelayTask::signalTransing, this, &RelayTask::onTransing);
}

void RelayTask::onTransComplete()
{
    speedTimer_->stop();
    enableControls();
    status_ = RelayTaskStatus::TransComplete;
}

void RelayTask::onTransFail()
{
    speedTimer_->stop();
    ui->btnStart->setEnabled(true);
    status_ = RelayTaskStatus::TransFail;
}

void RelayTask::onTransing()
{
    status_ = RelayTaskStatus::Transing;
}

void RelayTask::onStartRun()
{
    disableControls();
    speedTimer_->start();
    workerThread_->invoke([this]() {
        bool allSuccess = true;
        auto rows = tableWidget_->rowCount();
        emit signalTransing();
        for (int i = 0; i < rows; ++i) {
            auto* itemState = tableWidget_->item(i, 3);
            if (itemState->text() != GUI_FILE_TRAN_STATE_WAIT) {
                continue;
            }
            clearData();
            onStartFresh(i);
            startTime_ = std::chrono::steady_clock::now();
            bool handleSuccess = handleOneLine(i);
            if (!handleSuccess) {
                allSuccess = false;
                break;
            }
        }
        if (allSuccess) {
            emit signalTransComplete();
        } else {
            emit signalTransFail();
        }
    });
}

void RelayTask::GenOtherMetaPath(const FileMeta& in, FileMeta& out, bool isSend, const QString& localRoot,
                                 const QString& remoteRoot)
{
    out = in;
    auto fullPath = FileDir::GenOutPath(isSend ? localRoot : remoteRoot, in.fullPath, isSend ? remoteRoot : localRoot);
    out.fullPath = fullPath.toStdString();
    out.name = FileDir::GenFileName(fullPath).toStdString();
    out.dir = FileDir::GenDir(fullPath).toStdString();
}

bool RelayTask::handleOneLine(int row)
{
    int id = tableWidget_->item(row, 0)->text().toInt();
    if (id >= fileList_.size()) {
        return false;
    }

    //  等待Server通知结果
    //  根据结果进行放弃或者传输
    auto execRet = doubleLinker_->RunTaskItem(transItems_[id]);
    qDebug() << "handleOneLine: " << execRet;

    if (execRet) {
        emit signalLog("传输执行成功。");
        onSuccessFresh(row);
        return true;
    } else {
        emit signalLog("传输执行失败。");
        onFailFresh(row);
        return false;
    }
}

void RelayTask::onStartFresh(int row)
{
    QMetaObject::invokeMethod(this, [this, row]() { tableWidget_->item(row, 3)->setText(GUI_FILE_TRAN_STATE_TRANS); });
}

void RelayTask::onFailFresh(int row)
{
    QMetaObject::invokeMethod(this, [this, row]() { tableWidget_->item(row, 3)->setText(GUI_FILE_TRAN_STATE_FAILED); });
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
    QMetaObject::invokeMethod(this, [this, row]() { tableWidget_->item(row, 3)->setText(GUI_FILE_TRAN_STATE_DONE); });
}

void RelayTask::setData(std::shared_ptr<RelayTaskData> data)
{
    data_ = data;
}

void RelayTask::showEvent(QShowEvent* event)
{
    if (data_->isUpload) {
        setWindowTitle("上传任务");
    } else {
        setWindowTitle("下载任务");
    }
    QDialog::showEvent(event);
}

void RelayTask::onBaseCheck()
{
    emit signalLog("开始检查基础条件...");
    checkRet_ = false;
    disableControls();

    workerThread_->invoke([this]() {
        status_ = RelayTaskStatus::Checking;
        // 1.检查传输TCP是否正常。
        // 2.检查控制TCP是否正常。
        if (!doubleLinker_->waitFileConnect()) {
            emit signalLog("传输TCP连接失败。");
            emit signalCheckUnComplete();
            return;
        }
        emit signalLog("传输TCP连接检查通过。");
        fileList_.clear();
        // 3.检查本地根目录是否存在。
        std::shared_ptr<BaseAskDF> askDfOwn = data_->isUpload ? askLocalDf_ : askRemoteDf_;
        std::shared_ptr<BaseAskDF> askDfOther = data_->isUpload ? askRemoteDf_ : askLocalDf_;
        auto name = data_->isUpload ? GUI_DIRECTION_LOCAL : GUI_DIRECTION_REMOTE;

        for (const auto& item : data_->fileList) {
            auto path = FileDir::Join(data_->isUpload ? item.localRoot : item.remoteRoot, item.name);
            if (item.type == RFileType::mTypeDir) {
                std::vector<FileMeta> fileList;
                if (!askDfOwn->AskFileList(path.toStdString(), fileList, true)) {
                    emit signalLog(QString("获取目录内容：%1 失败。").arg(path));
                    emit signalCheckUnComplete();
                    return;
                }
                fileList_.insert(fileList_.end(), fileList.begin(), fileList.end());
                continue;
            }
            emit signalLog(QString("检查%1文件：%2").arg(name).arg(path));
            FileMeta meta;
            meta.localRoot = item.localRoot.toStdString();
            meta.remoteRoot = item.remoteRoot.toStdString();
            meta.dir = data_->isUpload ? item.localRoot.toStdString() : item.remoteRoot.toStdString();
            meta.sizeStr = item.sizeStr.toStdString();
            meta.name = item.name.toStdString();
            meta.fullPath = path.toStdString();
            meta.size = item.size;
            fileList_.push_back(meta);
        }
        FileMeta tmpMeta;
        for (auto& item : fileList_) {
            if (!askDfOwn->AskFileMeta(item.fullPath, tmpMeta)) {
                emit signalLog(QString("%1文件文件存在性检查：%2 失败。").arg(name).arg(item.fullPath));
                emit signalCheckUnComplete();
                return;
            }
            if (tmpMeta.exist == 0) {
                emit signalLog(QString("%1文件：%2 不存在。").arg(name).arg(item.fullPath));
                emit signalCheckUnComplete();
                return;
            }
            item.size = tmpMeta.size;
        }
        emit signalUpdateTable();
        emit signalLog("源端文件存在性检查完成。");
        emit signalLog("开始校验目标端文件是否已存在相同文件。");

        needConfirmFiles_.clear();
        needRemoveTaskFiles_.clear();

        // 存在性检查需要生成路径
        transItems_.clear();
        for (auto& item : fileList_) {
            auto trItem = std::make_shared<TransItem>();
            trItem->isSend = data_->isUpload;
            trItem->from = item;
            GenOtherMetaPath(item, trItem->to, data_->isUpload, QString::fromStdString(item.localRoot),
                             QString::fromStdString(item.remoteRoot));
            transItems_.push_back(trItem);
        }

        auto nameConfirm = data_->isUpload ? GUI_DIRECTION_REMOTE : GUI_DIRECTION_LOCAL;
        for (const auto& item : transItems_) {
            if (!askDfOther->AskFileMeta(item->to.fullPath, tmpMeta)) {
                emit signalLog(QString("%1文件文件存在性检查：%2 失败。").arg(nameConfirm).arg(item->to.fullPath));
                emit signalCheckUnComplete();
                return;
            }
            if (tmpMeta.exist != 0) {
                emit signalLog(QString("%1文件：%2 已存在相同文件。").arg(nameConfirm).arg(item->to.fullPath));
                needConfirmFiles_.push_back(item->to);
            }
        }
        emit signalNeedConfirmFiles();
    });
}

void RelayTask::onConfirmFiles()
{
    if (needConfirmFiles_.empty()) {
        emit signalCheckComplete();
        return;
    }
    bool needAsk = true;
    auto nameConfirm = data_->isUpload ? GUI_DIRECTION_REMOTE : GUI_DIRECTION_LOCAL;
    for (const auto& item : needConfirmFiles_) {
        if (!needAsk) {
            break;
        }
        auto r = MessageBoxHelper::questionFourButtons(
            this, "警告", QString("已存在%1文件%2, 是否覆盖？").arg(nameConfirm).arg(QString::fromStdString(item.fullPath)));
        if (r == MessageBoxHelper::Result::No) {
            needRemoveTaskFiles_.push_back(item);
        } else if (r == MessageBoxHelper::Result::Exit) {
            emit signalCheckUnComplete();
        } else if (r == MessageBoxHelper::Result::ALL) {
            needAsk = false;
        }
    }
    for (const auto& item : needRemoveTaskFiles_) {
        QMetaObject::invokeMethod(this, [this, item]() {
            auto qFull = QString::fromStdString(item.fullPath);
            if (curTableData_.count(qFull)) {
                auto* item = tableWidget_->item(curTableData_[qFull], 3);
                item->setText(GUI_FILE_TRAN_STATE_SKIP);
            }
        });
    }
    emit signalCheckComplete();
}

bool RelayTask::normalCheckFileExist()
{
    return false;
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
    curTableData_.clear();

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
    ui->edFrom->setText(from);
    ui->edTo->setText(to);
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
    curTableData_[QString::fromStdString(meta.fullPath)] = row;

    auto* indexItem = new QTableWidgetItem(QString::number(index));
    indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 0, indexItem);

    auto* nameItem = new QTableWidgetItem(QString::fromStdString(meta.fullPath));
    nameItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 1, nameItem);

    auto sizeStr = miniUtil::GetSizeInfo(meta.size);
    auto* sizeItem = new QTableWidgetItem(QString::fromStdString(sizeStr));
    sizeItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 2, sizeItem);

    auto* stateItem = new QTableWidgetItem(GUI_FILE_TRAN_STATE_WAIT);
    stateItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 3, stateItem);

    auto* speedItem = new QTableWidgetItem("N/A");
    speedItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 4, speedItem);

    auto* useItem = new QTableWidgetItem("N/A");
    useItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 5, useItem);
}
