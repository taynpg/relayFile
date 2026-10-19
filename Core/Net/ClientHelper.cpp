#include "ClientHelper.h"

#include "FileSessionIO.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>
#include <chrono>

#include "CoreDefine.hpp"
#include "File/FileDir.h"
#include "Protocol/Serialize.hpp"
#include "Utils/Common.h"
#include "Utils/TimerSTD.hpp"

DoubleLinker::DoubleLinker(QObject* parent) : QObject(parent)
{
}

DoubleLinker::~DoubleLinker()
{
}

void DoubleLinker::onSendControl(FramePtr frame)
{
    if (GIsFileMessageFrame(frame)) {
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
    if (GIsFileMessageFrame(frame)) {
        emit signalSendControl(frame);
    } else {
        emit signalSendFile(frame);
    }
}

void DoubleLinker::SetControlSession(std::shared_ptr<ControlSession> session)
{
    controlSession_ = session;
    auto* cliCore = controlSession_->getClientCore();
    // DirectConnection：在套接字线程里就完成路由，不经过 GUI 线程。
    // 否则 GUI 一旦阻塞（例如 RunTaskItem 正在等结果），对端的应答就排队排到死。
    connect(cliCore, &ClientCore::signalDeliverFrame, this, &DoubleLinker::onDeliverControl, Qt::DirectConnection);
    connect(this, &DoubleLinker::signalSendControl, cliCore, [this, cliCore](FramePtr frame) {
        if (!cliCore->Send(frame)) {
            qWarning() << "[TRACE-SID] 控制连接发送失败 sid=" << frame->sessionId
                       << "type=" << static_cast<int>(frame->type);
        } else if (frame->type != FrameType::kMsgType_Ask_Heart) {
            // qDebug() << "[TRACE-SID] sent to socket sid=" << frame->sessionId
            //          << "type=" << static_cast<int>(frame->type);
        }
    });
    connect(controlSession_.get(), &ControlSession::signalRequestSend, this, &DoubleLinker::onSendControl);
    startHeartbeat();
}

void DoubleLinker::startHeartbeat()
{
    // 关键：定时器必须活在控制连接的套接字线程里。
    // 放在 GUI 线程时，只要界面卡住超过服务器的心跳超时（15s），连接就会被踢掉，
    // 后续 Request_Complete / Cancel 之类走控制连接的帧全部发不出去。
    auto* cliCore = controlSession_->getClientCore();
    auto fileSession = fileSession_;
    QMetaObject::invokeMethod(
        cliCore,
        [cliCore, fileSession]() {
            auto* timer = new QTimer(cliCore);
            timer->setInterval(1500);
            QObject::connect(timer, &QTimer::timeout, cliCore, [cliCore, fileSession]() {
                if (!cliCore->isConnected()) {
                    return;
                }
                Message heartMsg;
                if (fileSession) {
                    heartMsg.comStr = fileSession->getClientCore()->getOwnClientInfo().clientId;
                }
                auto f = OneFrame::Create();
                f->sessionId = -1;
                f->data = serializeStruct(heartMsg);
                f->type = FrameType::kMsgType_Ask_Heart;
                cliCore->Send(f);
            });
            timer->start();
        },
        Qt::QueuedConnection);
}

void DoubleLinker::SetFileSession(std::shared_ptr<FileSession> session)
{
    fileSession_ = session;
    auto* cliCore = fileSession_->getClientCore();
    connect(cliCore, &ClientCore::signalDeliverFrame, this, &DoubleLinker::onDeliverFile, Qt::DirectConnection);
    connect(this, &DoubleLinker::signalSendFile, cliCore, [this, cliCore](FramePtr frame) {
        if (!cliCore->Send(frame)) {
            qWarning() << "文件连接发送失败, type=" << static_cast<int>(frame->type)
                       << "index=" << frame->index;
        }
    });
    // 关键：直接连传输引擎（IO 线程），不经过 FileSession 这个 GUI 线程门面。
    // 门面信号是排队投递到 GUI 线程的，一旦 GUI 在等任务结果（RunTaskItem 阻塞），
    // 数据帧发不出去、状态变更也到不了等待方，双方就互相等死。
    if (auto* io = fileSession_->io()) {
        connect(io, &FileSessionIO::signalRequestSend, this, &DoubleLinker::onSendFile, Qt::DirectConnection);
        connect(io, &FileSessionIO::signalTaskStateChanged, this, &DoubleLinker::onTaskStateChanged,
                Qt::DirectConnection);
    }
    connect(fileSession_.get(), &FileSession::signalNeedFileConnect, this, &DoubleLinker::onNeedFileConnect);
    connect(this, &DoubleLinker::signalAskFileID, fileSession_.get(), &FileSession::AskOwnID);
    connect(this, &DoubleLinker::signalFileDoConnect, cliCore, &ClientCore::connectToServer);
    connect(cliCore, &ClientCore::signalConnected, this, &DoubleLinker::onDoFileConnectSuccess);
    connect(cliCore, &ClientCore::signalDisconnected, this, &DoubleLinker::onDoFileConnectFailed);
    connect(cliCore, &ClientCore::signalErrorOccurred, this, &DoubleLinker::onDoFileConnectError);
    connect(fileSession_.get(), &FileSession::signalCurFileProgress,
            [this](std::uint64_t transed, std::uint64_t total) { emit signalCurFileProgress(transed, total); });
    connect(fileSession_.get(), &FileSession::signalCurFile, this,
            [this](const QString& from, const QString& to) { emit signalCurFileItem(from, to); });
    connect(this, &DoubleLinker::signalCancelWaitMsg, controlSession_.get(), &ControlSession::onCancelWaitMsg);
}

bool DoubleLinker::RunTask(const std::vector<std::shared_ptr<TransItem>>& tasks)
{
    for (auto& item : tasks) {
        if (!RunTaskItem(item)) {
            return false;
        }
    }
    return true;
}

bool DoubleLinker::RunTaskItem(const std::shared_ptr<TransItem>& item)
{
    Message reqMsg;
    reqMsg.uuid = Common::GetUUID().toStdString();
    reqMsg.ff = item->from;
    reqMsg.ft = item->to;
    reqMsg.from.clientId = controlSession_->getOwnInfo().clientId;
    reqMsg.to.clientId = controlSession_->getOtherInfo().clientId;
    // 归档传输：接收方收完即解包。
    reqMsg.setFlag(MessageFlag::kArchive, item->isArchive);
    if (item->isSend) {
        // 断点续传指纹：接收方拿它校验本地 .relaypart 是不是同一个源文件
        reqMsg.resumeFingerprint =
            FileDir::QuickFingerprint(QString::fromStdString(item->from.fullPath)).toStdString();
    }

    auto requestFrame = OneFrame::Create();
    requestFrame->data = serializeStruct(reqMsg);
    requestFrame->type = item->isSend ? FrameType::kFileType_Request_Send : FrameType::kFileType_Request_Down;

    TaskWait wait;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        taskWait_ = &wait;
        taskWaitUUID_ = reqMsg.uuid;
        taskWaitIsSend_ = item->isSend;
    }
    isRunTaskItem_ = true;
    curTaskIsSend_ = item->isSend;
    curTaskUUID_ = reqMsg.uuid;
    onSendControl(requestFrame);

    bool isDone = false;
    {
        std::unique_lock<std::mutex> lock(taskMutex_);
        const auto start = std::chrono::steady_clock::now();
        while (!wait.done && isRunTaskItem_) {
            taskCv_.wait_for(lock, std::chrono::milliseconds(200));
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (!wait.registered) {
                // 会话一直没建起来（对端没应答 / 握手失败）
                if (elapsed > defWaitCmdTimeout) {
                    qWarning() << "任务创建超时，对端未应答, uuid:" << QString::fromStdString(reqMsg.uuid);
                    break;
                }
            } else if (elapsed > defTaskTotalTimeout) {
                // 传输中途卡死由 OneFileTrans 自己的超时负责，这里只兜底
                qWarning() << "任务总超时, uuid:" << QString::fromStdString(reqMsg.uuid);
                break;
            }
        }
        isDone = wait.done && wait.status == OneFileTrans::TransStatus::Finished;
        taskWait_ = nullptr;
    }
    isRunTaskItem_ = false;
    return isDone;
}

