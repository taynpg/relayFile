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

void GlobalData::setDoubleLinker(std::shared_ptr<DoubleLinker> doubleLinker)
{
    std::lock_guard<std::mutex> lock(mutex_);
    doubleLinker_ = doubleLinker;
}

std::shared_ptr<DoubleLinker> GlobalData::getDoubleLinker()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return doubleLinker_;
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

void GlobalData::setAskDfLocal(std::shared_ptr<BaseAskDF> askDfLocal)
{
    std::lock_guard<std::mutex> lock(mutex_);
    askDfLocal_ = askDfLocal;
}

std::shared_ptr<BaseAskDF> GlobalData::getAskDfLocal()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return askDfLocal_;
}

void GlobalData::setAskDfRemote(std::shared_ptr<BaseAskDF> askDfRemote)
{
    std::lock_guard<std::mutex> lock(mutex_);
    askDfRemote_ = askDfRemote;
}

std::shared_ptr<BaseAskDF> GlobalData::getAskDfRemote()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return askDfRemote_;
}
