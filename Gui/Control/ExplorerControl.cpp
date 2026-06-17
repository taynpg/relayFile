#include "ExplorerControl.h"

#include <File/FileDir.h>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QHeaderView>
#include <QMenu>
#include <QTableWidgetItem>
#include <QUrl>
#include <Utils/miniUtil.h>

#include "Base/GuiDefine.hpp"
#include "Base/MessageBoxHelper.h"
#include "Form/FileMetaInfo.h"
#include "ui_ExplorerControl.h"

ExplorerControl::ExplorerControl(QWidget* parent) : QDialog(parent), ui(new Ui::ExplorerControl)
{
    ui->setupUi(this);
    initControl();
    baseTask();
    initSignals();
}

ExplorerControl::~ExplorerControl()
{
    delete ui;
}

void ExplorerControl::baseTask()
{
    workerThread_ = std::make_shared<WorkerThread<ExplorerControl>>(this);
    workerThread_->start();
}

void ExplorerControl::Quit()
{
    workerThread_->stop();
    workerThread_->quit();
}

void ExplorerControl::initSignals()
{
    connect(this, &ExplorerControl::fileListChanged, this, &ExplorerControl::onFileListChanged);
    connect(ui->btnEnter, &QPushButton::clicked, this, &ExplorerControl::onEnter);
    connect(ui->btnHome, &QPushButton::clicked, this, [this]() { onHome(false); });
    connect(ui->btnUp, &QPushButton::clicked, this, &ExplorerControl::onUp);
    connect(tableWidget_, &QTableWidget::itemDoubleClicked, this, &ExplorerControl::onDoubleClick);
    connect(tableWidget_, &QTableWidget::customContextMenuRequested, this, &ExplorerControl::onTableContextMenu);
}

std::shared_ptr<BaseAskDF> ExplorerControl::getAskDF()
{
    return askDf_;
}

void ExplorerControl::setAskDF(AskType askType)
{
    askType_ = askType;
    askDf_ = BaseAskDF::Create(askType_);
}

QString ExplorerControl::getCurrentPath()
{
    QMutexLocker locker(&curPathMut_);
    return currentPath_;
}

void ExplorerControl::setCurrentPath(const QString& path)
{
    QMutexLocker locker(&curPathMut_);
    currentPath_ = path;
}

void ExplorerControl::onEnter()
{
    auto path = ui->cbPath->currentText().trimmed();
    if (path.isEmpty()) {
        return;
    }
    enterPath(path);
}

void ExplorerControl::onEnterPath(const QString& path)
{
    ui->cbPath->setCurrentText(path);
    enterPath(path);
}

void ExplorerControl::enterPath(const QString& path)
{
    workerThread_->invoke([this, path]() {
        std::vector<FileMeta> fileList{};
        if (!askDf_->AskFileList(path.toStdString(), fileList)) {
            emit fileListChanged(false, fileList);
            return;
        }
        QMetaObject::invokeMethod(this, [this, path]() {
            setCurrentPath(path);
            uiPathSet(path);
        });
        emit fileListChanged(true, fileList);
    });
}

void ExplorerControl::onDoubleClick()
{
    auto item = tableWidget_->currentItem();
    if (!item) {
        return;
    }
    auto row = tableWidget_->row(item);
    auto name = tableWidget_->item(row, 1)->text();
    auto type = tableWidget_->item(row, 3)->text();
    if (type != GUI_FILE_TYPE_DIR) {
        qWarning() << name << "不是目录。";
        return;
    }
    qInfo() << "访问目录:" << name;
    auto path = FileDir::Join(currentPath_, name);
    enterPath(path);
}

void ExplorerControl::uiPathSet(const QString& path)
{
    ui->cbPath->setCurrentText(path);
}

void ExplorerControl::onHome(bool autoEnter)
{
    workerThread_->invoke([this, autoEnter]() {
        std::string home{};
        if (!askDf_->AskHome(home)) {
            qWarning() << "获取家目录失败。";
            return;
        }
        uiPathSet(QString::fromStdString(home));
        if (autoEnter) {
            onEnter();
        }
    });
}

void ExplorerControl::onFileListChanged(bool isSuccess, const std::vector<FileMeta>& fileList)
{
    if (!isSuccess) {
        return;
    }
    tableWidget_->clearContents();
    tableWidget_->setRowCount(0);
    for (int i = 0; i < fileList.size(); ++i) {
        auto row = tableWidget_->rowCount();
        tableWidget_->insertRow(row);
        setFileItem(fileList[i], row);
        if (i != 0 && i % 30 == 0) {
            QGuiApplication::processEvents();
        }
    }
    fileMetaList_ = fileList;
    currentMetaList_ = fileMetaList_;
}

