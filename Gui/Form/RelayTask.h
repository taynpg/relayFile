#ifndef RELAYTASK_H
#define RELAYTASK_H

#include <File/FileDir.h>
#include <Net/ClientCore.h>
#include <Net/ClientHelper.h>
#include <QDialog>
#include <QTableWidget>

#include "Base/AskDirFile/BaseAskDF.h"
#include "Base/GuiDefine.h"
#include "Base/WorkerThread.hpp"

namespace Ui {
class RelayTask;
}

class RelayTask : public QDialog
{
    Q_OBJECT

public:
    explicit RelayTask(QWidget* parent = nullptr);
    ~RelayTask();

signals:
    void signalLog(const QString& log);
    void signalCheckComplete();
    void signalCheckUnComplete();
    void signalUpdateTable();
    void signalTransComplete();
    void signalTransing();
    void signalTransFail();
    void signalNeedConfirmFiles();
    void signalCancelWaitMsg();

public:
    void Quit();
    void setData(std::shared_ptr<RelayTaskData> data);
    void closeEvent(QCloseEvent* event) override;
    template <typename HandleResp> bool Request(ClientCore* cli, FramePtr frame, HandleResp handleResp);

protected:
    void initControl();
    void initSignals();
    void baseTask();
    void showEvent(QShowEvent* event) override;

    void onBaseCheck();
    void onCheckComplete();
    void onCheckUnComplete();

    void onAppendLog(const QString& log);
    void updateTable();
    void setFileItem(const FileMeta& meta, int row, int index);
    void onStartRun();
    bool handleOneLine(int row);
    void onTransComplete();
    void onTransFail();
    void onTransing();

    void onCurFileProgress(std::uint64_t transed, std::uint64_t total);
    void onCurFileItem(const QString& from, const QString& to);
    void onRefreshSpeed();
    void onSuccessFresh(int row);
    void onFailFresh(int row);
    void onStartFresh(int row);
    void onConfirmFiles();

    bool normalCheckFileExist();
    void GenOtherMetaPath(const FileMeta& in, FileMeta& out, bool isSend, const QString& localRoot, const QString& remoteRoot);

private:
    void disableControls();
    void enableControls();
    void clearData();
    QString getSpeedStr(uint64_t transed);

private:
    // 标准库计时开始点
    std::chrono::steady_clock::time_point startTime_;

    uint64_t preTransed_{0};
    uint64_t totalSize_{0};
    uint64_t curTransed_{0};

    bool checkRet_{false};
    std::vector<FileMeta> fileList_;
    QTableWidget* tableWidget_{};
    QTimer* speedTimer_{};
    std::shared_ptr<RelayTaskData> data_;
    Ui::RelayTask* ui;

private:
    std::vector<FileMeta> needConfirmFiles_;
    std::vector<FileMeta> needRemoveTaskFiles_;

    std::map<QString, int> curTableData_;
    std::shared_ptr<BaseAskDF> askLocalDf_{};
    std::shared_ptr<BaseAskDF> askRemoteDf_{};
    RelayTaskStatus status_{RelayTaskStatus::Init};
    std::shared_ptr<DoubleLinker> doubleLinker_{};
    std::vector<std::shared_ptr<TransItem>> transItems_;
    std::shared_ptr<WorkerThread<RelayTask>> workerThread_{};
};

#endif   // RELAYTASK_H
