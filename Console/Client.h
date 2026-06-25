#pragma once

#include <QObject>
#include <QString>
#include <memory>

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
    ~BaseClient() override = default;
    
    void Work(const QString& ip, int16_t port);

private:
    void initSignal();

private slots:
    void onSuccess();
    void onError();

private:
    std::shared_ptr<BaseConfig> baseConfig_;
    std::shared_ptr<ControlSession> controlSession_;
    std::shared_ptr<FileSession> fileSession_;
    std::shared_ptr<DoubleLinker> doubleLinker_;
};