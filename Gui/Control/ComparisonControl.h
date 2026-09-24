#ifndef COMPARISONCONTROL_H
#define COMPARISONCONTROL_H

#include <QDialog>
#include <QListWidgetItem>

#include "Base/GuiDefine.h"
#include "Base/WorkerThread.hpp"
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
    void applyConfig(const QString& config);
    void delConfig();
    void showEvent(QShowEvent* event) override;
    void insertRow(int id, const QString& name, const QString& type, const QString& mark, const QString& localDir,
                   const QString& remoteDir, const QString& remote);
    bool isNameValid(const QString& name);
    void exeReplace();

private:
    void initControls();
    void initSignals();
    void onTableContextMenu(const QPoint& pos);
    void onRoughCheck(const QList<QTableWidgetItem*>& items);
    void onTrans(const QList<QTableWidgetItem*>& items, bool isSend);
    void onNewConfig();
    void onRefreshMark();
    void onListContextMenu(const QPoint& pos);
    void onListItemChanged();
    void onListDoubleClick(QListWidgetItem* item);
    void onCopyConfig();

private:
    QStringList headers_;
    Ui::ComparisonControl* ui;
    ComDropTable* tableWidget_;
    std::vector<int> delIds_{};
    QVector<CompDataItem> curItems_;
    bool autoChange_{};
    std::shared_ptr<ComparisonSql> comparisonSql_;
    std::shared_ptr<WorkerThread<ComparisonControl>> workerThread_{};
};

#endif   // COMPARISONCONTROL_H
