#include "Client.h"

#include <Net/ClientHelper.h>
#include <Utils/Logger.h>

BaseClient::BaseClient(QObject* parent) : QObject(parent)
{
    reconHelper_ = std::make_shared<ReconHelper>();
}

BaseClient::~BaseClient()
{
    reconTimer_->stop();
}

void BaseClient::Work(const QString& ip, int16_t port)
{
    baseConfig_ = std::make_shared<BaseConfig>();
    controlSession_ = std::make_shared<ControlSession>();
    fileSession_ = std::make_shared<FileSession>();

    doubleLinker_ = std::make_shared<DoubleLinker>();
    doubleLinker_->SetControlSession(controlSession_);
    doubleLinker_->SetFileSession(fileSession_);

    curIp_ = ip;
    curPort_ = port;

    initSignal();
    initReconnectTimer();
}

void BaseClient::initReconnectTimer()
{
    // 重连助手
    reconHelper_ = std::make_shared<ReconHelper>();
    RetryCon rc;
    baseConfig_->getReconInterval(rc);
    reconHelper_->setRetryCon(rc);
    reconHelper_->markConnected(false);
    reconHelper_->markNeedRecon(true);

    reconTimer_ = new QTimer(this);
    connect(reconTimer_, &QTimer::timeout, this, &BaseClient::onReconnectCheck);
    reconTimer_->start(1000);

    if (!rc.useRecon) {
        emit signalConnect(curIp_, curPort_);
    }
}

void BaseClient::onReconnectCheck()
{
    if (reconHelper_->shouleReconnct()) {
        qWarning() << "Auto reconnect To => " << curIp_ << ":" << curPort_;
        emit signalConnect(curIp_, curPort_);
    }
}

void BaseClient::initSignal()
{
    auto* cli = controlSession_->getClientCore();
    connect(this, &BaseClient::signalConnect, cli, &ClientCore::connectToServer);
    connect(cli, &ClientCore::signalConnected, this, &BaseClient::onSuccess);
    connect(cli, &ClientCore::signalDisconnected, this, &BaseClient::onError);
    connect(this, &BaseClient::signalAskID, this, [this](const QString& name) {
        if (doubleLinker_ && doubleLinker_->GetControlSession()) {
            doubleLinker_->GetControlSession()->AskOwnID(name);
        }
    });
}

void BaseClient::onSuccess()
{
    reconHelper_->markConnected(true);
    if (baseConfig_) {
        emit signalAskID(baseConfig_->getCurrentName());
    }
}

void BaseClient::onError()
{
    reconHelper_->markConnected(false);
    qInfo() << "断开了连接...";
}