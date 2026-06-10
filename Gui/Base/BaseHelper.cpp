#include "BaseHelper.h"

GlobalData* GlobalData::getInstance()
{
    static GlobalData instance;
    return &instance;
}

void GlobalData::setControlSession(std::shared_ptr<ControlSession> controlSession)
{
    std::lock_guard<std::mutex> lock(mutex_);
    controlSession_ = controlSession;
}

void GlobalData::setFileSession(std::shared_ptr<FileSession> fileSession)
{
    std::lock_guard<std::mutex> lock(mutex_);
    fileSession_ = fileSession;
}

std::shared_ptr<ControlSession> GlobalData::getControlSession()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return controlSession_;
}

std::shared_ptr<FileSession> GlobalData::getFileSession()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return fileSession_;
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