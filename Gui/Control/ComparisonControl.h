#ifndef COMPARISONCONTROL_H
#define COMPARISONCONTROL_H

#include <QDialog>

#include "Base/GuiDefine.hpp"
#include "OwnTableWidget.h"
#include "Sqlite/ComparisonSql.h"

namespace Ui {
class ComparisonControl;
}

class ComparisonControl : public QDialog
{
    Q_OBJECT

signals:
    void transTaskRun(std::shared_ptr<RelayTaskData> data);

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
    void initControls();
    void initSignals();
    void onTableContextMenu(const QPoint& pos);
    void onTrans(const QList<QTableWidgetItem*>& items, bool isSend);

private:
    QStringList headers_;
    Ui::ComparisonControl* ui;
    ComDropTable* tableWidget_;
    std::shared_ptr<ComparisonSql> comparisonSql_;
};

#endif   // COMPARISONCONTROL_H
