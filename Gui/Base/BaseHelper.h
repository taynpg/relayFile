#pragma once

#include <Net/ClientHelper.h>
#include <Net/ControlSession.h>
#include <Net/FileSession.h>
#include <QString>

#include "AskDirFile/BaseAskDF.h"

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

    std::shared_ptr<DoubleLinker> getDoubleLinker();
    std::shared_ptr<ControlSession> getControlSession();
    std::shared_ptr<FileSession> getFileSession();
    std::shared_ptr<BaseAskDF> getAskDfLocal();
    std::shared_ptr<BaseAskDF> getAskDfRemote();

    QString getGlobalConfigPath();
    void setGlobalConfigPath(const QString& globalConfigPath);

private:
    std::mutex mutex_;

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