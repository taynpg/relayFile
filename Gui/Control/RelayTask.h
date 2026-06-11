#ifndef RELAYTASK_H
#define RELAYTASK_H

#include <File/FileDir.h>
#include <Net/ClientCore.h>
#include <Net/ClientHelper.h>
#include <QDialog>
#include <QTableWidget>

#include "Base/AskDirFile/BaseAskDF.h"
#include "Base/WorkerThread.hpp"


struct FileItemData {
    QString name;
    QString path;
    RFileType type;
    std::uint64_t size{};
    QString sizeStr{};
};

struct RelayTaskData {
    QString localRoot;
    QString remoteRoot;
    QVector<FileItemData> fileList;
    bool isUpload{};
};

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
    void signalTransFail();

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
    void handleOneLine(int row);
    void onTransComplete();
    void onTransFail();

private:
    void disableControls();
    void enableControls();

private:
    bool checkRet_{false};
    std::vector<FileMeta> fileList_;
    QTableWidget* tableWidget_{};
    std::shared_ptr<RelayTaskData> data_;
    Ui::RelayTask* ui;

private:
    std::shared_ptr<BaseAskDF> askLocalDf_{};
    std::shared_ptr<BaseAskDF> askRemoteDf_{};
    std::shared_ptr<DoubleLinker> doubleLinker_{};
    std::shared_ptr<WorkerThread<RelayTask>> workerThread_{};
};

#endif   // RELAYTASK_H
