#include "relayFile.h"

#include <QScreen>
#include <QSplitter>
#include <QVBoxLayout>

#include "./ui_relayFile.h"

relayFile::relayFile(QWidget* parent) : QWidget(parent), ui(new Ui::relayFile)
{
    ui->setupUi(this);
    initControls();
    initLayout();
}

relayFile::~relayFile()
{
    delete ui;
}

void relayFile::initControls()
{
    connectorControl_ = new ConnectorControl(this);
    localExplorerControl_ = new ExplorerControl(this);
    remoteExplorerControl_ = new ExplorerControl(this);
    comparisonControl_ = new ComparisonControl(this);
    relayTask_ = new RelayTask(this);
    logControl_ = new LogControl(this);
    tabWidget_ = new QTabWidget(this);
}

void relayFile::initLayout()
{
    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->setHandleWidth(1);

    auto* sTop = new QSplitter(Qt::Horizontal);
    auto* sConnect = new QSplitter(Qt::Vertical);
    auto* sFile = new QSplitter(Qt::Horizontal);

    sTop->setHandleWidth(1);
    sConnect->setHandleWidth(1);
    sFile->setHandleWidth(1);

    sTop->addWidget(connectorControl_);
    sTop->addWidget(tabWidget_);

    tabWidget_->addTab(logControl_, "日志");
    tabWidget_->addTab(comparisonControl_, "文件对照");

    sFile->addWidget(localExplorerControl_);
    sFile->addWidget(remoteExplorerControl_);

    splitter->addWidget(sTop);
    splitter->addWidget(sFile);

    // 暂且这样初始化尺寸
    QList<int> sizes;
    sizes << height() * 2 / 5 << height() * 3 / 5;
    splitter->setSizes(sizes);

    QVBoxLayout* layout = new QVBoxLayout();
    layout->addWidget(splitter);
    setLayout(layout);
}