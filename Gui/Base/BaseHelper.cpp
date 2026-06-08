#include "BaseHelper.h"

GlobalData* GlobalData::getInstance()
{
    static GlobalData instance;
    return &instance;
}

void GlobalData::setClientControl(ClientCore* clientControl)
{
    std::lock_guard<std::mutex> lock(mutex_);
    clientControl_ = clientControl;
}

void GlobalData::setClientFile(ClientCore* clientFile)
{
    std::lock_guard<std::mutex> lock(mutex_);
    clientFile_ = clientFile;
}

ClientCore* GlobalData::getClientControl()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return clientControl_;
}

ClientCore* GlobalData::getClientFile()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return clientFile_;
}

QString GlobalData::getGlobalConfigPath()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return globalConfigPath_;
}

void GlobalData::setGlobalConfigPath(const QString& globalConfigPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    globalConfigPath_ = globalConfigPath;
}