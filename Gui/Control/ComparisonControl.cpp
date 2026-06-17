#include "ComparisonControl.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>

#include "Base/BaseHelper.h"
#include "Base/MessageBoxHelper.h"
#include "ui_ComparisonControl.h"

ComparisonControl::ComparisonControl(QWidget* parent) : QDialog(parent), ui(new Ui::ComparisonControl)
{
    ui->setupUi(this);
    initTableWidget();
    initSignals();
    initControls();

    ui->cbConfig->setEditable(false);
    comparisonSql_ = std::make_shared<ComparisonSql>();
    comparisonSql_->open(GlobalData::getInstance()->getGlobalConfigDir() + "/relayFileDb");
}

ComparisonControl::~ComparisonControl()
{
    comparisonSql_->close();
    delete ui;
}

void ComparisonControl::initControls()
{
    ui->cbConfig->setMinimumWidth(150);
    ui->listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listWidget, &QListWidget::customContextMenuRequested, this, &ComparisonControl::onListContextMenu);
    connect(ui->listWidget, &QListWidget::itemChanged, this, &ComparisonControl::onListItemChanged);
}

void ComparisonControl::onListContextMenu(const QPoint& pos)
{
    QMenu menu(ui->listWidget);
    QAction* actionAll = menu.addAction("全选");
    QAction* actionClear = menu.addAction("取消全选");

    QAction* ret = menu.exec(ui->listWidget->mapToGlobal(pos));
    if (!ret) {
        return;
    }

    autoChange_ = true;
    if (ret == actionAll) {
        for (int i = 0; i < ui->listWidget->count(); ++i) {
            ui->listWidget->item(i)->setCheckState(Qt::Checked);
        }
    } else if (ret == actionClear) {
        for (int i = 0; i < ui->listWidget->count(); ++i) {
            ui->listWidget->item(i)->setCheckState(Qt::Unchecked);
        }
    }
    autoChange_ = false;
    onListItemChanged();
}

void ComparisonControl::onListItemChanged()
{
    if (autoChange_) {
        return;
    }
    QVector<QString> types;
    for (int i = 0; i < ui->listWidget->count(); ++i) {
        if (ui->listWidget->item(i)->checkState() == Qt::Checked) {
            types.append(ui->listWidget->item(i)->text());
        }
    }
    tableWidget_->clearContents();
    tableWidget_->setRowCount(0);

    for (const auto& item : curItems_) {
        if (types.contains(item.mark)) {
            insertRow(item.id, item.name, item.type, item.mark, item.localDir, item.remoteDir);
        }
    }
}

void ComparisonControl::initSignals()
{
    connect(ui->btnSave, &QPushButton::clicked, this, &ComparisonControl::saveConfig);
    connect(ui->btnLoad, &QPushButton::clicked, this, &ComparisonControl::loadConfig);
    connect(ui->btnDel, &QPushButton::clicked, this, &ComparisonControl::delConfig);
    connect(ui->cbConfig, &QComboBox::currentIndexChanged, this, [this]() {
        autoChange_ = true;
        ui->listWidget->clear();
        autoChange_ = false;
        tableWidget_->clearContents();
        tableWidget_->setRowCount(0);
    });
    connect(tableWidget_, &QTableWidget::customContextMenuRequested, this, &ComparisonControl::onTableContextMenu);
    connect(ui->btnNew, &QPushButton::clicked, this, &ComparisonControl::onNewConfig);
}

void ComparisonControl::initTableWidget()
{
    tableWidget_ = new ComDropTable(this);
    headers_ << "ID" << "名称" << "类型" << "标记" << "本地目录" << "远程目录";

    tableWidget_->setColumnCount(headers_.size());
    tableWidget_->setHorizontalHeaderLabels(headers_);
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget_->setContextMenuPolicy(Qt::CustomContextMenu);

    tableWidget_->setColumnWidth(0, 50);
    tableWidget_->setColumnWidth(1, 280);
    tableWidget_->setColumnWidth(2, 50);
    tableWidget_->setColumnWidth(3, 80);

    tableWidget_->viewport()->setAcceptDrops(true);
    tableWidget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableWidget_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    // tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    tableWidget_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    tableWidget_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);

    auto* layout = new QHBoxLayout();
    layout->addWidget(tableWidget_);
    layout->setContentsMargins(0, 0, 0, 0);
    ui->widget->setLayout(layout);
}

