#include "ReplaceArea.h"

#include "ui_ReplaceArea.h"

ReplaceArea::ReplaceArea(QWidget* parent) : QDialog(parent), ui(new Ui::ReplaceArea)
{
    ui->setupUi(this);
    InitUi();
    result_ = std::make_shared<ReplcaeResult>();
}

ReplaceArea::~ReplaceArea()
{
    delete ui;
}

std::shared_ptr<ReplcaeResult> ReplaceArea::getResult() const
{
    return result_;
}

void ReplaceArea::onOk()
{
    result_->useReplace = true;
    result_->areaIndexs.clear();
    if (ui->cBoxName->isChecked()) {
        result_->areaIndexs.push_back(1);
    }
    if (ui->cBoxLocal->isChecked()) {
        result_->areaIndexs.push_back(4);
    }
    if (ui->cBoxRemote->isChecked()) {
        result_->areaIndexs.push_back(5);
    }
    result_->useCase = ui->rbUseCase->isChecked();
    result_->useRegex = ui->rbRegexRep->isChecked();
    accept();
}

void ReplaceArea::onCancel()
{
    result_->useReplace = false;
    reject();
}

void ReplaceArea::InitUi()
{
    auto miniSize = minimumSizeHint();
    miniSize.setWidth(miniSize.width() + 50);
    resize(miniSize);

    ui->rbUseCase->setChecked(true);
    ui->rbNormalRep->setChecked(true);
    ui->cBoxName->setChecked(true);
    ui->cBoxLocal->setChecked(true);
    ui->cBoxRemote->setChecked(true);

    setWindowTitle("替换区域");

    connect(ui->btnOk, &QPushButton::clicked, this, &ReplaceArea::onOk);
    connect(ui->btnCancel, &QPushButton::clicked, this, &ReplaceArea::onCancel);
}
