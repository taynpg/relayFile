#ifndef COMPARISONCONTROL_H
#define COMPARISONCONTROL_H

#include <QDialog>

#include "OwnTableWidget.h"
#include "Sqlite/ComparisonSql.h"

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
    void saveConfig();
    void loadConfig(bool notice);
    void delConfig();
    void showEvent(QShowEvent* event) override;

private:
    void initSignals();

private:
    QStringList headers_;
    Ui::ComparisonControl* ui;
    ComDropTable* tableWidget_;
    std::shared_ptr<ComparisonSql> comparisonSql_;
};

#endif   // COMPARISONCONTROL_H
