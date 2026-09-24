#include "Setting.h"

#include <QIntValidator>

#include "Base/MessageBoxHelper.h"
#include "ui_Setting.h"

Setting::Setting(QWidget* parent) : QDialog(parent), ui(new Ui::Setting)
{
    ui->setupUi(this);

    config_ = GlobalData::getInstance()->getBaseConfig();

    InitUi();
    onLoadDefault();
    rbReconChanged();
}

Setting::~Setting()
{
    delete ui;
}

void Setting::InitUi()
{
    connect(ui->rbNoRecon, &QRadioButton::clicked, this, &Setting::rbReconChanged);
    connect(ui->rbRecon, &QRadioButton::clicked, this, &Setting::rbReconChanged);
    ui->rbNoRecon->setChecked(true);
    setWindowTitle("设置");

    auto* validator = new QIntValidator(0, INT_MAX, this);
    ui->edReconInterval->setValidator(validator);
    ui->edMaxRetry->setValidator(validator);
    ui->edThreshold->setValidator(new QIntValidator(1, INT_MAX, this));

    connect(ui->btnSave, &QPushButton::clicked, this, &Setting::onSave);
    connect(ui->btnExit, &QPushButton::clicked, this, &Setting::onExit);
}

void Setting::rbReconChanged()
{
    if (ui->rbNoRecon->isChecked()) {
        ui->edReconInterval->setEnabled(false);
        ui->label->setEnabled(false);
        ui->edMaxRetry->setEnabled(false);
    }
    if (ui->rbRecon->isChecked()) {
        ui->edReconInterval->setEnabled(true);
        ui->label->setEnabled(true);
        ui->edMaxRetry->setEnabled(true);
    }
}

void Setting::onLoadDefault()
{
    RetryCon con;
    config_->getReconInterval(con);

    ui->edReconInterval->setText(QString::number(con.interval));
    ui->edMaxRetry->setText(QString::number(con.count));

    if (con.useRecon) {
        ui->rbRecon->setChecked(true);
    } else {
        ui->rbNoRecon->setChecked(true);
    }

    CompressConfig ccfg;
    if (!config_->getCompress(ccfg)) {
        ccfg.enabled = false;
        ccfg.thresholdMB = 2048;
    }
    ui->cbCompress->setChecked(ccfg.enabled);
    ui->edThreshold->setText(QString::number(ccfg.thresholdMB));
}

void Setting::onSave()
{
    RetryCon con;
    if (ui->rbRecon->isChecked()) {
        con.useRecon = true;
    } else {
        con.useRecon = false;
    }
    con.count = ui->edMaxRetry->text().toInt();
    con.interval = ui->edReconInterval->text().toInt();
    if (!config_->saveReconInterval(con)) {
        MessageBoxHelper::information(this, "提示", "保存失败");
        return;
    }

    CompressConfig ccfg;
    ccfg.enabled = ui->cbCompress->isChecked();
    ccfg.thresholdMB = ui->edThreshold->text().toInt();
    if (ccfg.thresholdMB <= 0) ccfg.thresholdMB = 2048;
    if (!config_->saveCompress(ccfg)) {
        MessageBoxHelper::information(this, "提示", "保存失败");
        return;
    }
    accept();
}

void Setting::onExit()
{
    reject();
}
