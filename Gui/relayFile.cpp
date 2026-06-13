#include "relayFile.h"

#include <QCloseEvent>
#include <QScreen>
#include <QSplitter>
#include <QVBoxLayout>
#include <Utils/Logger.h>

#include "./ui_relayFile.h"
#include "Base/BaseHelper.h"

static LogControl* gLogControl{};
static bool isQuit{false};

relayFile::relayFile(QWidget* parent) : QWidget(parent), ui(new Ui::relayFile)
{
    ui->setupUi(this);

    auto controlSession = std::make_shared<ControlSession>();
    auto fileSession = std::make_shared<FileSession>();
    GlobalData::getInstance()->setControlSession(controlSession);
    GlobalData::getInstance()->setFileSession(fileSession);

    doubleLinker_ = std::make_shared<DoubleLinker>();
    doubleLinker_->SetControlSession(controlSession);
    doubleLinker_->SetFileSession(fileSession);
    GlobalData::getInstance()->setDoubleLinker(doubleLinker_);
    GlobalData::getInstance()->setBaseConfig(controlSession->getBaseConfig());

    Logger logger;
    logger.setInfo("log/relayFileGUI.log", "relayFileGUI");
    logger.initSimpleLogger(false);

    initControls();
    initLayout();

    gLogControl = logControl_;
    isQuit = false;

    qInstallMessageHandler(ControlMsgHander);
    qInfo() << "启动。";
}

relayFile::~relayFile()
{
    connectorControl_->Quit();
    delete ui;
}

void relayFile::Quit()
{
    doubleLinker_->Quit();
    localExplorerControl_->Quit();
    remoteExplorerControl_->Quit();
    isQuit = true;
}

void relayFile::closeEvent(QCloseEvent* event)
{
    Quit();
    event->accept();
}

void relayFile::initControls()
{
    connectorControl_ = new ConnectorControl(this);
    localExplorerControl_ = new ExplorerControl(this);
    remoteExplorerControl_ = new ExplorerControl(this);

    localExplorerControl_->setAskDF(AskType::ASK_TYPE_LOCAL);
    remoteExplorerControl_->setAskDF(AskType::ASK_TYPE_REMOTE);

    GlobalData::getInstance()->setAskDfLocal(localExplorerControl_->getAskDF());
    GlobalData::getInstance()->setAskDfRemote(remoteExplorerControl_->getAskDF());

    comparisonControl_ = new ComparisonControl(this);
    logControl_ = new LogControl(this);
    tabWidget_ = new QTabWidget(this);

    localExplorerControl_->onHome();

    // connect(connectorControl_, &ConnectorControl::signalConnectDone, remoteExplorerControl_,
    //         [this]() { remoteExplorerControl_->onHome(true); });
    connect(connectorControl_, &ConnectorControl::signalConfirmOther, remoteExplorerControl_,
            [this]() { remoteExplorerControl_->onHome(true); });
    localExplorerControl_->setTellInfoCall([this](ExplorerSharedData& es) { remoteExplorerControl_->tellInfo(es); });
    remoteExplorerControl_->setTellInfoCall([this](ExplorerSharedData& es) { localExplorerControl_->tellInfo(es); });
}

void relayFile::ControlMsgHander(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (isQuit) {
        return;
    }
    switch (type) {
    case QtDebugMsg:
        QMetaObject::invokeMethod(gLogControl, [msg]() { gLogControl->Debug(msg); });
        break;
    case QtInfoMsg:
        QMetaObject::invokeMethod(gLogControl, [msg]() { gLogControl->Info(msg); });
        break;
    case QtWarningMsg:
        QMetaObject::invokeMethod(gLogControl, [msg]() { gLogControl->Warn(msg); });
        break;
    case QtCriticalMsg:
        QMetaObject::invokeMethod(gLogControl, [msg]() { gLogControl->Error(msg); });
        break;
    default:
        break;
    }
}

void relayFile::initLayout()
{
    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->setHandleWidth(0);

    auto* sTop = new QSplitter(Qt::Horizontal);
    auto* sConnect = new QSplitter(Qt::Vertical);
    auto* sFile = new QSplitter(Qt::Horizontal);

    sTop->setHandleWidth(0);
    sConnect->setHandleWidth(0);
    sFile->setHandleWidth(0);

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
