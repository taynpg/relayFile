#include "LogControl.h"

#include <QDateTime>
#include <Utils/Logger.h>

#include "ui_LogControl.h"

LogControl::LogControl(QWidget* parent) : QDialog(parent), ui(new Ui::LogControl)
{
    ui->setupUi(this);
}

LogControl::~LogControl()
{
    delete ui;
}

void LogControl::Debug(const QString& msg)
{
    auto cpMsg = msg;
    SPDLOG_DEBUG(cpMsg.toStdString());
    formatMsg(cpMsg);
    ui->pedLog->appendPlainText(cpMsg + "\n");
}

void LogControl::Info(const QString& msg)
{
    auto cpMsg = msg;
    SPDLOG_INFO(cpMsg.toStdString());
    formatMsg(cpMsg);
    ui->pedLog->appendPlainText(msg + "\n");
}

void LogControl::Warn(const QString& msg)
{
    auto cpMsg = msg;
    SPDLOG_WARN(cpMsg.toStdString());
    formatMsg(cpMsg);
    ui->pedLog->appendPlainText(cpMsg + "\n");
}

void LogControl::Error(const QString& msg)
{
    auto cpMsg = msg;
    SPDLOG_ERROR(cpMsg.toStdString());
    formatMsg(cpMsg);
    ui->pedLog->appendPlainText(cpMsg + "\n");
}

void LogControl::formatMsg(QString& msg)
{
    auto dt = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    msg = "[" + dt + "] " + msg;
}