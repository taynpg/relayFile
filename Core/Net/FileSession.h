#pragma once

#include <QMutex>
#include <QObject>
#include <QTimer>
#include <fstream>
#include <memory>
#include <string>

#include "ClientCore.h"
#include "Protocol/FileMeta.h"
#include "Protocol/Protocol.h"

class OneFileTrans : public QObject
{
    Q_OBJECT

signals:
    void signalProcess(std::uint64_t transed, std::uint64_t total);
    void signalFinished(const std::string& transId);
    void signalFailed(const std::string& transId, const std::string& errMsg);
    void signalInterrupt(const std::string& transId);
    void signalRequestSend(FramePtr frame);

public:
    enum class TransStatus {
        Idle = 0,
        WaitingAccept,
        Sending,
        Receving,
        Finished,
        Interrupted
    };

    enum class TransMode {
        Send,
        Receive
    };

public slots:
    void onFrameReceive(FramePtr frame);

public:
    OneFileTrans(QObject* parent = nullptr);

    bool initTransfer(TransMode mode, const FileMeta& fileMeta, const std::string& targetId, ClientCore* fileControl);
    void initSignals();

    bool nextSend();
    bool handleStart(FramePtr frame);
    bool handleAck(FramePtr frame);
    bool handleChuck(FramePtr frame);
    bool handleInterrupt(FramePtr frame);
    bool handleFinish(FramePtr frame);

    void onSendTimeout();

    FramePtr CreateControlFrame(MessageType type);
    FramePtr CreateFileFrame(FrameType type);

private:
    TransMode tMode_{};
    QMutex qMut_;
    QTimer* sendTimeoutTimer_{};
    TransStatus state_{};
    ClientCore* fileControl_{};

    FileMeta meta_;

    std::ofstream recvFile_;
    std::ifstream sendFile_;
    std::string filePath_;
    std::string targetId_;
    std::string ownId_;

    std::uint64_t totalSize_{};
    std::uint64_t transSize_{};
    std::uint64_t curBlockIndex_{};
    std::uint32_t blockSize_{1024 * 64};
};

class FileSession : public ClientCore
{
    Q_OBJECT
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

private:
    void AskOwnID();

private:
    ClientCore* clientCore_{};
    ClientWorker* clientWorker_{};
    QMutex transferMapLock_;
    QMap<QString, std::shared_ptr<OneFileTrans>> transferMap_;
};
