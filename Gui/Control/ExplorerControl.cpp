#include "ExplorerControl.h"

#include <File/FileDir.h>
#include <QDateTime>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <Utils/miniUtil.h>

#include "Base/GuiDefine.hpp"
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
    connect(tabWidget_, &QTableWidget::itemDoubleClicked, this, &ExplorerControl::onDoubleClick);
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
    auto item = tabWidget_->currentItem();
    if (!item) {
        return;
    }
    auto row = tabWidget_->row(item);
    auto name = tabWidget_->item(row, 1)->text();
    auto type = tabWidget_->item(row, 3)->text();
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
    tabWidget_->clearContents();
    tabWidget_->setRowCount(0);
    for (int i = 0; i < fileList.size(); ++i) {
        auto row = tabWidget_->rowCount();
        tabWidget_->insertRow(row);
        setFileItem(fileList[i], row);
        if (i != 0 && i % 30 == 0) {
            QGuiApplication::processEvents();
        }
    }
}

void ExplorerControl::onRefresh()
{
}

void ExplorerControl::onUp()
{
}

void ExplorerControl::initControl()
{
    ui->cbPath->setEditable(true);

    tabWidget_ = new QTableWidget(this);
    QStringList headers;
    headers << "" << "文件名称" << "最后修改时间" << "类型" << "大小";

    tabWidget_->setColumnCount(headers.size());
    tabWidget_->setHorizontalHeaderLabels(headers);
    tabWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);

    tabWidget_->setColumnWidth(0, 30);
    tabWidget_->setColumnWidth(1, 300);
    tabWidget_->setColumnWidth(2, 170);
    tabWidget_->setColumnWidth(3, 70);
    tabWidget_->setColumnWidth(4, 90);

    tabWidget_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    tabWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tabWidget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    auto* layout = new QHBoxLayout();
    layout->addWidget(tabWidget_);
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
    tabWidget_->setItem(row, 0, iconItem);

    auto* nameItem = new QTableWidgetItem(QString::fromStdString(meta.name));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    tabWidget_->setItem(row, 1, nameItem);

    QDateTime modifyTime = QDateTime::fromMSecsSinceEpoch(meta.lastModified);
    QString timeStr = modifyTime.toString("yyyy-MM-dd hh:mm:ss");
    auto* timeItem = new QTableWidgetItem(timeStr);
    timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
    tabWidget_->setItem(row, 2, timeItem);

    auto* typeItem = new QTableWidgetItem(typeStr(meta.type));
    typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
    tabWidget_->setItem(row, 3, typeItem);

    if (meta.type == FileType::FILE_TYPE_DIR) {
        auto* sizeItem = new QTableWidgetItem("");
        sizeItem->setFlags(sizeItem->flags() & ~Qt::ItemIsEditable);
        iconItem->setIcon(dirIcon);
        tabWidget_->setItem(row, 4, sizeItem);
    } else {
        auto* sizeItem = new QTableWidgetItem(QString::fromStdString(miniUtil::GetSizeInfo(meta.size)));
        sizeItem->setFlags(sizeItem->flags() & ~Qt::ItemIsEditable);
        iconItem->setIcon(fileIcon);
        tabWidget_->setItem(row, 4, sizeItem);
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