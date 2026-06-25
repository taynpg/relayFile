#include "Client.h"

#include <Net/ClientHelper.h>
#include <Utils/Logger.h>

BaseClient::BaseClient(QObject* parent) : QObject(parent)
{
}

void BaseClient::Work(const QString& ip, int16_t port)
{
    baseConfig_ = std::make_shared<BaseConfig>();
    controlSession_ = std::make_shared<ControlSession>();
    fileSession_ = std::make_shared<FileSession>();

    doubleLinker_ = std::make_shared<DoubleLinker>();
    doubleLinker_->SetControlSession(controlSession_);
    doubleLinker_->SetFileSession(fileSession_);
    
    initSignal();
    emit signalConnect(ip, port);
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
    if (baseConfig_) {
        emit signalAskID(baseConfig_->getCurrentName());
    }
}

void BaseClient::onError()
{
    qInfo() << "断开了连接...";
}