void ComparisonControl::saveConfig()
{
    if (!MessageBoxHelper::questionYesNo(this, "确认", "是否保存配置？")) {
        return;
    }
    auto config = ui->cbConfig->currentText().trimmed();
    if (config.isEmpty()) {
        QMessageBox::warning(this, "提示", "配置名称不能为空");
        return;
    }

    bool isSuccess = true;
    if (!comparisonSql_->tableExists(config)) {
        QMessageBox::warning(this, "提示", "配置不存在");
        return;
    }

    comparisonSql_->setTableName(config);
    for (int i = 0; i < tableWidget_->rowCount(); ++i) {
        CompDataItem dataItem;
        auto idText = tableWidget_->item(i, 0)->text();
        dataItem.name = tableWidget_->item(i, 1)->text();
        dataItem.type = tableWidget_->item(i, 2)->text();
        dataItem.mark = tableWidget_->item(i, 3)->text();
        dataItem.localDir = tableWidget_->item(i, 4)->text();
        dataItem.remoteDir = tableWidget_->item(i, 5)->text();

        if (idText.isEmpty()) {
            if (!comparisonSql_->addItem(dataItem)) {
                qCritical() << "添加" << dataItem.name << "到db失败:" << config;
                isSuccess = false;
                break;
            }
            tableWidget_->item(i, 0)->setText(QString::number(dataItem.id));
        } else {
            dataItem.id = idText.toInt();
            if (!comparisonSql_->updateItem(dataItem)) {
                qCritical() << "更新" << dataItem.name << "到db失败:" << config;
                isSuccess = false;
                break;
            }
        }
    }
    if (!delIds_.empty()) {
        for (const auto& id : delIds_) {
            if (!comparisonSql_->deleteItem(id)) {
                qCritical() << "删除id" << id << "失败:" << config;
                isSuccess = false;
                break;
            }
        }
    }
    if (isSuccess) {
        QMessageBox::information(this, "提示", "保存成功");
        if (ui->cbConfig->findText(config) < 0) {
            ui->cbConfig->addItem(config);
        }
        delIds_.clear();
    } else {
        QMessageBox::warning(this, "提示", "保存失败");
    }
}

void ComparisonControl::loadConfig(bool notice)
{
    if (!MessageBoxHelper::questionYesNo(this, "确认", "是否加载配置？")) {
        return;
    }
    delIds_.clear();
    tableWidget_->clearContents();
    tableWidget_->setRowCount(0);
    auto config = ui->cbConfig->currentText().trimmed();
    if (config.isEmpty()) {
        if (notice) {
            QMessageBox::warning(this, "提示", "配置名称不能为空");
        }
        return;
    }
    comparisonSql_->setTableName(config);
    curItems_ = comparisonSql_->getAll();
    for (auto& item : curItems_) {
        insertRow(item.id, item.name, item.type, item.mark, item.localDir, item.remoteDir);
    }
    onRefreshMark();
}

void ComparisonControl::insertRow(int id, const QString& name, const QString& type, const QString& mark, const QString& localDir,
                                  const QString& remoteDir)
{
    auto row = tableWidget_->rowCount();
    tableWidget_->insertRow(row);
    tableWidget_->setItem(row, 0, new QTableWidgetItem(id < 0 ? "" : QString::number(id)));
    tableWidget_->setItem(row, 1, new QTableWidgetItem(name));
    auto* typeItem = new QTableWidgetItem(type);
    typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
    tableWidget_->setItem(row, 2, typeItem);
    tableWidget_->setItem(row, 3, new QTableWidgetItem(mark));
    tableWidget_->setItem(row, 4, new QTableWidgetItem(localDir));
    tableWidget_->setItem(row, 5, new QTableWidgetItem(remoteDir));
}

void ComparisonControl::showEvent(QShowEvent* event)
{
    auto tables = comparisonSql_->tables();
    ui->cbConfig->addItems(tables);
    if (!tables.isEmpty()) {
        ui->cbConfig->setCurrentText(tables.first());
    }
    QDialog::showEvent(event);
}

void ComparisonControl::delConfig()
{
    delIds_.clear();
    auto config = ui->cbConfig->currentText().trimmed();
    if (config.isEmpty()) {
        QMessageBox::warning(this, "提示", "配置名称不能为空");
        return;
    }
    if (!comparisonSql_->tableExists(config)) {
        QMessageBox::warning(this, "提示", "配置不存在");
        return;
    }
    auto ret = QMessageBox::question(this, "提示", QString("确定要删除配置%1吗？").arg(config));
    if (ret != QMessageBox::Yes) {
        return;
    }
    if (!comparisonSql_->dropTable(config)) {
        QMessageBox::warning(this, "提示", "删除失败");
        return;
    }
    ui->cbConfig->removeItem(ui->cbConfig->findText(config));
    QMessageBox::information(this, "提示", "删除成功");
}

