#include "OneFileTrans.h"

#include <File/FileDir.h>

#include "Protocol/Serialize.hpp"

OneFileTrans::OneFileTrans(QObject* parent) : QObject(parent)
{
    sendTimeoutTimer_ = new QTimer(this);
    sendTimeoutTimer_->setSingleShot(true);
    connect(sendTimeoutTimer_, &QTimer::timeout, this, &OneFileTrans::onSendTimeout);
}

void OneFileTrans::initSignals()
{
}

void OneFileTrans::onSendTimeout()
{
    QMutexLocker locker(&qMut_);
    if (state_ == TransStatus::Sending) {
        emit signalFailed(ownId_, "超时");
        qWarning() << "发送超时:" << QString::fromStdString(ownId_);
        state_ = TransStatus::Interrupted;
    }
}

OneFileTrans::TransMode OneFileTrans::getTransMode()
{
    return tMode_;
}

bool OneFileTrans::initTransfer(TransMode mode, const Message& msg, const std::string& targetId, const std::string& ownId,
                                const std::string& uuid)
{
    QMutexLocker locker(&qMut_);
    tMode_ = mode;
    targetId_ = targetId;
    msg_ = msg;
    totalSize_ = (tMode_ == TransMode::Send ? msg.ff.size : msg.ft.size);
    meta_ = (tMode_ == TransMode::Send ? msg.ff : msg.ft);
    transSize_ = 0;
    uuid_ = uuid;
    curBlockIndex_ = 0;
    ownId_ = ownId;
    // filePath_ = QString::fromStdString(miniPath::Join(meta_.dir, meta_.name));
    filePath_ = QString::fromStdString(meta_.fullPath);

    qDebug() << "处理文件路径：" << filePath_;

    if (state_ != TransStatus::Idle) {
        return false;
    }

    if (tMode_ == TransMode::Send) {
        sendFile_.setFileName(filePath_);
        if (!FileDir::EnsureDir(FileDir::cdUp(filePath_)) || !sendFile_.open(QIODevice::ReadOnly)) {
            qWarning() << "打开发送文件失败:" << filePath_ << sendFile_.errorString();
            return false;
        }
        state_ = TransStatus::Sending;
    } else {
        recvFile_.setFileName(filePath_);
        if (!FileDir::EnsureDir(FileDir::cdUp(filePath_)) || !recvFile_.open(QIODevice::WriteOnly)) {
            qWarning() << "打开接收文件失败:" << filePath_ << recvFile_.errorString();
            return false;
        }
        state_ = TransStatus::Receving;
    }

    return true;
}

void OneFileTrans::stopTrans()
{
    handleInterrupt(nullptr);
}

void OneFileTrans::setTargetControlId(const std::string& targetControlId)
{
    targetControlId_ = targetControlId;
}

bool OneFileTrans::nextSend()
{
    if (TransStatus::Sending != state_) {
        return false;
    }
    QByteArray buffer(blockSize_, Qt::Uninitialized);
    qint64 bytesRead = sendFile_.read(buffer.data(), blockSize_);

    if (bytesRead <= 0) {
        sendFile_.close();
        auto frame = CreateFrame(FrameType::kFileType_Request_Complete);
        frame->to = targetControlId_;
        emit signalRequestSend(frame);
        state_ = TransStatus::Finished;
        emit signalFinished(ownId_);
        return true;
    }

    buffer.resize(static_cast<int>(bytesRead));
    auto frame = CreateFrame(FrameType::kFileType_Request_Chuck);
    frame->data.assign(buffer.begin(), buffer.end());
    emit signalRequestSend(frame);
    sendTimeoutTimer_->start(defSendTimeout);
    return true;
}

bool OneFileTrans::handleAck(FramePtr frame)
{
    if (frame->index == curBlockIndex_) {
        // timer
        sendTimeoutTimer_->stop();
        transSize_ += blockSize_;
        if (transSize_ > totalSize_) {
            transSize_ = totalSize_;
        }
        curBlockIndex_++;
        if (curBlockIndex_ % 10 == 0 || transSize_ >= totalSize_) {
            emit signalProcess(transSize_, totalSize_);
        }
        nextSend();
        return true;
    }
    return false;
}

bool OneFileTrans::handleRecvChuck(FramePtr frame)
{
    if (state_ != TransStatus::Receving) {
        return false;
    }
    if (frame->index != curBlockIndex_) {
        return false;
    }
    qint64 written = recvFile_.write(frame->data.data(), static_cast<qint64>(frame->data.size()));
    if (written != static_cast<qint64>(frame->data.size())) {
        qWarning() << "写入文件失败";
    }
    transSize_ += frame->data.size();

    emit signalProcess(transSize_, totalSize_);
    auto f = CreateFrame(FrameType::kFileType_Request_Ack);
    emit signalRequestSend(f);

    curBlockIndex_++;
    return true;
}

