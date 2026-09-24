#include "RelayTask.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QUuid>
#include <future>
#include <Utils/Common.h>

#include "Base/BaseHelper.h"
#include "Base/GuiDefine.h"
#include "Base/MessageBoxHelper.h"
#include "Compress/TarXzPacker.h"
#include "Protocol/Message.h"
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
    // 取消压缩下载的打包等待
    if (archiveWaitState_) {
        QMutexLocker locker(&archiveWaitState_->mutex);
        archiveWaitState_->cancelled = true;
        archiveWaitState_->cond.wakeAll();
    }
    doubleLinker_->GetControlSession()->onCancelWaitMsg();
    workerThread_->stop();
    workerThread_->quit();
}

void RelayTask::closeEvent(QCloseEvent* event)
{
    if (status_ == RelayTaskStatus::Transing) {
        if (MessageBoxHelper::questionYesNo(this, "确认", "正在传输中，是否确认退出？")) {
            doubleLinker_->clearCurrentTaskItem();
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
    ui->pedLog->setEnabled(false);
    // ui->lbSpeed->setEnabled(false);
    ui->btnStart->setEnabled(false);
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

    // 分页导航条
    btnFirst_ = new QPushButton("首页", this);
    btnPrev_ = new QPushButton("上一页", this);
    btnNext_ = new QPushButton("下一页", this);
    btnLast_ = new QPushButton("末页", this);
    pageLabel_ = new QLabel("第 1/1 页", this);
    pageLabel_->setAlignment(Qt::AlignCenter);
    auto* pageSizeLabel = new QLabel("每页:", this);
    pageSizeCombo_ = new QComboBox(this);
    pageSizeCombo_->addItems({"50", "100", "200", "500", "全部"});
    pageSizeCombo_->setCurrentText("100");

    auto* navLayout = new QHBoxLayout();
    navLayout->addWidget(btnFirst_);
    navLayout->addWidget(btnPrev_);
    navLayout->addStretch();
    navLayout->addWidget(pageLabel_);
    navLayout->addStretch();
    navLayout->addWidget(btnNext_);
    navLayout->addWidget(btnLast_);
    navLayout->addSpacing(24);
    navLayout->addWidget(pageSizeLabel);
    navLayout->addWidget(pageSizeCombo_);

    auto* layout = new QVBoxLayout();
    layout->addWidget(tableWidget_);
    layout->addLayout(navLayout);
    layout->setContentsMargins(0, 0, 0, 0);
    ui->widget->setLayout(layout);
}

void RelayTask::initSignals()
{
    connect(this, &RelayTask::signalLog, this, &RelayTask::onAppendLog);
    connect(this, &RelayTask::signalCheckComplete, this, &RelayTask::onCheckComplete);
    connect(ui->btnBasicCheck, &QPushButton::clicked, this, &RelayTask::onBaseCheck);
    connect(ui->btnStart, &QPushButton::clicked, this, &RelayTask::onStartRun);
    connect(this, &RelayTask::signalAutoStart, this, &RelayTask::onStartRun);
    connect(this, &RelayTask::signalUpdateTable, this, &RelayTask::updateTable);
    connect(this, &RelayTask::signalTransComplete, this, &RelayTask::onTransComplete);
    connect(this, &RelayTask::signalTransFail, this, &RelayTask::onTransFail);
    connect(doubleLinker_.get(), &DoubleLinker::signalCurFileProgress, this, &RelayTask::onCurFileProgress);
    connect(doubleLinker_.get(), &DoubleLinker::signalCurFileItem, this, &RelayTask::onCurFileItem);
    connect(speedTimer_, &QTimer::timeout, this, &RelayTask::onRefreshSpeed);
    connect(this, &RelayTask::signalNeedConfirmFiles, this, &RelayTask::onConfirmFiles);
    connect(this, &RelayTask::signalTransing, this, &RelayTask::onTransing);
    connect(this, &RelayTask::signalCancelWaitMsg, doubleLinker_.get(), &DoubleLinker::onCancelWaitMsg);
    connect(this, &RelayTask::signalCheckUnComplete, this, &RelayTask::onCheckUnComplete);
    connect(btnFirst_, &QPushButton::clicked, this, &RelayTask::onFirstPage);
    connect(btnPrev_, &QPushButton::clicked, this, &RelayTask::onPrevPage);
    connect(btnNext_, &QPushButton::clicked, this, &RelayTask::onNextPage);
    connect(btnLast_, &QPushButton::clicked, this, &RelayTask::onLastPage);
    connect(pageSizeCombo_, &QComboBox::currentTextChanged, this, [this]() { onPageSizeChanged(); });
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
    if (!checkRet_) {
        MessageBoxHelper::information(this, "提示", "基本检查未通过。");
        return;
    }
    disableControls();
    speedTimer_->start();
    // 主线程快照待处理项（状态为 WAIT 的数据索引），避免 worker 线程直接读模型产生竞态
    std::vector<int> todo;
    todo.reserve(fileList_.size());
    for (int id = 0; id < (int)fileList_.size(); ++id) {
        if (rowDisplay_[id].status == GUI_FILE_TRAN_STATE_WAIT) {
            todo.push_back(id);
        }
    }
    workerThread_->invoke([this, todo]() {
        bool allSuccess = true;
        emit signalTransing();

        // 压缩传输：启用压缩时，将所有待传文件打包为单个 tar.xz 归档。
        //   - 上传：本地为发送方，本地打包后发出，mark=2 通知远端收完即解包。
        //   - 下载：远端为发送方，本地通过 mark=2 请求远端打包后发出，远端收完
        //     解包。压缩意图按请求（UUID）传递，同一远端可对不同请求方区别响应。
        CompressConfig cfg;
        GlobalData::getInstance()->getBaseConfig()->getCompress(cfg);

        if (cfg.enabled && !todo.empty()) {
            std::uint64_t thresholdBytes = static_cast<std::uint64_t>(cfg.thresholdMB) * 1024 * 1024;

            if (data_->isUpload) {
                // 上传：本地打包
                std::vector<PackItem> packItems;
                packItems.reserve(todo.size());
                for (int id : todo) {
                    const auto& item = transItems_[id];
                    PackItem pi;
                    pi.srcPath = item->from.fullPath;
                    pi.destPath = item->to.fullPath;
                    pi.permission = item->from.permission;
                    packItems.push_back(pi);
                }

                QString archivePath =
                    QDir::tempPath() + QString("/relay_archive_%1.tar.xz").arg(Common::GetUUID());
                std::string err;
                std::uint64_t archiveSize = 0;
                emit signalLog(QString("开始打包 %1 个文件到归档...").arg(todo.size()));
                if (!TarXzPacker::pack(packItems, archivePath.toStdString(), archiveSize, err)) {
                    emit signalLog(QString("打包归档失败: %1").arg(QString::fromStdString(err)));
                    for (int id : todo) {
                        onFailFresh(id);
                    }
                    emit signalTransFail();
                    return;
                }
                emit signalLog(QString("归档打包完成，大小: %1")
                                   .arg(QString::fromStdString(miniUtil::GetSizeInfo(archiveSize))));

                // 阈值检测：超阈值则放弃本次传输（不拆分、不回退到直传）
                if (archiveSize > thresholdBytes) {
                    emit signalLog(
                        QString("归档大小 %1MB 超过阈值 %2MB，放弃本次传输。")
                            .arg(archiveSize / (1024 * 1024))
                            .arg(cfg.thresholdMB));
                    QFile::remove(archivePath);
                    for (int id : todo) {
                        onFailFresh(id);
                    }
                    emit signalTransFail();
                    return;
                }

                auto archiveItem = std::make_shared<TransItem>();
                archiveItem->isSend = true;
                archiveItem->isArchive = true;
                archiveItem->from.fullPath = archivePath.toStdString();
                archiveItem->from.size = archiveSize;
                archiveItem->from.name = FileDir::GenFileName(archivePath).toStdString();
                archiveItem->from.dir = QDir::tempPath().toStdString();
                archiveItem->from.exist = 1;
                archiveItem->to.fullPath = "relay_archive.tar.xz";
                archiveItem->to.size = archiveSize;
                archiveItem->to.name = "relay_archive.tar.xz";
                archiveItem->to.exist = 0;

                clearData();
                for (int id : todo) {
                    onStartFresh(id);
                }
                startTime_ = std::chrono::steady_clock::now();

                emit signalLog("开始传输归档...");
                auto execRet = doubleLinker_->RunTaskItem(archiveItem);
                doubleLinker_->clearCurrentTaskItem();

                QFile::remove(archivePath);

                if (execRet) {
                    emit signalLog(QString("归档传输成功，共 %1 个文件。").arg(todo.size()));
                    for (int id : todo) {
                        onSuccessFresh(id);
                    }
                    emit signalTransComplete();
                } else {
                    emit signalLog("归档传输失败。");
                    for (int id : todo) {
                        onFailFresh(id);
                    }
                    emit signalTransFail();
                }
                return;
            } else {
                // 下载：通过控制消息请求远端打包，打包完成后复用普通下载流程。
                std::vector<std::string> packList;
                packList.reserve(todo.size() * 2);
                for (int id : todo) {
                    const auto& item = transItems_[id];
                    packList.push_back(item->from.fullPath);   // 远端源路径
                    packList.push_back(item->to.fullPath);     // 本地目的路径
                }

                clearData();
                for (int id : todo) {
                    onStartFresh(id);
                }
                startTime_ = std::chrono::steady_clock::now();

                auto controlSession = doubleLinker_->GetControlSession();

                // 注册远端打包完成通知的处理。
                archiveWaitState_ = std::make_shared<ArchiveWaitState>();
                auto waitState = archiveWaitState_;
                controlSession->RegisterPubCall(FrameType::kMsgType_Ask_ArchiveReady,
                                                [waitState](FramePtr frame) {
                                                    QMutexLocker locker(&waitState->mutex);
                                                    if (waitState->cancelled) {
                                                        return;
                                                    }
                                                    waitState->ready = true;
                                                    waitState->frame = frame;
                                                    waitState->cond.wakeAll();
                                                });

                emit signalLog(QString("请求远端打包 %1 个文件...").arg(todo.size()));

                // 第一回合：发送打包请求，等待"开始打包"确认。
                Message packMsg;
                packMsg.strVec = packList;
                packMsg.comStr = std::to_string(thresholdBytes);
                packMsg.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
                auto promise = std::make_shared<std::promise<MessagePtr>>();
                auto future = promise->get_future();
                controlSession->SendWithCall(packMsg, FrameType::kMsgType_Ask_ArchivePack,
                                             [promise](MessagePtr ans) { promise->set_value(ans); });

                MessagePtr packAns = future.get();
                if (!packAns || packAns->msgStateCode != MessageStateCode::kMessageStateCodeSuccess) {
                    emit signalLog("远端拒绝打包请求。");
                    controlSession->RegisterPubCall(FrameType::kMsgType_Ask_ArchiveReady, nullptr);
                    archiveWaitState_.reset();
                    for (int id : todo) {
                        onFailFresh(id);
                    }
                    emit signalTransFail();
                    return;
                }
                emit signalLog("远端正在打包归档...");

                // 第二回合：等待远端打包完成通知（可被 Quit 取消）。
                FramePtr readyFrame;
                {
                    QMutexLocker locker(&waitState->mutex);
                    while (!waitState->ready && !waitState->cancelled) {
                        waitState->cond.wait(&waitState->mutex, 1000);
                    }
                    if (waitState->cancelled || !waitState->ready) {
                        controlSession->RegisterPubCall(FrameType::kMsgType_Ask_ArchiveReady, nullptr);
                        archiveWaitState_.reset();
                        emit signalLog("打包等待已取消。");
                        for (int id : todo) {
                            onFailFresh(id);
                        }
                        emit signalTransFail();
                        return;
                    }
                    readyFrame = waitState->frame;
                }
                controlSession->RegisterPubCall(FrameType::kMsgType_Ask_ArchiveReady, nullptr);
                archiveWaitState_.reset();

                Message readyMsg;
                deserializeStruct(readyFrame->data, readyMsg);
                if (readyMsg.msgStateCode != MessageStateCode::kMessageStateCodeSuccess) {
                    emit signalLog(QString("远端打包失败: %1").arg(QString::fromStdString(readyMsg.errMsg)));
                    for (int id : todo) {
                        onFailFresh(id);
                    }
                    emit signalTransFail();
                    return;
                }

                // 第三回合：复用普通下载流程下载归档文件（mark=2 通知本地解包）。
                auto archiveItem = std::make_shared<TransItem>();
                archiveItem->isSend = false;
                archiveItem->isArchive = true;
                archiveItem->from = readyMsg.ff;
                archiveItem->to.fullPath = "relay_archive.tar.xz";
                archiveItem->to.name = "relay_archive.tar.xz";

                emit signalLog(QString("归档打包完成，大小: %1，开始下载...")
                                   .arg(QString::fromStdString(miniUtil::GetSizeInfo(readyMsg.ff.size))));
                auto execRet = doubleLinker_->RunTaskItem(archiveItem);
                doubleLinker_->clearCurrentTaskItem();

                if (execRet) {
                    emit signalLog(QString("归档下载成功，共 %1 个文件。").arg(todo.size()));
                    for (int id : todo) {
                        onSuccessFresh(id);
                    }
                    emit signalTransComplete();
                } else {
                    emit signalLog("归档下载失败。");
                    for (int id : todo) {
                        onFailFresh(id);
                    }
                    emit signalTransFail();
                }
                return;
            }
        }

        for (int id : todo) {
            if (rowDisplay_[id].status != GUI_FILE_TRAN_STATE_WAIT) {
                continue;
            }
            clearData();
            onStartFresh(id);
            startTime_ = std::chrono::steady_clock::now();
            bool handleSuccess = handleOneLine(id);
            doubleLinker_->clearCurrentTaskItem();
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
    out.dir = FileDir::cdUp(fullPath).toStdString();
}

bool RelayTask::handleOneLine(int id)
{
    if (id < 0 || id >= (int)fileList_.size()) {
        return false;
    }

    //  等待Server通知结果
    //  根据结果进行放弃或者传输
    auto execRet = doubleLinker_->RunTaskItem(transItems_[id]);

    if (execRet) {
        emit signalLog("传输执行成功。");
        onSuccessFresh(id);
        return true;
    } else {
        emit signalLog("传输执行失败。");
        onFailFresh(id);
        return false;
    }
}

void RelayTask::onStartFresh(int id)
{
    QMetaObject::invokeMethod(this, [this, id]() {
        rowDisplay_[id].status = GUI_FILE_TRAN_STATE_TRANS;
        refreshVisibleCell(id);
    });
}

void RelayTask::onFailFresh(int id)
{
    QMetaObject::invokeMethod(this, [this, id]() {
        rowDisplay_[id].status = GUI_FILE_TRAN_STATE_FAILED;
        refreshVisibleCell(id);
    });
}

void RelayTask::onSuccessFresh(int id)
{
    auto stopPoint = std::chrono::steady_clock::now();
    auto useTime = std::chrono::duration_cast<std::chrono::milliseconds>(stopPoint - startTime_);
    auto speedSize = totalSize_ * 1.0 / useTime.count();
    auto speedStr = getSpeedStr(speedSize * 1000);
    auto useTimeStr = miniUtil::GetTimeInfo(useTime.count());
    QMetaObject::invokeMethod(this, [this, id, speedStr, useTimeStr]() {
        rowDisplay_[id].speedStr = speedStr;
        rowDisplay_[id].useTimeStr = QString::fromStdString(useTimeStr);
        rowDisplay_[id].status = GUI_FILE_TRAN_STATE_DONE;
        refreshVisibleCell(id);
    });
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
    onBaseCheck();
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
                for (auto& tmpItem : fileList) {
                    tmpItem.localRoot = item.localRoot.toStdString();
                    tmpItem.remoteRoot = item.remoteRoot.toStdString();
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
                emit signalLog(QString("%1文件文件存在性检查：%2 失败。").arg(name).arg(QString::fromStdString(item.fullPath)));
                emit signalCheckUnComplete();
                return;
            }
            if (tmpMeta.exist == 0) {
                emit signalLog(QString("%1文件：%2 不存在。").arg(name).arg(QString::fromStdString(item.fullPath)));
                emit signalCheckUnComplete();
                return;
            }
            // 发送方的信息
            item.size = tmpMeta.size;
            item.permission = tmpMeta.permission;
            item.mark = tmpMeta.mark;
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
        for (auto& item : transItems_) {
            if (!askDfOther->AskFileMeta(item->to.fullPath, tmpMeta)) {
                emit signalLog(
                    QString("%1文件文件存在性检查：%2 失败。").arg(nameConfirm).arg(QString::fromStdString(item->to.fullPath)));
                emit signalCheckUnComplete();
                return;
            }
            if (tmpMeta.exist != 0) {
                emit signalLog(
                    QString("%1文件：%2 已存在相同文件。").arg(nameConfirm).arg(QString::fromStdString(item->to.fullPath)));

                // 内容粗判：先比大小，大小一致再各抽取 10 个采样块比较
                bool contentSame = false;
                if (item->from.size == tmpMeta.size) {
                    std::vector<SampleBlock> ownSamples;
                    std::vector<SampleBlock> otherSamples;
                    if (askDfOwn->AskFileSamples(item->from.fullPath, ownSamples)
                        && askDfOther->AskFileSamples(item->to.fullPath, otherSamples)
                        && ownSamples.size() == otherSamples.size()) {
                        contentSame = true;
                        for (size_t i = 0; i < ownSamples.size(); ++i) {
                            if (ownSamples[i].offset != otherSamples[i].offset
                                || ownSamples[i].data != otherSamples[i].data) {
                                contentSame = false;
                                break;
                            }
                        }
                    }
                }
                emit signalLog(QString("文件：%1 内容粗判%2。")
                                   .arg(QString::fromStdString(item->to.fullPath))
                                   .arg(contentSame ? "一致" : "不一致"));
                needConfirmFiles_.push_back({item->to, contentSame});
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
    bool autoSkipSame = false;   // 一级全否：粗判一致的静默跳过，不一致的继续询问
    bool autoSkipAll = false;    // 二级全否：剩余全部静默跳过
    for (const auto& info : needConfirmFiles_) {
        const auto& item = info.file;
        if (autoSkipAll || (info.contentSame && autoSkipSame)) {
            needRemoveTaskFiles_.push_back(item);
            continue;
        }
        QString prompt = info.contentSame
                             ? QString("内容粗判一致，是否覆盖？\n%1").arg(QString::fromStdString(item.fullPath))
                             : QString("内容不一致，是否覆盖？\n%1").arg(QString::fromStdString(item.fullPath));
        auto r = MessageBoxHelper::questionFiveButtons(this, "警告", prompt);
        if (r == MessageBoxHelper::Result::No) {
            needRemoveTaskFiles_.push_back(item);
        } else if (r == MessageBoxHelper::Result::Exit) {
            emit signalCheckUnComplete();
            return;
        } else if (r == MessageBoxHelper::Result::ALL) {
            // 全是：剩余全部覆盖，不再询问
            break;
        } else if (r == MessageBoxHelper::Result::ALL_NO) {
            needRemoveTaskFiles_.push_back(item);
            if (info.contentSame) {
                // 一级：粗判一致的不再询问，仅内容不一致的继续询问
                autoSkipSame = true;
                emit signalLog("全否：粗判一致文件将自动跳过，内容不一致的仍会询问。");
            } else {
                // 二级：在内容不一致文件上点全否，剩余文件全部跳过
                autoSkipAll = true;
                emit signalLog("全否：剩余文件全部跳过。");
            }
        }
        // Yes：覆盖当前文件，继续询问下一个
    }
    for (const auto& item : needRemoveTaskFiles_) {
        auto fileName = QString::fromStdString(item.name);
        for (int id = 0; id < (int)fileList_.size(); ++id) {
            if (QString::fromStdString(fileList_[id].name) == fileName) {
                rowDisplay_[id].status = GUI_FILE_TRAN_STATE_SKIP;
                refreshVisibleCell(id);
                break;
            }
        }
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
    ui->btnStart->setEnabled(false);
}
void RelayTask::enableControls()
{
    ui->btnBasicCheck->setEnabled(true);
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
    emit signalAutoStart();
}

void RelayTask::onCheckUnComplete()
{
    emit signalLog("检查未完成");
    checkRet_ = false;
    ui->btnBasicCheck->setEnabled(true);
    ui->btnStart->setEnabled(false);
}

void RelayTask::updateTable()
{
    // 重建显示模型（按数据索引），与可见页解耦
    rowDisplay_.assign(fileList_.size(), RowDisplay{});
    curPage_ = 0;
    renderPage();
}

void RelayTask::renderRow(int viewRow, int id)
{
    const auto& meta = fileList_[id];
    const auto& d = rowDisplay_[id];

    auto mkItem = [](const QString& text) {
        auto* it = new QTableWidgetItem(text);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        return it;
    };

    tableWidget_->setItem(viewRow, 0, mkItem(QString::number(id)));
    tableWidget_->setItem(viewRow, 1, mkItem(QString::fromStdString(meta.fullPath)));
    tableWidget_->setItem(viewRow, 2, mkItem(QString::fromStdString(miniUtil::GetSizeInfo(meta.size))));
    tableWidget_->setItem(viewRow, 3, mkItem(d.status));
    tableWidget_->setItem(viewRow, 4, mkItem(d.speedStr));
    tableWidget_->setItem(viewRow, 5, mkItem(d.useTimeStr));
}

void RelayTask::renderPage()
{
    int total = (int)fileList_.size();
    int pages = (total <= 0) ? 1 : (total + pageSize_ - 1) / pageSize_;
    if (curPage_ >= pages) curPage_ = pages - 1;
    if (curPage_ < 0) curPage_ = 0;

    tableWidget_->setRowCount(0);
    tableWidget_->clearContents();

    if (total <= 0) {
        updatePageLabel();
        return;
    }

    int start = curPage_ * pageSize_;
    int end = std::min(start + pageSize_, total);
    tableWidget_->setRowCount(end - start);

    bool paginate = pageSize_ < total;
    for (int id = start; id < end; ++id) {
        renderRow(id - start, id);
        if (paginate && (id - start) % 30 == 29) {
            QGuiApplication::processEvents();
        }
    }
    updatePageLabel();
}

void RelayTask::gotoPage(int page)
{
    int total = (int)fileList_.size();
    int pages = (total <= 0) ? 1 : (total + pageSize_ - 1) / pageSize_;
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;
    if (page == curPage_) return;
    curPage_ = page;
    renderPage();
}

void RelayTask::updatePageLabel()
{
    int total = (int)fileList_.size();
    int pages = (total <= 0) ? 1 : (total + pageSize_ - 1) / pageSize_;
    if (curPage_ >= pages) curPage_ = pages - 1;
    if (curPage_ < 0) curPage_ = 0;
    int start = (total == 0) ? 0 : curPage_ * pageSize_ + 1;
    int end = (total == 0) ? 0 : std::min((curPage_ + 1) * pageSize_, total);
    pageLabel_->setText(QString("第 %1/%2 页 ｜ 条目 %3-%4 / 共 %5").arg(curPage_ + 1).arg(pages).arg(start).arg(end).arg(total));
    btnFirst_->setEnabled(curPage_ > 0);
    btnPrev_->setEnabled(curPage_ > 0);
    btnNext_->setEnabled(curPage_ < pages - 1);
    btnLast_->setEnabled(curPage_ < pages - 1);
}

void RelayTask::refreshVisibleCell(int id)
{
    if (id < 0 || id >= (int)rowDisplay_.size()) return;
    int start = curPage_ * pageSize_;
    int end = start + pageSize_;
    if (id < start || id >= end) return;  // 不在当前页：只更新模型，切到该页时 renderPage 会渲染
    int viewRow = id - start;
    if (viewRow >= tableWidget_->rowCount()) return;
    const auto& d = rowDisplay_[id];
    if (auto* it = tableWidget_->item(viewRow, 3)) it->setText(d.status);
    if (auto* it = tableWidget_->item(viewRow, 4)) it->setText(d.speedStr);
    if (auto* it = tableWidget_->item(viewRow, 5)) it->setText(d.useTimeStr);
}

void RelayTask::onFirstPage() { gotoPage(0); }
void RelayTask::onPrevPage() { gotoPage(curPage_ - 1); }
void RelayTask::onNextPage() { gotoPage(curPage_ + 1); }
void RelayTask::onLastPage()
{
    int total = (int)fileList_.size();
    int pages = (total <= 0) ? 1 : (total + pageSize_ - 1) / pageSize_;
    gotoPage(pages - 1);
}

void RelayTask::onPageSizeChanged()
{
    QString text = pageSizeCombo_->currentText();
    if (text == "全部") {
        pageSize_ = 1000000000;  // 实际全部
    } else {
        pageSize_ = text.toInt();
        if (pageSize_ <= 0) pageSize_ = 100;
    }
    curPage_ = 0;
    renderPage();
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