void DoubleLinker::onTaskStateChanged(const QString& uuid, bool isSend, int status)
{
    // 可能在 FileSession 的 IO 线程里被调用：只碰 mutex 保护的成员
    std::lock_guard<std::mutex> lock(taskMutex_);
    if (!taskWait_) {
        return;
    }
    if (uuid.toStdString() != taskWaitUUID_ || isSend != taskWaitIsSend_) {
        return;
    }
    const auto st = static_cast<OneFileTrans::TransStatus>(status);
    if (st == OneFileTrans::TransStatus::Idle) {
        return;
    }
    taskWait_->registered = true;
    if (st == OneFileTrans::TransStatus::Finished || st == OneFileTrans::TransStatus::Interrupted) {
        taskWait_->status = st;
        taskWait_->done = true;
    }
    taskCv_.notify_all();
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
        qWarning() << "请求远端" << QString::fromStdString(controlSession_->getOtherInfo().clientId) << "超时未响应。";
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
        qWarning() << "请求远端" << QString::fromStdString(controlSession_->getOtherInfo().clientId) << "超时未响应。";
        return;
    }

    handleResp(f);
}

void DoubleLinker::onDeliverControl(FramePtr frame)
{
    // 这里是套接字线程（DirectConnection）。只做投递：
    //   控制帧 → ControlSession（GUI 线程处理，涉及本地文件系统操作）
    //   文件帧 → FileSession 的传输 IO 线程
    if (GIsControlMessageFrame(frame)) {
        auto* cs = controlSession_.get();
        if (cs) {
            QMetaObject::invokeMethod(cs, [cs, frame]() { cs->handleFrame(frame); }, Qt::QueuedConnection);
        }
    } else if (fileSession_) {
        fileSession_->handleFrame(frame);
    }
}

