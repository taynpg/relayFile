#pragma once

#include <Net/ControlSession.h>
#include <Net/FileSession.h>
#include <QString>

class GlobalData
{
public:
    static GlobalData* getInstance();

public:
    void setControlSession(std::shared_ptr<ControlSession> controlSession);
    void setFileSession(std::shared_ptr<FileSession> fileSession);

    std::shared_ptr<ControlSession> getControlSession();
    std::shared_ptr<FileSession> getFileSession();

    QString getGlobalConfigPath();
    void setGlobalConfigPath(const QString& globalConfigPath);

private:
    std::mutex mutex_;

    std::shared_ptr<ControlSession> controlSession_{};
    std::shared_ptr<FileSession> fileSession_{};
    QString globalConfigPath_;

private:
    GlobalData() = default;
    ~GlobalData() = default;
};