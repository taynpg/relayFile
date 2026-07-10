#pragma once

#include <QObject>
#include <QString>
#include <Utils/Common.h>
#include <memory>
#include <QTimer>

class BaseConfig;
class ControlSession;
class FileSession;
class DoubleLinker;

class BaseClient : public QObject
{
    Q_OBJECT

signals:
    void signalConnect(const QString& ip, int16_t port);
    void signalAskID(const QString& name);

public:
    explicit BaseClient(QObject* parent = nullptr);
    ~BaseClient() override;

    void Work(const QString& ip, int16_t port);

private:
    void initSignal();
    void initReconnectTimer();
    void onReconnectCheck();

private slots:
    void onSuccess();
    void onError();

private:
    QString curIp_;
    int16_t curPort_;
    QTimer* reconTimer_{};
    std::shared_ptr<BaseConfig> baseConfig_;
    std::shared_ptr<ControlSession> controlSession_;
    std::shared_ptr<FileSession> fileSession_;
    std::shared_ptr<DoubleLinker> doubleLinker_;
    std::shared_ptr<ReconHelper> reconHelper_;
};