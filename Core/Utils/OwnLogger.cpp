#include "OwnLogger.h"

#include "Logger.h"

OwnLogger::OwnLogger(QObject* parent) : QObject(parent)
{
    connect(&Logger::instance(), &Logger::signalLogTrace, this, &OwnLogger::ShowInfo);
    connect(&Logger::instance(), &Logger::signalLogDebug, this, &OwnLogger::ShowInfo);
    connect(&Logger::instance(), &Logger::signalLogInfo, this, &OwnLogger::ShowInfo);
    connect(&Logger::instance(), &Logger::signalLogWarn, this, &OwnLogger::ShowInfo);
    connect(&Logger::instance(), &Logger::signalLogError, this, &OwnLogger::ShowInfo);
}

OwnLogger::~OwnLogger()
{
}

void OwnLogger::ConsoleMsgHander(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Q_UNUSED(context);
    switch (type) {
    case QtDebugMsg:
        SPDLOG_DEBUG(msg.toStdString());
        break;
    case QtInfoMsg:
        SPDLOG_INFO(msg.toStdString());
        break;
    case QtWarningMsg:
        SPDLOG_WARN(msg.toStdString());
        break;
    case QtCriticalMsg:
        SPDLOG_ERROR(msg.toStdString());
        break;
    case QtFatalMsg:
        SPDLOG_CRITICAL(msg.toStdString());
        break;
    default:
        SPDLOG_WARN("Unknown QtMsgType type.");
        break;
    }
}

void OwnLogger::ShowInfo(const QString& data)
{
    qInfo() << data;
}