void ExplorerControl::onRefresh()
{
}

void ExplorerControl::onUp()
{
    auto path = FileDir::cdUp(currentPath_);
    enterPath(path);
}

void ExplorerControl::initControl()
{
    ui->cbPath->setEditable(true);

    tableWidget_ = new ExpDropTable(this);
    headers_ << "" << "文件名称" << "最后修改时间" << "类型" << "大小";

    tableWidget_->setColumnCount(headers_.size());
    tableWidget_->setHorizontalHeaderLabels(headers_);
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget_->setContextMenuPolicy(Qt::CustomContextMenu);

    tableWidget_->setColumnWidth(0, 30);
    tableWidget_->setColumnWidth(1, 300);
    tableWidget_->setColumnWidth(2, 170);
    tableWidget_->setColumnWidth(3, 70);
    tableWidget_->setColumnWidth(4, 90);

    auto* layout = new QHBoxLayout();
    tableWidget_->setDragEnabled(true);
    tableWidget_->setAcceptDrops(true);
    tableWidget_->setDropIndicatorShown(true);
    tableWidget_->setDragDropMode(QAbstractItemView::DragDrop);
    tableWidget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableWidget_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    tableWidget_->setGetOwnRoot([this]() { return currentPath_; });

    layout->addWidget(tableWidget_);
    layout->setContentsMargins(0, 0, 0, 0);
    ui->widget->setLayout(layout);
}

void ExplorerControl::setFileItem(const FileMeta& meta, int row)
{
    static QIcon dirIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    static QIcon fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    auto* iconItem = new QTableWidgetItem("");
    iconItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    iconItem->setFlags(iconItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 0, iconItem);

    auto* nameItem = new QTableWidgetItem(QString::fromStdString(meta.name));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 1, nameItem);

    QDateTime modifyTime = QDateTime::fromMSecsSinceEpoch(meta.lastModified);
    QString timeStr = modifyTime.toString("yyyy-MM-dd hh:mm:ss");
    auto* timeItem = new QTableWidgetItem(timeStr);
    timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 2, timeItem);

    auto* typeItem = new QTableWidgetItem(typeStr(meta.type));
    typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 3, typeItem);

    if (meta.type == FileType::FILE_TYPE_DIR) {
        auto* sizeItem = new QTableWidgetItem("");
        sizeItem->setFlags(sizeItem->flags() & ~Qt::ItemIsEditable);
        iconItem->setIcon(dirIcon);
        tableWidget_->setItem(row, 4, sizeItem);
    } else {
        auto* sizeItem = new QTableWidgetItem(QString::fromStdString(miniUtil::GetSizeInfo(meta.size)));
        sizeItem->setFlags(sizeItem->flags() & ~Qt::ItemIsEditable);
        iconItem->setIcon(fileIcon);
        tableWidget_->setItem(row, 4, sizeItem);
    }
}

QString ExplorerControl::typeStr(FileType type)
{
    switch (type) {
    case FileType::FILE_TYPE_DIR:
        return GUI_FILE_TYPE_DIR;
    case FileType::FILE_TYPE_FILE:
        return GUI_FILE_TYPE_FILE;
    default:
        return GUI_FILE_TYPE_UNKNOWN;
    }
}

void ExplorerControl::onTableContextMenu(const QPoint& pos)
{
    auto datas = tableWidget_->selectedItems();
    if (datas.isEmpty()) {
        return;
    }

    QMenu menu(this);
    QAction* transAction = menu.addAction(style()->standardIcon(QStyle::SP_FileDialogStart), "传输");

    QAction* explorerAction{};
    QAction* copyPathAction{};
    QAction* renameAction{};
    QAction* sha256Action{};
    QAction* extractAction{};
    QAction* detailAction{};

    // 超过1组选中，不显示单项菜单。
    if (datas.size() <= headers_.size()) {
        if (auto type = tableWidget_->item(datas[0]->row(), 3);
            type->text() == GUI_FILE_TYPE_DIR && askType_ == AskType::ASK_TYPE_LOCAL) {
            explorerAction = menu.addAction(style()->standardIcon(QStyle::SP_DesktopIcon), "在资源管理器中打开");
            menu.addAction(explorerAction);
        }
        if (auto type = tableWidget_->item(datas[0]->row(), 3); type->text() == GUI_FILE_TYPE_FILE) {
            sha256Action = menu.addAction("SHA256");
            extractAction = menu.addAction("解压缩");
        }
        copyPathAction = menu.addAction("复制全路径");
        renameAction = menu.addAction("重命名");
        detailAction = menu.addAction("详细信息");
    }

    QAction* deleteAction = menu.addAction(style()->standardIcon(QStyle::SP_DesktopIcon), "删除");
    QAction* compressAction = menu.addAction("压缩");
    QAction* mkdirDirAction = menu.addAction("新建文件夹");

    auto* selectAction = menu.exec(tableWidget_->viewport()->mapToGlobal(pos));
    if (selectAction == nullptr) {
        return;
    }

    if (selectAction == transAction) {
        actionTrans(datas);
        return;
    }
    if (selectAction == explorerAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(currentPath_));
        return;
    }
    if (selectAction == copyPathAction) {
        QString path = currentPath_;
        path = FileDir::Join(currentPath_, datas[1]->text());
        QApplication::clipboard()->setText(path);
        return;
    }
    if (selectAction == renameAction) {
        onRename(datas[1]->row());
        return;
    }
    if (selectAction == sha256Action) {
        onSHA256(datas[1]->row());
        return;
    }
    if (selectAction == deleteAction) {
        std::vector<int> rows;
        for (int i = 0; i < datas.size() / headers_.size(); i++) {
            rows.push_back(datas[i * headers_.size()]->row());
        }
        onDelete(rows);
        return;
    }
    if (selectAction == mkdirDirAction) {
        onNewDir(datas[1]->row());
        return;
    }
    if (selectAction == detailAction) {
        onShowFileMetaInfo(datas[1]->row());
        return;
    }
}