FramePtr OneFileTrans::CreateFrame(FrameType type)
{
    auto frame = OneFrame::Create();
    frame->type = type;
    frame->to = targetId_;
    frame->from = ownId_;
    frame->fuuid = uuid_;
    frame->index = curBlockIndex_;
    frame->mark = tMode_ == TransMode::Send ? 0 : 1;

    if (static_cast<uint16_t>(type) < defDirectChuckAck) {
        Message msg;
        msg.to.clientId = targetId_;
        msg.uuid = uuid_;
        frame->data = serializeStruct(msg);
    }
    return frame;
}

bool OneFileTrans::handleFinish(FramePtr frame)
{
    if (tMode_ == TransMode::Receive) {
        if (recvFile_.isOpen()) {
            recvFile_.close();
        }
        // 查看是否需要同步权限
        auto ownMark = FileDir::GetMark();
        if (ownMark != msg_.ff.mark) {
            qInfo() << "不同系统，不同步权限，ownMark:" << ownMark << "，msgMark:" << msg_.ff.mark;
        } else {
            // 同步权限
            auto ret = FileDir::SetPermission(filePath_, msg_.ff.permission);
            qInfo() << "相同系统，同步权限，ownMark:" << ownMark << "，msgMark:" << msg_.ff.mark
                    << ", permission:" << msg_.ff.permission << ", ret:" << ret;
        }
        emit signalFinished(ownId_);
        state_ = TransStatus::Finished;
    }
    return true;
}

OneFileTrans::TransStatus OneFileTrans::getTransStatus() const
{
    return state_;
}

void OneFileTrans::onFrameReceive(FramePtr frame)
{
    // qDebug() << "收到消息:" << static_cast<int>(frame->type) << "，from:" << frame->from << "，to:" << frame->to
    //          << "，index:" << frame->index;
    if (state_ == TransStatus::Finished || state_ == TransStatus::Interrupted) {
        qWarning() << "文件传输已结束，无法处理消息, state is:" << static_cast<int>(state_);
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
        return;
    }
    switch (frame->type) {
    case FrameType::kFileType_Request_Chuck: {
        handleRecvChuck(frame);
        break;
    }
    case FrameType::kFileType_Request_Ack: {
        handleAck(frame);
        break;
    }
    case FrameType::kFileType_Request_Complete: {
        qDebug() << QString::fromStdString(frame->from) << ", kFileType_Request_Complete.";
        handleFinish(frame);
        break;
    }
    case FrameType::kFileType_Request_Cancel: {
        qDebug() << QString::fromStdString(frame->from) << ", kFileType_Request_Cancel.";
        handleInterrupt(frame);
        break;
    }
    case FrameType::kFileType_Request_Start: {
        // 如果自己是接收方，那么消息原地返回。
        if (tMode_ == TransMode::Receive) {
            std::swap(frame->from, frame->to);
            frame->mark = frame->mark == 0 ? 1 : 0;
            qDebug() << QString::fromStdString(frame->from) << ", kFileType_Request_Start.";
            emit signalRequestSend(frame);
            break;
        }
        // 如果自己是发送方，那么继续发送。
        if (tMode_ == TransMode::Send) {
            qDebug() << QString::fromStdString(frame->from) << ", nextSend.";
            nextSend();
        }
        break;
    }
    default: {
        break;
    }
    }
}

bool OneFileTrans::handleInterrupt(FramePtr frame)
{
    QMutexLocker locker(&qMut_);
    if (state_ == TransStatus::Finished) {
        return true;
    }
    if (tMode_ == TransMode::Receive && state_ == TransStatus::Receving) {
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
    }
    if (tMode_ == TransMode::Send && state_ == TransStatus::Sending) {
        auto f = CreateFrame(FrameType::kFileType_Request_Cancel);
        f->to = targetControlId_;
        emit signalRequestSend(f);
    }
    state_ = TransStatus::Interrupted;
    if (recvFile_.isOpen()) {
        qWarning() << "关闭接收文件" << filePath_;
        recvFile_.close();
    }
    if (sendFile_.isOpen()) {
        qWarning() << "关闭发送文件" << filePath_;
        sendFile_.close();
    }
    return true;
}

QString OneFileTrans::getTransName() const
{
    return filePath_;
}
