#pragma once

#include <QMutex>
#include <QString>

class Common
{
private:
    Common() = default;
    ~Common() = default;

public:
    static QString GetUUID();
};

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

private:
    void genPath();

public:
    QMutex mutex_;
    QString baseConfigPath_;
};