void DoubleLinker::onCancelWaitMsg()
{
    emit signalCancelWaitMsg();
}

void DoubleLinker::onDeliverFile(FramePtr frame)
{
    // 套接字线程：连接是否就绪由 FileSession 的 IO 线程自己确认，
    // 不能在这里阻塞等待（会卡住整条套接字的收包）。
    if (fileSession_) {
        fileSession_->handleFrame(frame);
    }
}

void DoubleLinker::onNeedFileConnect()
{
    if (!controlSession_) {
        return;
    }
    auto* cli = controlSession_->getClientCore();
    emit signalFileDoConnect(cli->getServerIp(), cli->getServerPort());
}

bool DoubleLinker::waitFileConnect()
{
    auto* cli = fileSession_->getClientCore();

    {
        QMutexLocker locker(&fcStateLock_);
        auto connected = fcState_ == FileControlState::FCS_Connected;
        auto getIded = !cli->getOwnClientInfo().clientId.empty();
        if (connected && getIded) {
            return true;
        }
    }

    auto ip = controlSession_->getClientCore()->getServerIp();
    auto port = controlSession_->getClientCore()->getServerPort();

    // 让传输 IO 线程自己也知道端点：它可能需要在收帧时补一次连接
    fileSession_->setServerEndpoint(ip, port);

    {
        QMutexLocker locker(&fcStateLock_);
        fcState_ = FileControlState::FCS_Connecting;
    }
    emit signalFileDoConnect(ip, port);

    // 不再 processEvents 忙等：连接、AskOwnID、收 ID 全部由各自线程的事件循环完成，
    // 这里只等待一个跨线程可见的状态（isConnected 看 atomic、clientId 加锁拷贝）。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cli->isConnected() && !cli->getOwnClientInfo().clientId.empty()) {
            return true;
        }
        QThread::msleep(10);
    }
    {
        QMutexLocker locker(&fcStateLock_);
        auto connected = fcState_ == FileControlState::FCS_Connected;
        auto getIded = !cli->getOwnClientInfo().clientId.empty();
        return connected && getIded;
    }
}

void DoubleLinker::clearCurrentTaskItem()
{
    fileSession_->clearTask(curTaskUUID_, curTaskIsSend_);
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        isRunTaskItem_ = false;
        taskCv_.notify_all();
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
