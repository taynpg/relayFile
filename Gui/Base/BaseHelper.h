#pragma once

#include <Net/ClientHelper.h>
#include <Net/ControlSession.h>
#include <Net/FileSession.h>
#include <QString>
#include <Utils/Common.h>

#include "AskDirFile/BaseAskDF.h"

struct IpHistory {
    std::vector<std::string> history;
    std::string current;
};

class BaseConfig
{
public:
    BaseConfig();
    ~BaseConfig() = default;

public:
    QString getCurrentName();
    QString generateRandomName();
    bool getIpHistory(IpHistory& history);
    bool pushOneIp(const std::string& ip);
    std::pair<int, int> getWidthHeight();
    bool saveWidthHeight(int width, int height);

private:
    void genPath();

public:
    QMutex mutex_;
    QString baseConfigPath_;
};

class GlobalData
{
public:
    static GlobalData* getInstance();

public:
    void setDoubleLinker(std::shared_ptr<DoubleLinker> doubleLinker);
    void setControlSession(std::shared_ptr<ControlSession> controlSession);
    void setFileSession(std::shared_ptr<FileSession> fileSession);
    void setAskDfLocal(std::shared_ptr<BaseAskDF> askDfLocal);
    void setAskDfRemote(std::shared_ptr<BaseAskDF> askDfRemote);
    void setBaseConfig(std::shared_ptr<BaseConfig> baseConfig);

    std::shared_ptr<DoubleLinker> getDoubleLinker();
    std::shared_ptr<ControlSession> getControlSession();
    std::shared_ptr<FileSession> getFileSession();
    std::shared_ptr<BaseAskDF> getAskDfLocal();
    std::shared_ptr<BaseAskDF> getAskDfRemote();
    std::shared_ptr<BaseConfig> getBaseConfig();

    QString getGlobalConfigPath();
    void setGlobalConfigPath(const QString& globalConfigPath);

private:
    std::mutex mutex_;

    std::shared_ptr<BaseConfig> baseConfig_{};
    std::shared_ptr<DoubleLinker> doubleLinker_{};
    std::shared_ptr<ControlSession> controlSession_{};
    std::shared_ptr<FileSession> fileSession_{};
    std::shared_ptr<BaseAskDF> askDfLocal_{};
    std::shared_ptr<BaseAskDF> askDfRemote_{};
    QString globalConfigPath_;

private:
    GlobalData() = default;
    ~GlobalData() = default;
};