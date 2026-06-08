#include "RelayTask.h"

#include "ui_RelayTask.h"

RelayTask::RelayTask(QWidget* parent) : QDialog(parent), ui(new Ui::RelayTask)
{
    ui->setupUi(this);
}

RelayTask::~RelayTask()
{
    delete ui;
}
