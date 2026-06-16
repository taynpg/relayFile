#pragma once

#include "OneFileTrans.h"

class FileSession : public QObject
{
    Q_OBJECT

signals:
    void signalRequestSend(FramePtr frame);

signals:
    void signalCurFileProgress(std::uint64_t transed, std::uint64_t total);
    void signalCurFile(const QString& from, const QString& to);

public:
    FileSession(QObject* parent = nullptr);
    ~FileSession();

public:
    void Quit();
    ClientCore* getClientCore();
    bool getFileMeta(const Message& msg, FileMeta& meta);
    void handleFrame(FramePtr frame);
    bool getTransStatus(OneFileTrans::TransStatus& status, const std::string& baseUUID, bool isSend);

public slots:
    void AskOwnID();
    void clearTask(const std::string& uuid, bool isSend);

private:
    void pushTask(const std::shared_ptr<OneFileTrans>& fileTrans, const Message& msg, const std::string& errMsg, FrameType type,
                  bool ret, bool needConnect);
    std::string getMapKeyUUIDByMode(const std::string& uuid, OneFileTrans::TransMode mode);
    std::string getMapKeyUUIDByMark(const std::string& uuid, int16_t mark);

private:
    QMutex transferMapLock_;
    ClientCore* clientCore_{};
    ClientWorker* clientWorker_{};
    QMap<std::string, std::shared_ptr<OneFileTrans>> transferMap_;
};
