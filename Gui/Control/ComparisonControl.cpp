#include "ComparisonControl.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>

#include "Base/BaseHelper.h"
#include "ui_ComparisonControl.h"

ComparisonControl::ComparisonControl(QWidget* parent) : QDialog(parent), ui(new Ui::ComparisonControl)
{
    ui->setupUi(this);
    initTableWidget();
    initSignals();
    initControls();
    ui->cbConfig->setEditable(true);
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
}

void ComparisonControl::initSignals()
{
    connect(ui->btnSave, &QPushButton::clicked, this, &ComparisonControl::saveConfig);
    connect(ui->btnLoad, &QPushButton::clicked, this, &ComparisonControl::loadConfig);
    connect(ui->btnDel, &QPushButton::clicked, this, &ComparisonControl::delConfig);
    connect(tableWidget_, &QTableWidget::customContextMenuRequested, this, &ComparisonControl::onTableContextMenu);
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
    auto config = ui->cbConfig->currentText().trimmed();
    if (config.isEmpty()) {
        QMessageBox::warning(this, "提示", "配置名称不能为空");
        return;
    }

    if (!comparisonSql_->isNameValid(config)) {
        QMessageBox::warning(this, "提示", "配置名称不合法，请修改后重试");
        return;
    }

    bool isSuccess = true;
    if (!comparisonSql_->tableExists(config)) {
        if (!comparisonSql_->createTable(config)) {
            qCritical() << "创建表失败:" << config;
            isSuccess = false;
            return;
        }
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
    if (isSuccess) {
        QMessageBox::information(this, "提示", "保存成功");
        auto tables = comparisonSql_->tables();
        ui->cbConfig->clear();
        ui->cbConfig->addItems(tables);
        if (ui->cbConfig->findText(config) >= 0) {
            ui->cbConfig->setCurrentText(config);
        }
    } else {
        QMessageBox::warning(this, "提示", "保存失败");
    }
}

void ComparisonControl::loadConfig(bool notice)
{
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
    auto items = comparisonSql_->getAll();
    for (auto& item : items) {
        auto row = tableWidget_->rowCount();
        tableWidget_->insertRow(row);
        auto* itemId = new QTableWidgetItem(QString::number(item.id));
        itemId->setFlags(itemId->flags() & ~Qt::ItemIsEditable);
        tableWidget_->setItem(row, 0, itemId);
        tableWidget_->setItem(row, 1, new QTableWidgetItem(item.name));
        auto* typeItem = new QTableWidgetItem(item.type);
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        tableWidget_->setItem(row, 2, typeItem);
        tableWidget_->setItem(row, 3, new QTableWidgetItem(item.mark));
        tableWidget_->setItem(row, 4, new QTableWidgetItem(item.localDir));
        tableWidget_->setItem(row, 5, new QTableWidgetItem(item.remoteDir));
    }
}

void ComparisonControl::showEvent(QShowEvent* event)
{
    auto tables = comparisonSql_->tables();
    ui->cbConfig->addItems(tables);
    if (!tables.isEmpty()) {
        ui->cbConfig->setCurrentText(tables.first());
    }
    loadConfig(false);
    QDialog::showEvent(event);
}

void ComparisonControl::delConfig()
{
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
    QAction* accessDirAction = menu.addAction("访问本地目录");
    QAction* accessRemoteDirAction = menu.addAction("访问远程目录");
    QAction* openDirAction = menu.addAction("打开所在目录");
    QAction* uploadAction = menu.addAction(style()->standardIcon(QStyle::SP_ArrowUp), "上传");
    QAction* newLineAction = menu.addAction("新行");
    QAction* deleteAction = menu.addAction("删除");
    QAction* downloadAction = menu.addAction(style()->standardIcon(QStyle::SP_ArrowDown), "下载");
    auto* selectAction = menu.exec(tableWidget_->viewport()->mapToGlobal(pos));

    if (selectAction == accessDirAction) {
        return;
    }
    if (selectAction == accessRemoteDirAction) {
        return;
    }
    if (selectAction == openDirAction) {
        return;
    }
    if (selectAction == newLineAction) {
        return;
    }
    if (selectAction == uploadAction) {
        return;
    }
    if (selectAction == deleteAction) {
        return;
    }
    if (selectAction == downloadAction) {
        return;
    }
}
