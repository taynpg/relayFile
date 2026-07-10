#ifndef SETTING_H
#define SETTING_H

#include <QDialog>

#include "Base/BaseHelper.h"

namespace Ui {
class Setting;
}

class Setting : public QDialog
{
    Q_OBJECT

public:
    explicit Setting(QWidget* parent = nullptr);
    ~Setting();

private:
    void InitUi();
    void rbReconChanged();
    void onLoadDefault();
    void onSave();
    void onExit();

private:
    Ui::Setting* ui;
    std::shared_ptr<BaseConfig> config_;
};

#endif   // SETTING_H
