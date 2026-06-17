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
    void signalExplorerLocal(const QString& path);
    void signalExplorerRemote(const QString& path);

public:
    explicit ComparisonControl(QWidget* parent = nullptr);
    ~ComparisonControl();

private:
    void initTableWidget();
    void saveConfig();
    void loadConfig(bool notice);
    void delConfig();
    void showEvent(QShowEvent* event) override;
    void insertRow(int id, const QString& name, const QString& type, const QString& mark, const QString& localDir,
                   const QString& remoteDir);

private:
    void initControls();
    void initSignals();
    void onTableContextMenu(const QPoint& pos);
    void onTrans(const QList<QTableWidgetItem*>& items, bool isSend);

private:
    QStringList headers_;
    Ui::ComparisonControl* ui;
    ComDropTable* tableWidget_;
    std::vector<int> delIds_{};
    std::shared_ptr<ComparisonSql> comparisonSql_;
};

#endif   // COMPARISONCONTROL_H
