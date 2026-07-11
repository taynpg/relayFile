#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

/**
 * @brief Qt + spdlog 统一日志桥接器
 *
 * 使用方式：
 * 1. Logger::instance().initLogger(true);
 * 2. spdlog::info("xxx");
 * 3. qDebug() << "yyy";
 * 4. connect(&Logger::instance(), &Logger::signalLogInfo, ...)
 */
class Logger final : public QObject
{
    Q_OBJECT

signals:
    void signalLogTrace(const QString& str);
    void signalLogDebug(const QString& str);
    void signalLogInfo(const QString& str);
    void signalLogWarn(const QString& str);
    void signalLogError(const QString& str);

public:
    ~Logger() override = default;

    static Logger& instance();

    explicit Logger(QObject* parent = nullptr);

    /// 两个 Init 只能调用一个
    bool initLogger(bool enableConsole = false);
    bool initSimpleLogger(bool enableConsole = false);

    void setInfo(const std::string& logPath, const std::string& markStr);
    std::shared_ptr<spdlog::logger> getLogger();

private:
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    std::string logPath_;
    std::string markStr_;
    std::shared_ptr<spdlog::logger> logger_;
};