void ComparisonControl::onTableContextMenu(const QPoint& pos)
{
    auto datas = tableWidget_->selectedItems();
    if (datas.isEmpty()) {
        return;
    }

    QMenu menu(this);
    QAction* accessDirAction{};
    QAction* accessRemoteDirAction{};
    QAction* openDirAction{};
    QAction* uploadAction = menu.addAction(style()->standardIcon(QStyle::SP_ArrowUp), "上传");
    QAction* newLineAction = menu.addAction("新行");
    QAction* deleteAction = menu.addAction("删除");
    QAction* downloadAction = menu.addAction(style()->standardIcon(QStyle::SP_ArrowDown), "下载");

    // 有的菜单项单行选中时才显示
    if (datas.size() / headers_.size() == 1) {
        accessDirAction = menu.addAction(style()->standardIcon(QStyle::SP_DesktopIcon), "访问本地目录");
        accessRemoteDirAction = menu.addAction(style()->standardIcon(QStyle::SP_DesktopIcon), "访问远程目录");
        openDirAction = menu.addAction("打开本地所在目录");
    }

    auto* selectAction = menu.exec(tableWidget_->viewport()->mapToGlobal(pos));
    if (selectAction == nullptr) {
        return;
    }

    if (selectAction == accessDirAction) {
        emit signalExplorerLocal(datas[4]->text());
        return;
    }
    if (selectAction == accessRemoteDirAction) {
        emit signalExplorerRemote(datas[5]->text());
        return;
    }
    if (selectAction == openDirAction) {
        auto path = datas[4]->text().trimmed();
        if (path.isEmpty()) {
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }
    if (selectAction == newLineAction) {
        insertRow(-1, "", "", "", "", "");
        return;
    }
    if (selectAction == uploadAction) {
        onTrans(datas, true);
        return;
    }
    if (selectAction == deleteAction) {
        if (!MessageBoxHelper::questionYesNo(this, "确认", "是否删除选中行？")) {
            return;
        }
        // 倒着删除
        for (int i = datas.size() / headers_.size() - 1; i >= 0; --i) {
            auto row = datas[i * headers_.size()]->row();
            auto idText = tableWidget_->item(row, 0)->text();
            if (!idText.isEmpty()) {
                delIds_.push_back(idText.toInt());
            }
            tableWidget_->removeRow(row);
        }
        return;
    }
    if (selectAction == downloadAction) {
        onTrans(datas, false);
        return;
    }
}

void ComparisonControl::onTrans(const QList<QTableWidgetItem*>& items, bool isSend)
{
    auto transData = std::make_shared<RelayTaskData>();
    transData->isUpload = isSend;
    for (int i = 0; i < items.size() / headers_.size(); i++) {
        auto curRow = items[i * headers_.size()]->row();
        auto name = tableWidget_->item(curRow, 1)->text();
        auto stdName = name.toStdString();
        auto type = tableWidget_->item(curRow, 2)->text();
        FileItemData itemData;
        itemData.name = name;
        itemData.localRoot = tableWidget_->item(curRow, 4)->text().trimmed();
        itemData.remoteRoot = tableWidget_->item(curRow, 5)->text().trimmed();
        if (itemData.localRoot.isEmpty() || itemData.remoteRoot.isEmpty()) {
            QMessageBox::warning(this, "提示", "本地目录或远程目录不能为空");
            return;
        }
        itemData.sizeStr = "";
        itemData.type = (type == GUI_FILE_TYPE_DIR ? RFileType::mTypeDir : RFileType::mTypeFile);
        itemData.size = 0;
        transData->fileList.push_back(itemData);
    }
    qDebug() << "初始文件个数（含文件夹）:" << transData->fileList.size();
    emit transTaskRun(transData);
}

void ComparisonControl::onNewConfig()
{
    QString newName;
    if (!MessageBoxHelper::getTextInput(this, "重命名", "请输入新配置名", newName)) {
        return;
    }
    if (!comparisonSql_->isNameValid(newName)) {
        QMessageBox::warning(this, "提示", "配置名称不合法，请修改后重试");
        return;
    }

    if (!comparisonSql_->tableExists(newName)) {
        if (!comparisonSql_->createTable(newName)) {
            QMessageBox::warning(this, "提示", "创建表失败");
            return;
        } else {
            QMessageBox::information(this, "提示", "创建表成功");
            ui->cbConfig->addItem(newName);
        }
    } else {
        QMessageBox::warning(this, "提示", "配置已存在");
    }
}

void ComparisonControl::onRefreshMark()
{
    QStringList marks;
    for (const auto& item : curItems_) {
        if (!marks.contains(item.mark)) {
            marks.append(item.mark);
        }
    }

    autoChange_ = true;
    ui->listWidget->clear();
    for (const QString& mark : marks) {
        QListWidgetItem* item = new QListWidgetItem(mark, ui->listWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    autoChange_ = false;
}