#include "relayFile.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QMessageBox>
#include <QScreen>
#include <QSplitter>
#include <QVBoxLayout>
#include <Utils/Logger.h>
#include <relayFileVersion.h>

#include "./ui_relayFile.h"
#include "Base/BaseHelper.h"

static LogControl* gLogControl{};
static bool isQuit{false};

relayFile::relayFile(QWidget* parent) : QWidget(parent), ui(new Ui::relayFile)
{
    ui->setupUi(this);

    auto controlSession = std::make_shared<ControlSession>();
    auto fileSession = std::make_shared<FileSession>();
    baseConfig_ = std::make_shared<BaseConfig>();

    GlobalData::getInstance()->setGlobalConfigDir(QString::fromStdString(baseConfig_->configDir_));
    GlobalData::getInstance()->setControlSession(controlSession);
    GlobalData::getInstance()->setFileSession(fileSession);

    doubleLinker_ = std::make_shared<DoubleLinker>();
    doubleLinker_->SetControlSession(controlSession);
    doubleLinker_->SetFileSession(fileSession);
    GlobalData::getInstance()->setDoubleLinker(doubleLinker_);
    GlobalData::getInstance()->setBaseConfig(baseConfig_);
    connect(this, &relayFile::signalCancelWaitMsg, doubleLinker_.get(), &DoubleLinker::onCancelWaitMsg);

    Logger& logger = Logger::instance();
    logger.setInfo("log/relayFileGUI.log", "relayFileGUI");
    logger.initSimpleLogger(false);

    initControls();
    initLayout();

    gLogControl = logControl_;
    isQuit = false;

    qInstallMessageHandler(ControlMsgHander);
    SPDLOG_INFO("启动");
    initAfter();
}

void relayFile::initAfter()
{
    auto size = baseConfig_->getWidthHeight();
    resize(size.first, size.second);
    setWindowIcon(QIcon("://Resource/Client.ico"));

    auto ver = QString("relayFile v%1 %2 %3").arg(VERSION_NUM, VERSION_GIT_COMMIT, VERSION_DEV);
    setWindowTitle(ver);
}

relayFile::~relayFile()
{
    connectorControl_->Quit();
    delete ui;
}

void relayFile::Quit()
{
    emit signalCancelWaitMsg();
    connectorControl_->Quit();
    doubleLinker_->Quit();
    localExplorerControl_->Quit();
    remoteExplorerControl_->Quit();
    isQuit = true;
}

void relayFile::closeEvent(QCloseEvent* event)
{
    baseConfig_->saveWidthHeight(width(), height());
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
    connect(connectorControl_, &ConnectorControl::signalNoticeClear, this, [this]() { remoteExplorerControl_->onClear(); });

    connect(localExplorerControl_, &ExplorerControl::transTaskRun, this,
            [this](std::shared_ptr<RelayTaskData> data) { onTransTaskRun(data); });
    connect(remoteExplorerControl_, &ExplorerControl::transTaskRun, this,
            [this](std::shared_ptr<RelayTaskData> data) { onTransTaskRun(data); });
    connect(comparisonControl_, &ComparisonControl::transTaskRun, this,
            [this](std::shared_ptr<RelayTaskData> data) { onTransTaskRun(data); });
    connect(comparisonControl_, &ComparisonControl::signalExplorerLocal, localExplorerControl_,
            [this](const QString& path) { localExplorerControl_->onEnterPath(path); });
    connect(comparisonControl_, &ComparisonControl::signalExplorerRemote, remoteExplorerControl_,
            [this](const QString& path) { remoteExplorerControl_->onEnterPath(path); });
}

void relayFile::ControlMsgHander(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (isQuit) {
        return;
    }
    switch (type) {
    default:
        QMetaObject::invokeMethod(gLogControl, [msg]() {
            auto dt = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
            auto retMsg = "[" + dt + "] " + msg + "\n";
            gLogControl->ShowInfo(retMsg);
        });
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

void relayFile::onTransTaskRun(std::shared_ptr<RelayTaskData> data)
{
    // 先检查是否已经连接了服务器和选择了对方ID
    auto controlSession = GlobalData::getInstance()->getControlSession();
    if (!controlSession->getClientCore()->isConnected()) {
        QMessageBox::warning(this, "提示", "请先连接服务器");
        return;
    }
    if (controlSession->getOtherInfo().clientId.empty()) {
        QMessageBox::warning(this, "提示", "请先选择通信对象");
        return;
    }

    RelayTask* relayTask = new RelayTask(this);
    relayTask->setData(data);
    relayTask->show();
}
