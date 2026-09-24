#ifndef RELAYTASK_H
#define RELAYTASK_H

#include <File/FileDir.h>
#include <Net/ClientCore.h>
#include <Net/ClientHelper.h>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
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
    void signalAutoStart();

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
    void renderRow(int viewRow, int id);
    void renderPage();
    void gotoPage(int page);
    void updatePageLabel();
    void refreshVisibleCell(int id);
    void onStartRun();
    bool handleOneLine(int id);
    void onTransComplete();
    void onTransFail();
    void onTransing();

    void onCurFileProgress(std::uint64_t transed, std::uint64_t total);
    void onCurFileItem(const QString& from, const QString& to);
    void onRefreshSpeed();
    void onSuccessFresh(int id);
    void onFailFresh(int id);
    void onStartFresh(int id);
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
    // 覆盖确认项：目标端已存在文件 + 内容粗判结果（true=粗判一致）
    struct ConfirmFileInfo {
        FileMeta file;
        bool contentSame{};
    };
    std::vector<ConfirmFileInfo> needConfirmFiles_;
    std::vector<FileMeta> needRemoveTaskFiles_;

    std::shared_ptr<BaseAskDF> askLocalDf_{};
    std::shared_ptr<BaseAskDF> askRemoteDf_{};
    RelayTaskStatus status_{RelayTaskStatus::Init};
    std::shared_ptr<DoubleLinker> doubleLinker_{};

    std::vector<std::shared_ptr<TransItem>> transItems_;
    std::shared_ptr<WorkerThread<RelayTask>> workerThread_{};

    // ---- 分页显示相关 ----
    // 每行显示状态（与 fileList_ 一一对应，按数据索引寻址，与当前可见页解耦）
    struct RowDisplay {
        QString status{GUI_FILE_TRAN_STATE_WAIT};
        QString speedStr{"N/A"};
        QString useTimeStr{"N/A"};
    };
    std::vector<RowDisplay> rowDisplay_;
    int curPage_{0};
    int pageSize_{100};

    // 分页导航控件
    QPushButton* btnFirst_{};
    QPushButton* btnPrev_{};
    QPushButton* btnNext_{};
    QPushButton* btnLast_{};
    QLabel* pageLabel_{};
    QComboBox* pageSizeCombo_{};

    void onFirstPage();
    void onPrevPage();
    void onNextPage();
    void onLastPage();
    void onPageSizeChanged();
};

#endif   // RELAYTASK_H
