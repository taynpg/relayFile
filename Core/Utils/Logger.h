#pragma once

#include <spdlog/spdlog.h>

class Logger
{
public:
    Logger();

public:
    bool initLogger(bool enableConsole = false);
    void setInfo(const std::string& logPath, const std::string& markStr);
    std::shared_ptr<spdlog::logger> getLogger();

private:
    std::string logPath_;
    std::string markStr_;
    std::shared_ptr<spdlog::logger> logger_;
};
