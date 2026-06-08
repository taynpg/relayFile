#pragma once

#include <spdlog/spdlog.h>

class Logger
{
public:
    Logger();

public:
    // 两个Init只能调用一个
    bool initLogger(bool enableConsole = false);
    // 两个Init只能调用一个
    bool initSimpleLogger(bool enableConsole = false);
    void setInfo(const std::string& logPath, const std::string& markStr);
    std::shared_ptr<spdlog::logger> getLogger();

private:
    std::string logPath_;
    std::string markStr_;
    std::shared_ptr<spdlog::logger> logger_;
};
