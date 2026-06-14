#pragma once

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <fstream>
#include <memory>

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

    bool initTransfer(TransMode mode, const FileMeta& fileMeta, const std::string& targetId, const std::string& ownId,
                      const std::string& uuid);
    void initSignals();

    void stopTrans();
    bool nextSend();
    bool handleAck(FramePtr frame);
    bool handleRecvChuck(FramePtr frame);
    bool handleInterrupt(FramePtr frame);
    bool handleFinish(FramePtr frame);
    TransMode getTransMode();
    TransStatus getTransStatus() const;
    QString getTransName() const;

    void onSendTimeout();
    void setTargetControlId(const std::string& targetControlId);
    FramePtr CreateFrame(FrameType type);

private:
    TransMode tMode_{};
    QMutex qMut_;
    QTimer* sendTimeoutTimer_{};
    TransStatus state_{};

    FileMeta meta_;
    QString filePath_;
    std::string uuid_;
    std::string targetId_;
    std::string targetControlId_;
    std::string ownId_;

    QFile sendFile_;
    QFile recvFile_;

    bool normalTrans_{true};
    std::uint64_t totalSize_{};
    std::uint64_t transSize_{};
    std::uint64_t curBlockIndex_{};
    std::uint32_t blockSize_{1024 * 64};
};