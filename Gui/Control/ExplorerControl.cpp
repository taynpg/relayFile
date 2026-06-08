#include "ExplorerControl.h"

#include "ui_ExplorerControl.h"

ExplorerControl::ExplorerControl(QWidget* parent) : QDialog(parent), ui(new Ui::ExplorerControl)
{
    ui->setupUi(this);
}

ExplorerControl::~ExplorerControl()
{
    delete ui;
}
