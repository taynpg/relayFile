#include "ConnectorControl.h"

#include "ui_ConnectorControl.h"

ConnectorControl::ConnectorControl(QWidget* parent) : QDialog(parent), ui(new Ui::ConnectorControl)
{
    ui->setupUi(this);
}

ConnectorControl::~ConnectorControl()
{
    delete ui;
}
