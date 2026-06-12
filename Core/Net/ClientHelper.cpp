#include "ClientHelper.h"

#include <QTimer>
#include "Protocol/Serialize.hpp"

DoubleLinker::DoubleLinker(QObject* parent) : QObject(parent)
{
}

DoubleLinker::~DoubleLinker()
{
}

void DoubleLinker::onSendControl(FramePtr frame)
{
    if (GIsTurnFrame(frame)) {
        // 如果是控制消息，那么就添上自己控制端的ID.
        Message sourceMsg;
        deserializeStruct(frame->data, sourceMsg);
        sourceMsg.transId = fileSession_->getClientCore()->getOwnClientInfo().clientId;
        frame->data = serializeStruct(sourceMsg);
    }
    emit signalSendControl(frame);
}

void DoubleLinker::onSendFile(FramePtr frame)
{
    if (GIsTurnFrame(frame)) {
        emit signalSendControl(frame);
    } else {
        emit signalSendFile(frame);
    }
}

void DoubleLinker::SetControlSession(std::shared_ptr<ControlSession> session)
{
    controlSession_ = session;
    auto* cliCore = controlSession_->getClientCore();
    connect(cliCore, &ClientCore::signalDeliverFrame, this, &DoubleLinker::onDeliverControl);
    connect(this, &DoubleLinker::signalSendControl, cliCore, [this, cliCore](FramePtr frame) { cliCore->Send(frame); });
    connect(controlSession_.get(), &ControlSession::signalRequestSend, this, &DoubleLinker::onSendControl);
}

void DoubleLinker::SetFileSession(std::shared_ptr<FileSession> session)
{
    fileSession_ = session;
    auto* cliCore = fileSession_->getClientCore();
    connect(cliCore, &ClientCore::signalDeliverFrame, this, &DoubleLinker::onDeliverFile);
    connect(this, &DoubleLinker::signalSendFile, cliCore, [this, cliCore](FramePtr frame) { cliCore->Send(frame); });
    connect(fileSession_.get(), &FileSession::signalRequestSend, this, &DoubleLinker::onSendFile);
    connect(this, &DoubleLinker::signalAskFileID, fileSession_.get(), &FileSession::AskOwnID);
    connect(this, &DoubleLinker::signalFileDoConnect, cliCore, &ClientCore::connectToServer);
    connect(cliCore, &ClientCore::signalConnected, this, &DoubleLinker::onDoFileConnectSuccess);
    connect(cliCore, &ClientCore::signalDisconnected, this, &DoubleLinker::onDoFileConnectFailed);
    connect(cliCore, &ClientCore::signalErrorOccurred, this, &DoubleLinker::onDoFileConnectError);
}

std::shared_ptr<ControlSession> DoubleLinker::GetControlSession() const
{
    return controlSession_;
}

std::shared_ptr<FileSession> DoubleLinker::GetFileSession() const
{
    return fileSession_;
}

void DoubleLinker::Quit()
{
    controlSession_->Quit();
    fileSession_->Quit();
}

template <typename HandleResp> bool DoubleLinker::bRequest(FramePtr frame, HandleResp handleResp)
{
    auto promise = std::make_shared<std::promise<FramePtr>>();
    auto future = promise->get_future();

    controlSession_->SendWithCall(frame, [promise](FramePtr f) { promise->set_value(f); });

    FramePtr f = future.get();
    if (!f) {
        qWarning() << "请求远端" << controlSession_->getOtherInfo().clientId << "超时未响应。";
        return false;
    }

    return handleResp(f);
}

template <typename HandleResp> void DoubleLinker::vRequest(FramePtr frame, HandleResp handleResp)
{
    auto promise = std::make_shared<std::promise<FramePtr>>();
    auto future = promise->get_future();

    controlSession_->SendWithCall(frame, [promise](FramePtr f) { promise->set_value(f); });

    FramePtr f = future.get();
    if (!f) {
        qWarning() << "请求远端" << controlSession_->getOtherInfo().clientId << "超时未响应。";
        return;
    }

    handleResp(f);
}

void DoubleLinker::onDeliverControl(FramePtr frame)
{
    if (GIsMsgFrame(frame)) {
        controlSession_->handleFrame(frame);
    } else {
        onDeliverFile(frame);
    }
}

void DoubleLinker::onDeliverFile(FramePtr frame)
{
    // 暂时先简单等待吧
    if (!waitFileConnect()) {
        // 告诉对方取消。
        return;
    }
    fileSession_->handleFrame(frame);
}

bool DoubleLinker::waitFileConnect()
{
    {
        QMutexLocker locker(&fcStateLock_);
        if (fcState_ == FileControlState::FCS_Connected) {
            return true;
        }
    }

    QTimer* timer = new QTimer();

    bool Run = true;
    connect(timer, &QTimer::timeout, this, [this, &Run, timer]() {
        Run = false;
        timer->deleteLater();
    });

    timer->setSingleShot(true);
    timer->setInterval(5000);

    auto ip = controlSession_->getClientCore()->getServerIp();
    auto port = controlSession_->getClientCore()->getServerPort();
    emit signalFileDoConnect(ip, port);

    {
        QMutexLocker locker(&fcStateLock_);
        fcState_ = FileControlState::FCS_Connecting;
    }

    timer->start();

    while (Run) {
        QThread::msleep(1);
        if (fileSession_->getClientCore()->isConnected()) {
            return true;
        }
    }

    timer->stop();
    {
        QMutexLocker locker(&fcStateLock_);
        return fcState_ == FileControlState::FCS_Connected;
    }
}

void DoubleLinker::onDoFileConnectSuccess()
{
    QMutexLocker locker(&fcStateLock_);
    fcState_ = FileControlState::FCS_Connected;
    emit signalAskFileID();
}

void DoubleLinker::onDoFileConnectFailed()
{
    QMutexLocker locker(&fcStateLock_);
    fcState_ = FileControlState::FCS_Unconnected;
}

void DoubleLinker::onDoFileConnectError()
{
    QMutexLocker locker(&fcStateLock_);
    fcState_ = FileControlState::FCS_Unconnected;
}

CmdExecutor::CmdExecutor(QObject* parent, std::shared_ptr<DoubleLinker> doubleLinker)
    : QObject(parent), doubleLinker_(doubleLinker)
{
}

void CmdExecutor::AddStep(Step step)
{
    steps_.push_back(std::move(step));
}

bool CmdExecutor::Execute(FramePtr startFrame)
{
    currentStep_ = 0;
    ExecuteStep(startFrame);
    return execResult_ == ExecResult::Success;
}

void CmdExecutor::ExecuteStep(FramePtr frame)
{
    doubleLinker_->vRequest(frame, [this](FramePtr f) {
        if (!f) {
            execResult_ = ExecResult::Failed;
            return;
        }
        if (currentStep_ >= steps_.size()) {
            execResult_ = ExecResult::Success;
            return;
        }
        auto nf = steps_[currentStep_++](f);
        if (!nf && currentStep_ < steps_.size()) {
            execResult_ = ExecResult::Failed;
            return;
        }
        if (!nf && currentStep_ == steps_.size()) {
            execResult_ = ExecResult::Success;
            return;
        }
        ExecuteStep(nf);
    });
}

void CmdExecutor::Reset()
{
    steps_.clear();
    currentStep_ = 0;
    execResult_ = ExecResult::Running;
}