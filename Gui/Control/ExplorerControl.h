#ifndef EXPLORERCONTROL_H
#define EXPLORERCONTROL_H

#include <Protocol/FileMeta.h>
#include <QDialog>
#include <QMutex>
#include <QTableWidget>
#include <QWaitCondition>

#include "Base/AskDirFile/BaseAskDF.h"
#include "Base/GuiDefine.h"
#include "Base/MessageBoxHelper.h"
#include "Base/WorkerThread.hpp"
#include "Form/WaitDialog.h"
#include "OwnTableWidget.h"

namespace Ui {
class ExplorerControl;
}

struct ExplorerSharedData {
    QString currentPath_;
};

class ExplorerControl : public QDialog
{
    Q_OBJECT

public:
    explicit ExplorerControl(QWidget* parent = nullptr);
    ~ExplorerControl();

signals:
    void transTaskRun(std::shared_ptr<RelayTaskData> data);
    void fileListChanged(bool isSuccess, const std::vector<FileMeta>& fileList);
    void signalStartWaitForm();
    void signalWaitQuit();
    void signalShowNotice(const QString& msg);
    void signalWaitQuitMsg(const QString& msg);
    void signalShouldConfirm(const QString& title, const QString& text);

public slots:
    void onFileListChanged(bool isSuccess, const std::vector<FileMeta>& fileList);
    void onClear();

public:
    void Quit();
    void setAskDF(AskType askType);
    std::shared_ptr<BaseAskDF> getAskDF();
    QString getCurrentPath();
    void setCurrentPath(const QString& path);
    void tellInfo(ExplorerSharedData& es);
    void setTellInfoCall(std::function<void(ExplorerSharedData& es)> call);

private:
    void initControl();
    void setFileItem(const FileMeta& meta, int row);
    QString typeStr(FileType type);
    void baseTask();
    void initSignals();

public:
    void onEnter();
    void onHome(bool autoEnter = true);
    void onRefresh();
    void onUp();
    void onDoubleClick();
    void enterPath(const QString& path);
    void onTableContextMenu(const QPoint& pos);
    void onEnterPath(const QString& path);
    void onShowFileMeta(const FileMeta& meta);

private:
    void onRename(int row);
    void onSHA256(int row);
    void onRoughCheck(int row);
    void onDelete(const std::vector<int>& rows);
    void onNewDir(int row);
    void onShowFileMetaInfo(int row);
    void onArchive(const std::vector<int>& rows);
    void onUnArchive(int row);
    void onShowWaitDialog();
    void onShowNotice(const QString& msg);
    void onConfirm(const QString& title, const QString& text);
    void onHeaderClicked(int index);
    void onFilterChanged();
    void onToggleFilter(bool checked);
    void onResetFilter();

private:
    // 基于 currentMetaList_（源数据）按当前筛选/排序条件重建表格
    void rebuildView();
    // 收集当前目录全部文件后缀集合并刷新后缀多选框（保留仍存在的旧选中项）
    void updateExtOptions();
    static bool lessThanMeta(const FileMeta& a, const FileMeta& b, int sortColumn);

private:
    void uiPathSet(const QString& path);
    void uiPathSet(const QString& path, const std::vector<std::string>& drivers);
    void actionTrans(const QList<QTableWidgetItem*>& datas);
    WaitDialog* newWaitDialog();

private:
    Ui::ExplorerControl* ui;
    ExpDropTable* tableWidget_;
    AskType askType_{};
    QStringList headers_{};
    std::shared_ptr<BaseAskDF> askDf_{};
    std::shared_ptr<WorkerThread<ExplorerControl>> workerThread_{};

private:
    QMutex curPathMut_;
    QString currentPath_;
    std::vector<FileMeta> currentMetaList_;
    std::vector<FileMeta> fileMetaList_;
    std::function<void(ExplorerSharedData& es)> tellInfoCall_;

    // 排序状态（列：1=名称 2=时间 3=类型 4=大小）
    int sortColumn_{1};
    Qt::SortOrder sortOrder_{Qt::AscendingOrder};
    // 筛选状态
    QString filterName_;
    QStringList filterExts_;   // 后缀选项（小写、不带点；无后缀项用特殊标签）；空=不按后缀筛选

private:
    QMutex askMut_;
    WaitDialog* waitDialog_;
    QWaitCondition confirmCond_;
    std::atomic<bool> isTaskRunning_{};
    bool isConfirmRun_{false};
    bool confirmResult_{false};
};

#endif   // EXPLORERCONTROL_H
