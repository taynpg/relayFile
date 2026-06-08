#pragma once

#include <Net/ClientCore.h>
#include <QString>

class GlobalData
{
public:
    static GlobalData* getInstance();

public:
    void setClientControl(ClientCore* clientControl);
    void setClientFile(ClientCore* clientFile);

    ClientCore* getClientControl();
    ClientCore* getClientFile();

    QString getGlobalConfigPath();
    void setGlobalConfigPath(const QString& globalConfigPath);

private:
    std::mutex mutex_;
    ClientCore* clientControl_{};
    ClientCore* clientFile_{};
    QString globalConfigPath_;

private:
    GlobalData() = default;
    ~GlobalData() = default;
};