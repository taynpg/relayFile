#include "Logger.h"

#include <QMetaObject>
#include <QString>
#include <iostream>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// =========================
// QtSink（核心）
// =========================
template <typename Mutex> class QtSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    explicit QtSink(Logger* logger) : logger_(logger)
    {
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);

        QString text = QString::fromUtf8(formatted.data(), static_cast<int>(formatted.size()));

        switch (msg.level) {
        case spdlog::level::trace:
            QMetaObject::invokeMethod(logger_, "signalLogTrace", Qt::QueuedConnection, Q_ARG(QString, text));
            break;
        case spdlog::level::debug:
            QMetaObject::invokeMethod(logger_, "signalLogDebug", Qt::QueuedConnection, Q_ARG(QString, text));
            break;
        case spdlog::level::info:
            QMetaObject::invokeMethod(logger_, "signalLogInfo", Qt::QueuedConnection, Q_ARG(QString, text));
            break;
        case spdlog::level::warn:
            QMetaObject::invokeMethod(logger_, "signalLogWarn", Qt::QueuedConnection, Q_ARG(QString, text));
            break;
        case spdlog::level::err:
        case spdlog::level::critical:
            QMetaObject::invokeMethod(logger_, "signalLogError", Qt::QueuedConnection, Q_ARG(QString, text));
            break;
        default:
            break;
        }
    }

    void flush_() override
    {
    }

private:
    Logger* logger_;
};

using QtSinkMt = QtSink<std::mutex>;

Logger::Logger(QObject* parent) : QObject(parent)
{
}

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

void Logger::setInfo(const std::string& logPath, const std::string& markStr)
{
    logPath_ = logPath;
    markStr_ = markStr;
}

std::shared_ptr<spdlog::logger> Logger::getLogger()
{
    return logger_;
}

bool Logger::initLogger(bool enableConsole)
{
    try {
        auto fileSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(logPath_, 0, 0);

        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l]: %v\n    @%s:%!:%#,%t\n");

        std::vector<spdlog::sink_ptr> sinks{fileSink};

        if (enableConsole) {
            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_pattern("%H:%M:%S.%e %^[%l]: %v%$\n    @%s:%!:%#,%t\n");
            sinks.push_back(consoleSink);
        }

        auto qtSink = std::make_shared<QtSinkMt>(this);
        qtSink->set_pattern("[%H:%M:%S.%e][%l] %v");
        sinks.push_back(qtSink);

        logger_ = std::make_shared<spdlog::logger>(markStr_, sinks.begin(), sinks.end());

        spdlog::register_logger(logger_);
        spdlog::set_default_logger(logger_);
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);

        return true;
    } catch (const std::exception& e) {
        std::cerr << __FUNCTION__ << ": " << e.what() << std::endl;
        return false;
    }
}

bool Logger::initSimpleLogger(bool enableConsole)
{
    try {
        auto fileSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(logPath_, 0, 0, 60);

        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l]: %v");

        std::vector<spdlog::sink_ptr> sinks{fileSink};

        if (enableConsole) {
            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_pattern("%Y-%m-%d %H:%M:%S.%e %^[%l]: %v%$");
            sinks.push_back(consoleSink);
        }

        auto qtSink = std::make_shared<QtSinkMt>(this);
        qtSink->set_pattern("[%H:%M:%S.%e][%l] %v");
        sinks.push_back(qtSink);

        logger_ = std::make_shared<spdlog::logger>(markStr_, sinks.begin(), sinks.end());

        spdlog::register_logger(logger_);
        spdlog::set_default_logger(logger_);
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);

        return true;
    } catch (const std::exception& e) {
        std::cerr << __FUNCTION__ << ": " << e.what() << std::endl;
        return false;
    }
}
