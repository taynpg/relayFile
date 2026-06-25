#pragma once

#include <QMutex>
#include <QString>
#include <nlohmann/json.hpp>

struct IpHistory {
    std::vector<std::string> history;
    std::string current;
};

class Common
{
private:
    Common() = default;
    ~Common() = default;

public:
    static QString GetUUID();
    static QString GenSha256(const QString& str, bool isFile);
    static QVector<QString> GetLocalDrivers();
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
    bool saveJson(const nlohmann::json& j);
    nlohmann::json loadJson();

private:
    void genPath();

public:
    QMutex mutex_;
    std::string configDir_{};
    QString baseConfigPath_;
};