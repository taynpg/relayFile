#include "LogControl.h"

#include <QAction>
#include <QDateTime>
#include <QMenu>
#include <Utils/Logger.h>

#include "Form/Setting.h"
#include "ui_LogControl.h"

LogControl::LogControl(QWidget* parent) : QDialog(parent), ui(new Ui::LogControl)
{
    ui->setupUi(this);
    ui->pedLog->setReadOnly(true);
    InitMenu();
}

LogControl::~LogControl()
{
    delete ui;
}

void LogControl::InitMenu()
{
    auto* menu = new QMenu(ui->pedLog);
    auto* acSetting = new QAction("设置");
    menu->addAction(acSetting);

    connect(acSetting, &QAction::triggered, this, [this]() {
        Setting setting;
        setting.exec();
    });
    ui->pedLog->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->pedLog, &QPlainTextEdit::customContextMenuRequested, this,
            [this, menu](const QPoint& pos) { menu->exec(ui->pedLog->mapToGlobal(pos)); });
}

void LogControl::Debug(const QString& msg)
{
    auto cpMsg = msg;
    SPDLOG_DEBUG(cpMsg.toStdString());
    formatMsg(cpMsg);
    ui->pedLog->appendPlainText(cpMsg);
}

void LogControl::Info(const QString& msg)
{
    auto cpMsg = msg;
    SPDLOG_INFO(cpMsg.toStdString());
    formatMsg(cpMsg);
    ui->pedLog->appendPlainText(cpMsg);
}

void LogControl::Warn(const QString& msg)
{
    auto cpMsg = msg;
    SPDLOG_WARN(cpMsg.toStdString());
    formatMsg(cpMsg);
    ui->pedLog->appendPlainText(cpMsg);
}

void LogControl::Error(const QString& msg)
{
    auto cpMsg = msg;
    SPDLOG_ERROR(cpMsg.toStdString());
    formatMsg(cpMsg);
    ui->pedLog->appendPlainText(cpMsg);
}

void LogControl::formatMsg(QString& msg)
{
    auto dt = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    msg = "[" + dt + "] " + msg;
}
