#include "LogControl.h"

#include "ui_LogControl.h"

LogControl::LogControl(QWidget* parent) : QDialog(parent), ui(new Ui::LogControl)
{
    ui->setupUi(this);
}

LogControl::~LogControl()
{
    delete ui;
}
