#ifndef COMPARISONCONTROL_H
#define COMPARISONCONTROL_H

#include <QDialog>
#include "OwnTableWidget.h"

namespace Ui {
class ComparisonControl;
}

class ComparisonControl : public QDialog
{
    Q_OBJECT

public:
    explicit ComparisonControl(QWidget* parent = nullptr);
    ~ComparisonControl();

private:
    void initTableWidget();

private:
    ComDropTable* tableWidget_;
    QStringList headers_;
    Ui::ComparisonControl* ui;
};

#endif   // COMPARISONCONTROL_H
