#ifndef EXPLORERCONTROL_H
#define EXPLORERCONTROL_H

#include <Protocol/FileMeta.h>
#include <QDialog>
#include <QTableWidget>

#include "Base/AskDirFile/BaseAskDF.h"
#include "Base/WorkerThread.hpp"
#include "RelayTask.h"

namespace Ui {
class ExplorerControl;
}

class ExplorerControl : public QDialog
{
    Q_OBJECT

public:
    explicit ExplorerControl(QWidget* parent = nullptr);
    ~ExplorerControl();

signals:
    void transTaskRun(std::shared_ptr<RelayTaskData> data);
    void fileListChanged(bool isSuccess, const std::vector<FileMeta>& fileList);

public slots:
    void onFileListChanged(bool isSuccess, const std::vector<FileMeta>& fileList);

public:
    void Quit();
    void setAskDF(AskType askType);
    std::shared_ptr<BaseAskDF> getAskDF();
    QString getCurrentPath();
    void setCurrentPath(const QString& path);

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
    void onTransForm(std::shared_ptr<RelayTaskData> data);

private:
    void uiPathSet(const QString& path);

private:
    Ui::ExplorerControl* ui;
    QTableWidget* tabWidget_;
    AskType askType_{};
    QStringList headers_{};
    std::shared_ptr<BaseAskDF> askDf_{};
    std::shared_ptr<WorkerThread<ExplorerControl>> workerThread_{};

private:
    QMutex curPathMut_;
    QString currentPath_;
    std::vector<FileMeta> currentMetaList_;
    std::vector<FileMeta> fileMetaList_;
};

#endif   // EXPLORERCONTROL_H
