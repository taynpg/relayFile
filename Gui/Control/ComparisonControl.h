#ifndef COMPARISONCONTROL_H
#define COMPARISONCONTROL_H

#include <QDialog>

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
    Ui::ComparisonControl* ui;
};

#endif   // COMPARISONCONTROL_H
