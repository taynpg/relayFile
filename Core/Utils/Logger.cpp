#include "Logger.h"

#include <iostream>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

Logger::Logger()
{
}

void Logger::setInfo(const std::string& logPath, const std::string& markStr)
{
    logPath_ = logPath;
    markStr_ = markStr;
}

bool Logger::initSimpleLogger(bool enableConsole)
{
    try {
        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(logPath_, 0, 0, 60);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l]: %v");

        std::vector<spdlog::sink_ptr> sinks{file_sink};
        if (enableConsole) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("%Y-%m-%d %H:%M:%S.%e %^[%l]: %v%$");
            sinks.push_back(console_sink);
        }

        logger_ = std::make_shared<spdlog::logger>(markStr_, sinks.begin(), sinks.end());
        spdlog::register_logger(logger_);
        spdlog::set_default_logger(logger_);
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
        // spdlog::flush_every(std::chrono::seconds(5));
        return true;
    } catch (const std::exception& e) {
        std::cerr << __FUNCTION__ << ":" << e.what() << std::endl;
        return false;
    }
}

bool Logger::initLogger(bool enableConsole)
{
    try {
        spdlog::level::level_enum lv = spdlog::level::trace;

        // auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("xx", 1024 * 50, 3);
        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(logPath_, 0, 0);

        std::vector<spdlog::sink_ptr> sinks{file_sink};

        if (enableConsole) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("%H:%M:%S.%e %^[%l]: %v%$\n    @%s:%!:%#,%t\n");
            sinks.push_back(console_sink);
        }

        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l]: %v\n    @%s:%!:%#,%t\n");
        logger_ = std::make_shared<spdlog::logger>(markStr_, sinks.begin(), sinks.end());

        spdlog::register_logger(logger_);
        spdlog::set_default_logger(logger_);
        spdlog::set_level(lv);
        spdlog::flush_on(lv);
        // spdlog::flush_every(std::chrono::seconds(5));

        return true;
    } catch (const std::exception& e) {
        std::cerr << __FUNCTION__ << ":" << e.what() << std::endl;
        return false;
    }
}

std::shared_ptr<spdlog::logger> Logger::getLogger()
{
    return logger_;
}
