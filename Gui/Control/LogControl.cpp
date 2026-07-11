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

    connect(&Logger::instance(), &Logger::signalLogTrace, this, &LogControl::ShowInfo);
    connect(&Logger::instance(), &Logger::signalLogDebug, this, &LogControl::ShowInfo);
    connect(&Logger::instance(), &Logger::signalLogInfo, this, &LogControl::ShowInfo);
    connect(&Logger::instance(), &Logger::signalLogWarn, this, &LogControl::ShowInfo);
    connect(&Logger::instance(), &Logger::signalLogError, this, &LogControl::ShowInfo);
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

void LogControl::ShowInfo(const QString& msg)
{
    ui->pedLog->moveCursor(QTextCursor::End);
    ui->pedLog->insertPlainText(msg);
}