void ExplorerControl::actionTrans(const QList<QTableWidgetItem*>& datas)
{
    ExplorerSharedData es;
    if (tellInfoCall_) {
        tellInfoCall_(es);
    }

    auto transData = std::make_shared<RelayTaskData>();
    auto localRoot = (askType_ == AskType::ASK_TYPE_LOCAL ? currentPath_ : es.currentPath_);
    auto remoteRoot = (askType_ == AskType::ASK_TYPE_LOCAL ? es.currentPath_ : currentPath_);
    transData->isUpload = (askType_ == AskType::ASK_TYPE_LOCAL);
    for (int i = 0; i < datas.size() / headers_.size(); i++) {
        auto curRow = datas[i * headers_.size()]->row();
        auto name = tableWidget_->item(curRow, 1)->text();
        auto stdName = name.toStdString();
        auto type = tableWidget_->item(curRow, 3)->text();
        auto sizeStr = tableWidget_->item(curRow, 4)->text();
        FileItemData itemData;
        itemData.name = name;
        itemData.localRoot = localRoot;
        itemData.remoteRoot = remoteRoot;
        itemData.sizeStr = sizeStr;
        itemData.type = (type == GUI_FILE_TYPE_DIR ? RFileType::mTypeDir : RFileType::mTypeFile);
        auto it = std::find_if(currentMetaList_.begin(), currentMetaList_.end(),
                               [stdName](const FileMeta& meta) { return meta.name == stdName; });
        if (it != currentMetaList_.end()) {
            itemData.size = it->size;
        }
        transData->fileList.push_back(itemData);
    }
    qDebug() << "初始文件个数（含文件夹）:" << transData->fileList.size();
    emit transTaskRun(transData);
}

void ExplorerControl::setTellInfoCall(std::function<void(ExplorerSharedData& es)> call)
{
    tellInfoCall_ = call;
}

void ExplorerControl::tellInfo(ExplorerSharedData& es)
{
    es.currentPath_ = currentPath_;
}

WaitDialog* ExplorerControl::newWaitDialog()
{
    WaitDialog* waitDialog = new WaitDialog(this);
    connect(this, &ExplorerControl::signalWaitQuit, waitDialog, &WaitDialog::Quit);
    connect(this, &ExplorerControl::signalWaitQuitMsg, waitDialog, &WaitDialog::QuitWithNotify);
    waitDialog->setAttribute(Qt::WA_DeleteOnClose);
    return waitDialog;
}

void ExplorerControl::onRename(int row)
{
    auto oldName = tableWidget_->item(row, 1)->text();
    QString newName;
    if (!MessageBoxHelper::getTextInput(this, "重命名", "请输入新文件名", newName, oldName)) {
        return;
    }
    auto oldPath = FileDir::Join(currentPath_, oldName);
    auto newPath = FileDir::Join(currentPath_, newName);

    WaitDialog* waitDialog = newWaitDialog();
    workerThread_->invoke([this, oldPath, newPath, waitDialog, row, newName]() {
        auto ret = askDf_->AskRename(oldPath.toStdString(), newPath.toStdString());
        if (ret) {
            emit signalWaitQuit();
            QMetaObject::invokeMethod(this, [this, row, newName]() { tableWidget_->item(row, 1)->setText(newName); });
        } else {
            emit signalWaitQuitMsg("文件重命名失败");
        }
    });
    waitDialog->exec();
}

