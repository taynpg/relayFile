#include "relayFile.h"

#include "./ui_relayFile.h"

relayFile::relayFile(QWidget* parent) : QWidget(parent), ui(new Ui::relayFile)
{
    ui->setupUi(this);
}

relayFile::~relayFile()
{
    delete ui;
}
