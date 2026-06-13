#pragma once

#include <QString>

class Common
{
private:
    Common() = default;
    ~Common() = default;

public:
    static QString GetUUID();
};

class BaseConfig
{
public:
    BaseConfig();
    ~BaseConfig() = default;

public:
    QString getCurrentName();
    QString generateRandomName();

private:
    void genPath();

public:
    QString baseConfigPath_;
};