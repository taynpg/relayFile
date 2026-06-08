#include "ComparisonControl.h"

#include "ui_ComparisonControl.h"

ComparisonControl::ComparisonControl(QWidget* parent) : QDialog(parent), ui(new Ui::ComparisonControl)
{
    ui->setupUi(this);
}

ComparisonControl::~ComparisonControl()
{
    delete ui;
}
