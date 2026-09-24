#include "ExplorerControl.h"

#include <File/FileDir.h>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <Utils/miniUtil.h>

#include <algorithm>

#include "Base/GuiDefine.h"
#include "Base/MenuIcons.h"
#include "Base/MessageBoxHelper.h"
#include "Form/FileMetaInfo.h"
#include "ui_ExplorerControl.h"

#define QUIT_ATOMIC(name)                                                                                                        \
    name.store(true);                                                                                                            \
    emit signalStartWaitForm();                                                                                                  \
    std::vector<std::function<void()>> exitActions;                                                                              \
    std::shared_ptr<void> recv(nullptr, [this, &exitActions](void*) {                                                            \
        emit signalWaitQuit();                                                                                                   \
        for (auto& action : exitActions) {                                                                                       \
            action();                                                                                                            \
        }                                                                                                                        \
        name.store(false);                                                                                                       \
    });

// 后缀选项中代表“无后缀文件”的标签
static const QString kNoExtLabel = QStringLiteral("(无后缀)");

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
    connect(this, &ExplorerControl::signalShouldConfirm, this, &ExplorerControl::onConfirm);
    connect(this, &ExplorerControl::signalStartWaitForm, this, &ExplorerControl::onShowWaitDialog);
    connect(this, &ExplorerControl::signalShowNotice, this, &ExplorerControl::onShowNotice);

    // 筛选控件：名称输入走 200ms 防抖，大目录下避免每次按键都全量重建
    auto* filterTimer = new QTimer(this);
    filterTimer->setSingleShot(true);
    filterTimer->setInterval(200);
    connect(filterTimer, &QTimer::timeout, this, &ExplorerControl::onFilterChanged);
    connect(ui->edSearch, &QLineEdit::textChanged, filterTimer, QOverload<>::of(&QTimer::start));
    // 后缀为点击式多选，无需防抖
    connect(ui->cbExt, &CheckableComboBox::checkedItemsChanged, this,
            [this](const QStringList&) { onFilterChanged(); });
    connect(ui->btnFilter, &QPushButton::toggled, this, &ExplorerControl::onToggleFilter);
    connect(ui->btnResetFilter, &QPushButton::clicked, this, &ExplorerControl::onResetFilter);

    // 筛选行默认隐藏，与按钮初始选中状态同步
    ui->filterWidget->setVisible(ui->btnFilter->isChecked());
}

std::shared_ptr<BaseAskDF> ExplorerControl::getAskDF()
{
    return askDf_;
}

