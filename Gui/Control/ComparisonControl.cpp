#include "ComparisonControl.h"

#include <QHBoxLayout>
#include <QHeaderView>

#include "ui_ComparisonControl.h"

ComparisonControl::ComparisonControl(QWidget* parent) : QDialog(parent), ui(new Ui::ComparisonControl)
{
    ui->setupUi(this);
    initTableWidget();
}

ComparisonControl::~ComparisonControl()
{
    delete ui;
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
    tableWidget_->setColumnWidth(1, 230);
    tableWidget_->setColumnWidth(2, 100);
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
