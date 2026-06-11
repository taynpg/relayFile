#include "ClientHelper.h"

#include <QTimer>

DoubleLinker::DoubleLinker(QObject* parent) : QObject(parent)
{
}

DoubleLinker::~DoubleLinker()
{
}

void DoubleLinker::onSendControl(FramePtr frame)
{
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
        qWarning() << "请求远端" << controlSession_->getOtherInfo().clientId << "失败";
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
        qWarning() << "请求远端" << controlSession_->getOtherInfo().clientId << "失败";
        return;
    }

    handleResp(f);
}

void DoubleLinker::onDeliverControl(FramePtr frame)
{
    if (frame->fuuid.empty()) {
        controlSession_->handleFrame(frame);
    } else {
        onDeliverFile(frame);
    }
}

void DoubleLinker::onDeliverFile(FramePtr frame)
{
    {
        QMutexLocker locker(&fcStateLock_);
        if (fcState_ != FileControlState::FCS_Connected) {
            auto ip = controlSession_->getClientCore()->getServerIp();
            auto port = controlSession_->getClientCore()->getServerPort();
            fcState_ = FileControlState::FCS_Connecting;
            emit signalDoConnect(ip, port);
            return;
        }
    }
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
    timer->start();

    while (Run) {
        QThread::msleep(1);
        if (fileSession_->getClientCore()->isConnected()) {
            return true;
        }
    }
    {
        QMutexLocker locker(&fcStateLock_);
        return fcState_ == FileControlState::FCS_Connected;
    }
}

void DoubleLinker::onDoConnectSuccess()
{
    QMutexLocker locker(&fcStateLock_);
    fcState_ = FileControlState::FCS_Connected;
}

void DoubleLinker::onDoConnectFailed()
{
    QMutexLocker locker(&fcStateLock_);
    fcState_ = FileControlState::FCS_Unconnected;
}

void DoubleLinker::onDoConnectError()
{
    QMutexLocker locker(&fcStateLock_);
    fcState_ = FileControlState::FCS_Unconnected;
}

CmdExecutor::CmdExecutor(std::shared_ptr<DoubleLinker> doubleLinker) : doubleLinker_(doubleLinker)
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