void ExplorerControl::onShowNotice(const QString& msg)
{
    QMessageBox::information(this, "提示", msg);
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

void ExplorerControl::uiPathSet(const QString& path, const std::vector<std::string>& drivers)
{
    ui->cbPath->clear();
    ui->cbPath->addItem(path);
    for (const auto& driver : drivers) {
        ui->cbPath->addItem(QString::fromStdString(driver));
    }
    ui->cbPath->setCurrentText(path);
}

void ExplorerControl::onHome(bool autoEnter)
{
    workerThread_->invoke([this, autoEnter]() {
        std::string home{};
        std::vector<std::string> drivers{};
        if (!askDf_->AskHomeAndDriver(drivers, home)) {
            qWarning() << "获取家目录失败。";
            return;
        }
        uiPathSet(QString::fromStdString(home), drivers);
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
    // 保存源数据，表格按当前筛选/排序条件派生
    fileMetaList_ = fileList;
    currentMetaList_ = fileMetaList_;
    // 当前目录变化：刷新后缀集合选项（会保留仍存在的旧勾选），再重建视图
    updateExtOptions();
    rebuildView();
}

void ExplorerControl::onRefresh()
{
}

void ExplorerControl::onHeaderClicked(int index)
{
    // 第 0 列是图标，不参与排序
    if (index <= 0) {
        return;
    }
    if (index == sortColumn_) {
        sortOrder_ = (sortOrder_ == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
    } else {
        sortColumn_ = index;
        // 名称/类型默认升序，时间/大小默认降序（符合文件管理器习惯）
        sortOrder_ = (index == 2 || index == 4) ? Qt::DescendingOrder : Qt::AscendingOrder;
    }
    rebuildView();
}

// ---------------- 筛选 ----------------

void ExplorerControl::onFilterChanged()
{
    filterName_ = ui->edSearch->text().trimmed();
    filterExts_ = ui->cbExt->checkedItems();
    rebuildView();
}

void ExplorerControl::onToggleFilter(bool checked)
{
    ui->filterWidget->setVisible(checked);
}

void ExplorerControl::onResetFilter()
{
    // edSearch 清空走防抖重建；后缀清空立即触发 checkedItemsChanged → onFilterChanged
    ui->edSearch->clear();
    ui->cbExt->setCheckedItems({});
}

void ExplorerControl::updateExtOptions()
{
    // 收集当前目录下所有文件的后缀集合（去重、小写排序）；无后缀文件用特殊标签表示
    QStringList exts;
    bool hasNoExt = false;
    for (const auto& meta : currentMetaList_) {
        if (meta.type != FileType::FILE_TYPE_FILE) {
            continue;
        }
        const QString ext = QFileInfo(QString::fromStdString(meta.name)).suffix().toLower();
        if (ext.isEmpty()) {
            hasNoExt = true;
        } else if (!exts.contains(ext)) {
            exts.append(ext);
        }
    }
    exts.sort();
    if (hasNoExt) {
        exts.prepend(kNoExtLabel);
    }
    // setOptions 会自动保留同名的旧勾选，跨目录导航时“只看某类文件”得以延续
    ui->cbExt->setOptions(exts);
}

// ---------------- 视图重建（过滤 + 排序） ----------------

void ExplorerControl::rebuildView()
{
    // 1) 过滤，目录与文件分两组（目录始终置顶，与资源管理器一致）
    std::vector<const FileMeta*> dirs;
    std::vector<const FileMeta*> files;
    for (const auto& meta : currentMetaList_) {
        const bool isDir = (meta.type == FileType::FILE_TYPE_DIR);

        // 名称模糊搜索（不区分大小写，文件与文件夹同等匹配）
        if (!filterName_.isEmpty() &&
            !QString::fromStdString(meta.name).contains(filterName_, Qt::CaseInsensitive)) {
            continue;
        }
        // 后缀筛选：选中后缀后结果只保留匹配的文件，目录一律不显示
        if (!filterExts_.isEmpty()) {
            if (isDir) {
                continue;
            }
            const QString rawExt = QFileInfo(QString::fromStdString(meta.name)).suffix().toLower();
            const QString key = rawExt.isEmpty() ? kNoExtLabel : rawExt;
            if (!filterExts_.contains(key)) {
                continue;
            }
        }
        (isDir ? dirs : files).push_back(&meta);
    }

    // 2) 各组内独立排序（保持目录置顶），再按升降序决定组内方向
    const int col = sortColumn_;
    auto cmp = [col](const FileMeta* a, const FileMeta* b) { return lessThanMeta(*a, *b, col); };
    std::stable_sort(dirs.begin(), dirs.end(), cmp);
    std::stable_sort(files.begin(), files.end(), cmp);
    if (sortOrder_ == Qt::DescendingOrder) {
        std::reverse(dirs.begin(), dirs.end());
        std::reverse(files.begin(), files.end());
    }

    // 3) 重建表格
    tableWidget_->setUpdatesEnabled(false);
    tableWidget_->clearContents();
    tableWidget_->setRowCount(0);

    auto appendOne = [this](const FileMeta* meta, int row) {
        tableWidget_->insertRow(row);
        setFileItem(*meta, row);
    };
    int row = 0;
    for (const auto* meta : dirs) {
        appendOne(meta, row++);
        if (row % 50 == 0) {
            QGuiApplication::processEvents();
        }
    }
    for (const auto* meta : files) {
        appendOne(meta, row++);
        if (row % 50 == 0) {
            QGuiApplication::processEvents();
        }
    }

    tableWidget_->horizontalHeader()->blockSignals(true);
    tableWidget_->horizontalHeader()->setSortIndicator(sortColumn_, sortOrder_);
    tableWidget_->horizontalHeader()->blockSignals(false);
    tableWidget_->setUpdatesEnabled(true);
}

bool ExplorerControl::lessThanMeta(const FileMeta& a, const FileMeta& b, int sortColumn)
{
    // 主键比较；并列时一律以名称（不区分大小写）兜底，保证严格弱序
    switch (sortColumn) {
    case 2:   // 最后修改时间（毫秒时间戳）
        if (a.lastModified != b.lastModified) {
            return a.lastModified < b.lastModified;
        }
        break;
    case 3:   // 类型
        if (a.type != b.type) {
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        }
        break;
    case 4:   // 大小（字节）
        if (a.size != b.size) {
            return a.size < b.size;
        }
        break;
    case 1:   // 名称
    default:
        break;
    }
    return QString::fromStdString(a.name).toLower() < QString::fromStdString(b.name).toLower();
}

void ExplorerControl::onUp()
{
    auto path = FileDir::cdUp(currentPath_);
    enterPath(path);
}

void ExplorerControl::initControl()
{
    ui->cbPath->setEditable(true);
    waitDialog_ = newWaitDialog();

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

    // 表头点击排序
    tableWidget_->horizontalHeader()->setSectionsClickable(true);
    tableWidget_->horizontalHeader()->setSortIndicatorShown(true);
    connect(tableWidget_->horizontalHeader(), &QHeaderView::sectionClicked, this, &ExplorerControl::onHeaderClicked);

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

void ExplorerControl::onClear()
{
    tableWidget_->clearContents();
    tableWidget_->setRowCount(0);
    fileMetaList_.clear();
    currentMetaList_.clear();
    currentPath_ = "";
    ui->cbPath->clear();
}

void ExplorerControl::onTableContextMenu(const QPoint& pos)
{
    auto datas = tableWidget_->selectedItems();

    QMenu menu(this);
    QAction* explorerAction{};
    QAction* copyPathAction{};
    QAction* renameAction{};
    QAction* sha256Action{};
    QAction* extractAction{};
    QAction* detailAction{};
    QAction* transAction{};
    QAction* deleteAction{};
    QAction* compressAction{};
    QAction* mkdirDirAction{};

    if (askType_ == AskType::ASK_TYPE_LOCAL && datas.size() <= headers_.size()) {
        explorerAction = new QAction(MenuIcons::explorer(), "在资源管理器中打开");
    }

    // 传输永远在第一项（仅当有选中项时，无选中则无可传输内容）
    if (!datas.isEmpty()) {
        transAction = menu.addAction(MenuIcons::transfer(), "传输");
    }

    // 新建文件夹默认都显示
    mkdirDirAction = menu.addAction(MenuIcons::newFolder(), "新建文件夹");

    if (!datas.isEmpty()) {
        // 超过1组选中，不显示单项菜单。
        if (datas.size() <= headers_.size()) {
            if (auto type = tableWidget_->item(datas[0]->row(), 3); type->text() == GUI_FILE_TYPE_FILE) {
                sha256Action = menu.addAction(MenuIcons::sha256(), "SHA256");
                extractAction = menu.addAction(MenuIcons::extract(), "解压缩");
            }
            copyPathAction = menu.addAction(MenuIcons::copyPath(), "复制全路径");
            renameAction = menu.addAction(MenuIcons::rename(), "重命名");
            detailAction = menu.addAction(MenuIcons::detail(), "详细信息");
        }

        deleteAction = menu.addAction(MenuIcons::del(), "删除");
        compressAction = menu.addAction(MenuIcons::compress(), "压缩");
        if (explorerAction) {
            menu.addAction(explorerAction);
        }
    } else {
        if (explorerAction) {
            menu.addAction(explorerAction);
        }
    }

    auto* selectAction = menu.exec(tableWidget_->viewport()->mapToGlobal(pos));
    if (selectAction == nullptr) {
        return;
    }

    if (selectAction == transAction) {
        actionTrans(datas);
        return;
    }
    if (selectAction == explorerAction) {
        if (datas.size() < 1) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(currentPath_));
            return;
        } else if (auto type = tableWidget_->item(datas[0]->row(), 3); type->text() == GUI_FILE_TYPE_FILE) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(currentPath_));
        } else if (auto type = tableWidget_->item(datas[0]->row(), 3); type->text() == GUI_FILE_TYPE_DIR) {
            auto path = FileDir::Join(currentPath_, datas[1]->text());
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
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
        int row = datas.isEmpty() ? tableWidget_->rowCount() - 1 : datas[1]->row();
        onNewDir(row);
        return;
    }
    if (selectAction == detailAction) {
        onShowFileMetaInfo(datas[1]->row());
        return;
    }
    if (selectAction == compressAction) {
        std::vector<int> rows;
        for (int i = 0; i < datas.size() / headers_.size(); i++) {
            rows.push_back(datas[i * headers_.size()]->row());
        }
        onArchive(rows);
        return;
    }
    if (selectAction == extractAction) {
        onUnArchive(datas[1]->row());
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
    auto* dl = new WaitDialog(this);
    connect(this, &ExplorerControl::signalWaitQuit, dl, &WaitDialog::Quit);
    connect(this, &ExplorerControl::signalWaitQuitMsg, dl, &WaitDialog::QuitWithNotify);
    // dl->setAttribute(Qt::WA_DeleteOnClose);
    return dl;
}

void ExplorerControl::onShowWaitDialog()
{
    if (!isTaskRunning_.load()) {
        return;
    }
    waitDialog_->Reset();
    waitDialog_->exec();
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

    workerThread_->invoke([this, oldPath, newPath, row, newName]() {
        QUIT_ATOMIC(isTaskRunning_);
        auto ret = askDf_->AskRename(oldPath.toStdString(), newPath.toStdString());
        if (ret) {
            QMetaObject::invokeMethod(this, [this, row, newName]() { tableWidget_->item(row, 1)->setText(newName); });
        } else {
            exitActions.emplace_back([this, newName]() { emit signalShowNotice("文件重命名失败"); });
        }
    });
}

void ExplorerControl::onSHA256(int row)
{
    auto fileName = tableWidget_->item(row, 1)->text();
    auto filePath = FileDir::Join(currentPath_, fileName);

    workerThread_->invoke([this, filePath, row]() {
        QUIT_ATOMIC(isTaskRunning_);
        std::string out;
        auto ret = askDf_->AskSha256(filePath.toStdString(), out);
        auto qStr = QString::fromStdString(out);
        if (ret) {
            exitActions.emplace_back([this, filePath, qStr]() {
                emit signalShowNotice(filePath + "\n" + qStr + " ");
                qInfo() << filePath << ",SHA256:" << qStr;
            });
        } else {
            exitActions.emplace_back([this]() { emit signalShowNotice("文件SHA256计算失败"); });
        }
    });
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

    workerThread_->invoke([this, fileList, copyRows]() {
        QUIT_ATOMIC(isTaskRunning_);
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
                // emit signalWaitQuit();
            } else {
                auto notifyMsg =
                    QString::fromStdString(failedFiles[0]) + "等" + QString::number(failedFiles.size()) + "个文件删除失败。";
                exitActions.emplace_back([this, notifyMsg]() { emit signalShowNotice(notifyMsg); });
            }
        } else {
            exitActions.emplace_back([this]() { emit signalShowNotice("文件删除失败"); });
        }
    });
}

void ExplorerControl::onNewDir(int row)
{
    QString newName;
    if (!MessageBoxHelper::getTextInput(this, "新建文件夹", "请输入新文件夹名", newName)) {
        return;
    }

    workerThread_->invoke([this, newName, row]() {
        QUIT_ATOMIC(isTaskRunning_);
        std::string out;
        auto ret = askDf_->AskCreateDir(FileDir::Join(currentPath_, newName).toStdString());
        if (ret) {
            FileMeta meta;
            if (askDf_->AskFileMeta(FileDir::Join(currentPath_, newName).toStdString(), meta)) {
                QMetaObject::invokeMethod(this, [this, row, meta]() {
                    tableWidget_->insertRow(row + 1);
                    setFileItem(meta, row + 1);
                });
            }
        } else {
            exitActions.emplace_back([this, newName]() {
                emit signalShowNotice("新建文件夹" + newName + "失败，请检查文件夹名是否已存在或者是否有权限新建。");
            });
        }
    });
}

void ExplorerControl::onShowFileMetaInfo(int row)
{
    auto fileName = tableWidget_->item(row, 1)->text();
    auto filePath = FileDir::Join(currentPath_, fileName);

    workerThread_->invoke([this, row, filePath]() {
        QUIT_ATOMIC(isTaskRunning_);
        FileMeta meta;
        if (askDf_->AskFileMeta(filePath.toStdString(), meta)) {
            exitActions.emplace_back(
                [this, meta]() { QMetaObject::invokeMethod(this, [this, meta]() { onShowFileMeta(meta); }); });
        } else {
            exitActions.emplace_back([this]() { emit signalShowNotice("文件元数据获取失败"); });
        }
    });
}

void ExplorerControl::onShowFileMeta(const FileMeta& meta)
{
    FileMetaInfo* info = new FileMetaInfo(this);
    info->setAttribute(Qt::WA_DeleteOnClose);
    info->setMeta(meta);
    info->exec();
}

void ExplorerControl::onArchive(const std::vector<int>& rows)
{
    if (rows.empty()) {
        return;
    }
    if (!MessageBoxHelper::questionYesNo(this, "确认", "是否压缩选中行？")) {
        return;
    }
    QString archiveName;
    if (!MessageBoxHelper::getTextInput(this, "压缩文件", "请输入压缩文件名(自动后缀zip)", archiveName)) {
        return;
    }
    archiveName += ".zip";
    std::vector<FileMeta> metaList;
    int maxRow = *std::max_element(rows.begin(), rows.end());
    for (auto row : rows) {
        auto fileName = tableWidget_->item(row, 1)->text();
        auto type = tableWidget_->item(row, 3)->text();
        FileMeta meta;
        meta.type = (type == GUI_FILE_TYPE_DIR ? FileType::FILE_TYPE_DIR : FileType::FILE_TYPE_FILE);
        meta.fullPath = FileDir::Join(currentPath_, fileName).toStdString();
        metaList.push_back(meta);
    }

    workerThread_->invoke([this, metaList, archiveName, maxRow]() {
        QUIT_ATOMIC(isTaskRunning_);
        auto qArchivePath = FileDir::Join(currentPath_, archiveName);
        FileMeta checkMeta;
        if (!askDf_->AskFileMeta(qArchivePath.toStdString(), checkMeta)) {
            exitActions.emplace_back([this]() { emit signalShowNotice("检测压缩文件失败。"); });
            return;
        }
        if (checkMeta.exist == 1) {
            emit signalShouldConfirm("警告", "压缩文件已存在，是否继续？");
            isConfirmRun_ = true;
            QMutexLocker locker(&askMut_);
            while (isConfirmRun_) {
                confirmCond_.wait(&askMut_);
            }
            if (!confirmResult_) {
                return;
            }
        }
        auto archivePath = qArchivePath.toStdString();
        qInfo() << "压缩文件:" << qArchivePath;
        if (askDf_->AskArchive(metaList, archivePath)) {
            FileMeta meta;
            if (askDf_->AskFileMeta(archivePath, meta)) {
                QMetaObject::invokeMethod(this, [this, maxRow, meta]() {
                    tableWidget_->insertRow(maxRow + 1);
                    setFileItem(meta, maxRow + 1);
                });
            }
            exitActions.emplace_back([this]() { emit signalShowNotice("文件压缩成功"); });
        } else {
            exitActions.emplace_back([this]() { emit signalShowNotice("文件压缩失败"); });
        }
    });
}

void ExplorerControl::onUnArchive(int row)
{
    if (!MessageBoxHelper::questionYesNo(this, "确认", "是否解压选中行？")) {
        return;
    }

    auto sourceName = tableWidget_->item(row, 1)->text();
    QString autoOutDir;
    FileDir::GetFileNameNoExt(sourceName, autoOutDir);

    QString unarchiveName;
    if (!MessageBoxHelper::getTextInput(this, "解压文件", "请输入解压目录", unarchiveName, autoOutDir)) {
        return;
    }
    auto outDir = FileDir::Join(currentPath_, unarchiveName);

    workerThread_->invoke([this, outDir, row]() {
        QUIT_ATOMIC(isTaskRunning_);
        FileMeta outDirMeta;
        if (!askDf_->AskFileMeta(outDir.toStdString(), outDirMeta)) {
            exitActions.emplace_back([this]() { emit signalShowNotice("检测解压目录失败。"); });
            return;
        }
        bool isCreateDir = false;
        if (outDirMeta.exist == 0) {
            if (!askDf_->AskCreateDir(outDir.toStdString())) {
                exitActions.emplace_back([this]() { emit signalShowNotice("创建解压目录失败。"); });
                return;
            }
            isCreateDir = true;
        } else {
            // if (!MessageBoxHelper::questionYesNo(this, "确认", "解压目录已存在，是否继续？")) {
            //     return;
            // }
            emit signalShouldConfirm("确认", "解压目录已存在，是否继续？");
            isConfirmRun_ = true;
            QMutexLocker locker(&askMut_);
            while (isConfirmRun_) {
                confirmCond_.wait(&askMut_);
            }
            if (!confirmResult_) {
                return;
            }
        }
        auto fileName = tableWidget_->item(row, 1)->text();
        auto filePath = FileDir::Join(currentPath_, fileName);
        if (askDf_->AskUnArchive(filePath.toStdString(), outDir.toStdString())) {
            if (isCreateDir) {
                FileMeta newDirMeta;
                if (askDf_->AskFileMeta(outDir.toStdString(), newDirMeta)) {
                    exitActions.emplace_back([this, row, newDirMeta]() {
                        QMetaObject::invokeMethod(this, [this, row, newDirMeta]() {
                            tableWidget_->insertRow(row + 1);
                            setFileItem(newDirMeta, row + 1);
                        });
                    });
                }
            }
        } else {
            exitActions.emplace_back([this]() { emit signalShowNotice("文件解压失败"); });
        }
    });
}

void ExplorerControl::onConfirm(const QString& title, const QString& text)
{
    confirmResult_ = MessageBoxHelper::questionYesNo(this, title, text);
    isConfirmRun_ = false;
    confirmCond_.wakeOne();
}