void ExplorerControl::onSHA256(int row)
{
    auto fileName = tableWidget_->item(row, 1)->text();
    auto filePath = FileDir::Join(currentPath_, fileName);

    WaitDialog* waitDialog = newWaitDialog();
    workerThread_->invoke([this, filePath, waitDialog, row]() {
        std::string out;
        auto ret = askDf_->AskSha256(filePath.toStdString(), out);
        auto qStr = QString::fromStdString(out);
        if (ret) {
            emit signalWaitQuitMsg(filePath + "\n" + qStr + " ");
            qInfo() << filePath << ",SHA256:" << qStr;
        } else {
            emit signalWaitQuitMsg("文件SHA256计算失败");
        }
    });
    waitDialog->exec();
}

void ExplorerControl::onDelete(const std::vector<int>& rows)
{
    if (!MessageBoxHelper::questionYesNo(this, "确认", "是否删除选中行？")) {
        return;
    }
    std::vector<int> copyRows = rows;
    // 排序，确保从后往前删除，避免索引变化
    std::sort(copyRows.begin(), copyRows.end(), std::greater<int>());

    std::vector<std::string> fileList;
    for (auto row : copyRows) {
        fileList.push_back(FileDir::Join(currentPath_, tableWidget_->item(row, 1)->text()).toStdString());
    }
    WaitDialog* waitDialog = newWaitDialog();
    workerThread_->invoke([this, fileList, waitDialog, copyRows]() {
        std::vector<std::string> failedFiles;
        auto ret = askDf_->AskDelete(fileList, failedFiles);
        if (ret) {
            // 更新删除成功的部分
            std::vector<QString> failedNames;
            failedNames.reserve(failedFiles.size());
            std::transform(failedFiles.begin(), failedFiles.end(), std::back_inserter(failedNames), [](const std::string& s) {
                auto qp = QString::fromStdString(s);
                return FileDir::GenFileName(qp);
            });

            for (auto row : copyRows) {
                auto curName = tableWidget_->item(row, 1)->text();
                if (std::find(failedNames.begin(), failedNames.end(), curName) == failedNames.end()) {
                    tableWidget_->removeRow(row);
                    qInfo() << "删除成功:" << FileDir::Join(currentPath_, curName);
                }
            }

            if (failedFiles.empty()) {
                emit signalWaitQuit();
            } else {
                auto notifyMsg =
                    QString::fromStdString(failedFiles[0]) + "等" + QString::number(failedFiles.size()) + "个文件删除失败。";
                emit signalWaitQuitMsg(notifyMsg);
            }
        } else {
            emit signalWaitQuitMsg("文件删除失败");
        }
    });
    waitDialog->exec();
}

void ExplorerControl::onNewDir(int row)
{
    QString newName;
    if (!MessageBoxHelper::getTextInput(this, "新建文件夹", "请输入新文件夹名", newName)) {
        return;
    }
    WaitDialog* waitDialog = newWaitDialog();
    workerThread_->invoke([this, newName, waitDialog, row]() {
        std::string out;
        auto ret = askDf_->AskCreateDir(FileDir::Join(currentPath_, newName).toStdString());
        if (ret) {
            emit signalWaitQuit();
            QMetaObject::invokeMethod(this, [this, row, newName]() {
                FileMeta meta;
                if (askDf_->AskFileMeta(FileDir::Join(currentPath_, newName).toStdString(), meta)) {
                    tableWidget_->insertRow(row + 1);
                    setFileItem(meta, row + 1);
                }
            });
        } else {
            emit signalWaitQuitMsg("新建文件夹" + newName + "失败，请检查文件夹名是否已存在或者是否有权限新建。");
        }
    });
    waitDialog->exec();
}

void ExplorerControl::onShowFileMetaInfo(int row)
{
    auto fileName = tableWidget_->item(row, 1)->text();
    auto filePath = FileDir::Join(currentPath_, fileName);
    WaitDialog* waitDialog = newWaitDialog();
    workerThread_->invoke([this, waitDialog, row, filePath]() {
        FileMeta meta;
        if (askDf_->AskFileMeta(filePath.toStdString(), meta)) {
            emit signalWaitQuit();
            QMetaObject::invokeMethod(this, [this, meta]() { onShowFileMeta(meta); });
        } else {
            emit signalWaitQuitMsg("文件元数据获取失败");
        }
    });
    waitDialog->exec();
}

void ExplorerControl::onShowFileMeta(const FileMeta& meta)
{
    FileMetaInfo* info = new FileMetaInfo(this);
    info->setAttribute(Qt::WA_DeleteOnClose);
    info->setMeta(meta);
    info->exec();
}