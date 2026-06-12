#pragma once

#include "OneFileTrans.h"

class FileSession : public ClientCore
{
    Q_OBJECT

signals:
    void signalRequestSend(FramePtr frame);

public:
    FileSession(QObject* parent = nullptr);
    ~FileSession() override;

    bool startFileTransfer(const FileMeta& fileMeta, const std::string& targetId);
    bool acceptFileTransfer(const std::string& transferId, const std::string& savePath);
    bool rejectFileTransfer(const std::string& transferId);

public:
    void Quit();
    ClientCore* getClientCore();
    bool getFileMeta(const Message& msg, FileMeta& meta);
    void handleFrame(FramePtr frame);

public slots:
    void AskOwnID();

private:
    void pushTask(const std::shared_ptr<OneFileTrans>& fileTrans, const Message& msg, const std::string& errMsg, FrameType type, bool ret);

private:
    ClientCore* clientCore_{};
    ClientWorker* clientWorker_{};
    QMutex transferMapLock_;
    QMap<std::string, std::shared_ptr<OneFileTrans>> transferMap_;
};
