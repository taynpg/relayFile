#include "ControlSession.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <chrono>

#include "Compress/TarXzPacker.h"
#include "CoreDefine.hpp"
#include "File/LocalHandle.h"
#include "Protocol/Message.h"
#include "Protocol/Serialize.hpp"

// [TRACE-SID] 安全提取帧内 comStr（路径等），解码失败不抛异常，避免影响主流程
static QString traceSafeComStr(const FramePtr& frame)
{
    try {
        Message m;
        deserializeStruct(frame->data, m);
        return QString::fromStdString(m.comStr);
    } catch (...) {
        return QStringLiteral("<decode fail>");
    }
}

ControlSession::ControlSession(QObject* parent)
    : QObject(parent), workerPool_(std::make_shared<ThreadPool>(3)), timerPoolStd_(std::make_shared<TimerPoolStd>(3))
{
    clearWorkerTimer_ = new QTimer(this);
    clientCore_ = new ClientCore();
    clientCore_->setIsControl(true);
    clientWorker_ = new ClientWorker(clientCore_, nullptr);
    clientCore_->moveToThread(clientWorker_);
    initSignals();
    clientWorker_->start();
}

ClientCore* ControlSession::getClientCore()
{
    return clientCore_;
}

ControlSession::~ControlSession()
{
    Quit();
    timerPoolStd_->stop();
    if (clientWorker_) {
        delete clientWorker_;
        clientWorker_ = nullptr;
    }
}

void ControlSession::Quit()
{
    if (quit_.exchange(true)) {
        return;
    }
    qDebug() << "等待线程池完成...";
    workerPool_->Quit();
    qDebug() << "线程池完成";

    clearWorkerTimer_->stop();
    if (!clientCore_) {
        return;
    }
    // 与 FileSession 同理：ClientCore/QTcpSocket 属于 worker 线程，必须在线程内销毁
    if (clientWorker_->isRunning() && QThread::currentThread() != clientWorker_) {
        QMetaObject::invokeMethod(
            clientCore_,
            [this]() {
                delete clientCore_;
                clientCore_ = nullptr;
            },
            Qt::BlockingQueuedConnection);
        clientWorker_->quit();
        clientWorker_->wait();
    } else {
        delete clientCore_;
        clientCore_ = nullptr;
        if (clientWorker_->isRunning()) {
            clientWorker_->quit();
            clientWorker_->wait();
        }
    }
}

void ControlSession::initSignals()
{
    connect(clearWorkerTimer_, &QTimer::timeout, this, &ControlSession::clearWorker);
    clearWorkerTimer_->start(defClearWorkerTimeout);
}

template <typename Callback> bool ControlSession::SendCall(FramePtr frame, Callback callback)
{
    auto sid = clientCore_->GetSessionId();
    frame->sessionId = sid;

    emit signalRequestSend(frame);
    // qDebug() << "ControlSession::SendCall [TRACE-SID] sid=" << sid << "type=" << static_cast<int>(frame->type)
    //          << "to=" << QString::fromStdString(frame->to) << "path=" << traceSafeComStr(frame);

    // 这里不能用QTimer，因为QTimer依赖事件循环，而阻塞需求常常会阻塞事件循环，导致定时器失效
    auto timeoutHandler = [this, sid]() {
        QMetaObject::invokeMethod(
            this,
            [this, sid]() {
                std::shared_ptr<WaiteFrame> waiter;
                {
                    QMutexLocker locker(&requestWaitLock_);
                    auto it = requestWaitFrame_.find(sid);
                    if (it == requestWaitFrame_.end()) {
                        return;
                    }
                    waiter = it.value();
                    requestWaitFrame_.erase(it);
                }
                if (!waiter) {
                    return;
                }
                qDebug() << "ControlSession::SendCall timeout [TRACE-SID] sid=" << sid << "type=" << waiter->ftype
                         << "to=" << QString::fromStdString(waiter->target)
                         << "path=" << QString::fromStdString(waiter->path);
                timerPoolStd_->stop(waiter->timerId);
                // 回调放到锁外执行：回调可能再次进入 ControlSession，持锁调用会死锁
                if (waiter->done.exchange(true)) {
                    return;
                }
                switch (waiter->callType) {
                case CallType::CT_Message: {
                    MessagePtr msg = nullptr;
                    waiter->call(msg);
                    break;
                }
                case CallType::CT_Frame: {
                    FramePtr f = nullptr;
                    waiter->call(f);
                    break;
                }
                default:
                    break;
                }
            },
            Qt::QueuedConnection);
    };

    auto waiter = std::make_shared<WaiteFrame>();
    waiter->sessionId = sid;
    waiter->ftype = static_cast<int>(frame->type);
    waiter->target = frame->to;
    waiter->path = traceSafeComStr(frame).toStdString();
    using ArgType = typename function_traits<Callback>::argument_type;
    waiter->call = [cb = std::move(callback)](std::any arg) { cb(std::any_cast<ArgType>(arg)); };

    if constexpr (std::is_same_v<ArgType, MessagePtr>) {
        waiter->callType = CallType::CT_Message;
    } else if constexpr (std::is_same_v<ArgType, FramePtr>) {
        waiter->callType = CallType::CT_Frame;
    } else {
        static_assert(!sizeof(ArgType), "Unsupported callback argument type");
    }

    {
        QMutexLocker locker(&requestWaitLock_);
        requestWaitFrame_[sid] = waiter;
    }

    waiter->timerId = timerPoolStd_->start_once(std::chrono::milliseconds(defWaitCmdTimeout), timeoutHandler);
    return true;
}

void ControlSession::onCancelWaitMsg()
{
    // 先把所有等待者移出容器，再在锁外逐个回调。
    // 原实现在持锁状态下调用用户回调（可重入），并且 erase(it) 之后继续 it++，
    // 是对失效迭代器自增（UB）。
    QList<std::shared_ptr<WaiteFrame>> waiters;
    {
        QMutexLocker locker(&requestWaitLock_);
        for (auto it = requestWaitFrame_.begin(); it != requestWaitFrame_.end(); ++it) {
            if (it.value()) {
                waiters.push_back(it.value());
            }
        }
        requestWaitFrame_.clear();
    }
    for (auto& waiter : waiters) {
        timerPoolStd_->stop(waiter->timerId);
        if (waiter->done.exchange(true)) {
            continue;
        }
        switch (waiter->callType) {
        case CallType::CT_Message: {
            MessagePtr msg = nullptr;
            waiter->call(msg);
            break;
        }
        case CallType::CT_Frame: {
            FramePtr f = nullptr;
            waiter->call(f);
            break;
        }
        default:
            break;
        }
    }
}

ClientInfo ControlSession::getOtherInfo()
{
    return clientCore_->oInfo_;
}

ClientInfo ControlSession::getOwnInfo()
{
    return clientCore_->mInfo_;
}

void ControlSession::dispatchMessage(FramePtr frame, FrameType answerType,
                                     std::function<void(const Message& sourceMsg, Message& ansMsg)> handler)
{
    auto worker = ControlSession::TaskWorker::CreateWorker(frame);
    {
        QMutexLocker locker(&responseWaitLock_);
        if (!responseWaitWorker_.contains(worker->frame->sessionId)) {
            responseWaitWorker_.insert(worker->frame->sessionId, worker);
        } else {
            // qWarning() << "[TRACE-SID] DEDUP DROP sid=" << worker->frame->sessionId
            //            << "type=" << static_cast<int>(worker->frame->type)
            //            << "from=" << QString::fromStdString(worker->frame->from)
            //            << "path=" << traceSafeComStr(worker->frame) << "(request silently dropped!)";
            return;
        }
    }
    // qDebug() << "[TRACE-SID] dispatch enqueue sid=" << frame->sessionId
    //          << "type=" << static_cast<int>(frame->type) << "path=" << traceSafeComStr(frame);
    workerPool_->enqueue([this, worker, answerType, handler]() {
        // qDebug() << "[TRACE-SID] task BEGIN sid=" << worker->sessionId;
        auto t0 = std::chrono::steady_clock::now();
        Message sourceMsg;
        deserializeStruct(worker->frame->data, sourceMsg);
        auto answerFrame = OneFrame::Create(worker->frame);
        Message m(sourceMsg);

        handler(sourceMsg, m);

        auto costMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        answerFrame->data = serializeStruct(m);
        answerFrame->type = answerType;
        // qDebug() << "[TRACE-SID] task HANDLER DONE sid=" << worker->sessionId << "cost=" << costMs
        //          << "ms, answer type=" << static_cast<int>(answerType) << "bytes=" << answerFrame->data.size();
        emit signalRequestSend(answerFrame);
        worker->isDone = true;
    });
}

void ControlSession::handleFrame(FramePtr frame)
{
    // qDebug() << "[TRACE-SID] recv frame sid=" << frame->sessionId << "type=" << static_cast<int>(frame->type)
    //          << "from=" << QString::fromStdString(frame->from) << "to=" << QString::fromStdString(frame->to);
    MessagePtr answerMsg = Message::Create();
    try {
        deserializeStruct(frame->data, *answerMsg);
    } catch (const std::exception& e) {
        // 任何非 Message 的帧都会走到这里；不能让异常穿透 Qt 事件循环
        qWarning() << "解析控制帧失败（已丢弃）, type=" << static_cast<int>(frame->type) << "err:" << e.what();
        return;
    }

    switch (frame->type) {
    case FrameType::kMsgType_Answer_ID: {
        ClientInfo info;
        info.clientId = answerMsg->to.clientId;
        info.clientName = answerMsg->to.clientName;
        info.uuid = answerMsg->comStr;
        clientCore_->onRecordOwnInfo(info);
        qDebug() << "本机信息：" << QString::fromStdString(info.clientId) << ", " << QString::fromStdString(info.clientName);
        emit signalOwnInfo(info);
        break;
    }
    case FrameType::kMsgType_Ask_Home: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Home,
                        [](const Message& sourceMsg, Message& ansMsg) { LocalHandle::AskHome(ansMsg.comStr); });
        break;
    }
    case FrameType::kMsgType_Ask_FileList: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_FileList, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.mapData[""] = std::vector<FileMeta>();
            auto& stdMeta = ansMsg.mapData[""];
            LocalHandle::AskFileList(sourceMsg.comStr, stdMeta, sourceMsg.hasFlag(MessageFlag::kRecursiveList));
        });
        break;
    }
    case FrameType::kMsgType_Ask_FileMeta: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_FileMeta, [](const Message& sourceMsg, Message& ansMsg) {
            if (!sourceMsg.strVec.empty()) {
                // 批量：一次请求查一批路径，结果按请求顺序返回；
                // 单条查不到时只是 exist=0，不影响其余条目
                ansMsg.mapData.clear();
                auto& metas = ansMsg.mapData[""];
                metas.reserve(sourceMsg.strVec.size());
                for (const auto& path : sourceMsg.strVec) {
                    FileMeta meta;
                    LocalHandle::AskFileMeta(path, meta);
                    if (meta.exist == 0 && meta.fullPath.empty()) {
                        meta.fullPath = path;
                    }
                    metas.push_back(meta);
                }
                return;
            }
            LocalHandle::AskFileMeta(sourceMsg.comStr, ansMsg.ff);
        });
        break;
    }
    case FrameType::kMsgType_Ask_FileSamples: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_FileSamples, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.samples.clear();
            ansMsg.sampleMap.clear();
            if (!sourceMsg.strVec.empty()) {
                for (const auto& path : sourceMsg.strVec) {
                    LocalHandle::AskFileSamples(path, ansMsg.sampleMap[path]);
                }
                return;
            }
            LocalHandle::AskFileSamples(sourceMsg.comStr, ansMsg.samples);
        });
        break;
    }
    case FrameType::kMsgType_Ask_ArchivePack: {
        qInfo() << "收到 Ask_ArchivePack 请求，strVec 大小:" << answerMsg->strVec.size();
        // 压缩下载：请求方要求将多个文件打包为单个 tar.xz 归档。
        // 立即回 Answer（"开始打包"），实际打包丢到线程池，完成后再发 Ask_ArchiveReady。
        Message packingAns(*answerMsg);
        std::swap(packingAns.from, packingAns.to);
        packingAns.msgStateCode = MessageStateCode::kMessageStateCodeSuccess;
        packingAns.comStr = "packing";
        auto packingFrame = OneFrame::Create(frame);
        packingFrame->data = serializeStruct(packingAns);
        packingFrame->type = FrameType::kMsgType_Answer_ArchivePack;
        emit signalRequestSend(packingFrame);

        // 异步打包
        auto sessionId = frame->sessionId;
        auto from = frame->from;
        auto to = frame->to;
        workerPool_->enqueue([this, frame, sessionId, from, to]() {
            Message sourceMsg;
            deserializeStruct(frame->data, sourceMsg);

            std::vector<PackItem> packItems;
            packItems.reserve(sourceMsg.strVec.size() / 2);
            for (size_t i = 0; i + 1 < sourceMsg.strVec.size(); i += 2) {
                PackItem pi;
                pi.srcPath = sourceMsg.strVec[i];
                pi.destPath = sourceMsg.strVec[i + 1];
                packItems.push_back(std::move(pi));
            }
            QString archivePath =
                QDir::tempPath() + QString("/relay_archive_%1.tar.xz").arg(QString::fromStdString(sourceMsg.uuid));
            std::string err;
            std::uint64_t archiveSize = 0;
            qInfo() << "压缩下载：开始打包" << packItems.size() << "个文件";
            bool ok = TarXzPacker::pack(packItems, archivePath.toStdString(), archiveSize, err);
            std::uint64_t threshold = 0;
            try {
                threshold = std::stoull(sourceMsg.comStr);
            } catch (...) {
                threshold = 0;
            }
            if (ok && threshold > 0 && archiveSize > threshold) {
                qWarning() << "压缩下载：归档大小" << archiveSize << "超过阈值" << threshold;
                QFile::remove(archivePath);
                ok = false;
                err = "归档大小超过阈值，已放弃传输";
            }

            // 打包结果通过 Ask_ArchiveReady 通知请求方。
            Message readyMsg(sourceMsg);
            std::swap(readyMsg.from, readyMsg.to);
            if (ok) {
                qInfo() << "压缩下载：打包完成，大小" << archiveSize;
                readyMsg.msgStateCode = MessageStateCode::kMessageStateCodeSuccess;
                readyMsg.ff.fullPath = archivePath.toStdString();
                readyMsg.ff.name = "relay_archive.tar.xz";
                readyMsg.ff.size = archiveSize;
                readyMsg.ff.exist = 1;
                readyMsg.ff.dir = QDir::tempPath().toStdString();
            } else {
                qWarning() << "压缩下载：打包失败" << QString::fromStdString(err);
                readyMsg.msgStateCode = MessageStateCode::kMessageStateCodeFailed;
                readyMsg.errMsg = err;
            }
            auto readyFrame = OneFrame::Create();
            readyFrame->sessionId = sessionId;
            readyFrame->from = to;
            readyFrame->to = from;
            readyFrame->data = serializeStruct(readyMsg);
            readyFrame->type = FrameType::kMsgType_Ask_ArchiveReady;
            readyFrame->fuuid = sourceMsg.uuid;
            emit signalRequestSend(readyFrame);

            // 若请求方超时未下载，自动清理归档。
            if (ok) {
                timerPoolStd_->start_once(std::chrono::seconds(120), [this, archivePath]() {
                    if (QFile::exists(archivePath)) {
                        qInfo() << "压缩下载：归档超时未下载，清理:" << archivePath;
                        QFile::remove(archivePath);
                    }
                });
            }
        });
        break;
    }
    case FrameType::kMsgType_Ask_ArchiveReady: {
        // 打包方通知请求方归档已就绪。请求方在此回 Answer，业务逻辑由 pubCall 处理。
        Message readyAns(*answerMsg);
        std::swap(readyAns.from, readyAns.to);
        readyAns.msgStateCode = MessageStateCode::kMessageStateCodeSuccess;
        auto readyAnsFrame = OneFrame::Create(frame);
        readyAnsFrame->data = serializeStruct(readyAns);
        readyAnsFrame->type = FrameType::kMsgType_Answer_ArchiveReady;
        emit signalRequestSend(readyAnsFrame);

        // 交给注册的 pubCall 处理（发起普通下载等）。回调在锁外执行，避免重入死锁。
        std::function<void(FramePtr frame)> readyCall;
        {
            QMutexLocker locker(&pubCallLock_);
            auto it = pubCall_.find(frame->type);
            if (it != pubCall_.end() && it.value()) {
                readyCall = it.value();
            }
        }
        if (readyCall) {
            readyCall(frame);
        }
        break;
    }
    case FrameType::kMsgType_Ask_Delete: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Delete, [](const Message& sourceMsg, Message& ansMsg) {
            LocalHandle::AskDelete(sourceMsg.strVec, ansMsg.strVec);
        });
        break;
    }
    case FrameType::kMsgType_Ask_Rename: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Rename, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.msgStateCode = LocalHandle::AskRename(sourceMsg.ff.fullPath, sourceMsg.ft.fullPath)
                         ? MessageStateCode::kMessageStateCodeSuccess
                         : MessageStateCode::kMessageStateCodeFailed;
        });
        break;
    }
    case FrameType::kMsgType_Ask_CreateDir: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_CreateDir, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.msgStateCode = LocalHandle::AskCreateDir(sourceMsg.comStr) ? MessageStateCode::kMessageStateCodeSuccess
                                                                          : MessageStateCode::kMessageStateCodeFailed;
        });
        break;
    }
    case FrameType::kMsgType_Ask_Sha256: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Sha256, [](const Message& sourceMsg, Message& ansMsg) {
            LocalHandle::AskSha256(sourceMsg.comStr, ansMsg.comStr);
        });
        break;
    }
    case FrameType::kMsgType_Ask_Archive: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_Archive, [](const Message& sourceMsg, Message& ansMsg) {
            const auto& fileList = sourceMsg.mapData.at("");
            ansMsg.msgStateCode = LocalHandle::AskArchive(fileList, sourceMsg.comStr)
                         ? MessageStateCode::kMessageStateCodeSuccess
                         : MessageStateCode::kMessageStateCodeFailed;
        });
        break;
    }
    case FrameType::kMsgType_Ask_UnArchive: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_UnArchive, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.msgStateCode = LocalHandle::AskUnArchive(sourceMsg.ff.fullPath, sourceMsg.ft.fullPath)
                         ? MessageStateCode::kMessageStateCodeSuccess
                         : MessageStateCode::kMessageStateCodeFailed;
        });
        break;
    }
    case FrameType::kMsgType_Ask_HomeAndDriver: {
        dispatchMessage(frame, FrameType::kMsgType_Answer_HomeAndDriver, [](const Message& sourceMsg, Message& ansMsg) {
            ansMsg.msgStateCode = LocalHandle::AskHomeAndDriver(ansMsg.strVec, ansMsg.comStr)
                         ? MessageStateCode::kMessageStateCodeSuccess
                         : MessageStateCode::kMessageStateCodeFailed;
        });
        break;
    }
    default: {
        std::function<void(FramePtr frame)> pubCall;
        {
            QMutexLocker locker(&pubCallLock_);
            auto it = pubCall_.find(frame->type);
            if (it != pubCall_.end() && it.value()) {
                pubCall = it.value();
            }
        }
        if (pubCall) {
            // 回调在锁外执行，避免业务逻辑反向调用 ControlSession 时死锁
            pubCall(frame);
            break;
        }
        std::shared_ptr<WaiteFrame> waiter;
        {
            QMutexLocker locker(&requestWaitLock_);
            auto it = requestWaitFrame_.find(frame->sessionId);
            if (it != requestWaitFrame_.end()) {
                waiter = it.value();
                requestWaitFrame_.erase(it);
            }
        }
        if (waiter) {
            timerPoolStd_->stop(waiter->timerId);
            if (waiter->done.exchange(true)) {
                break;
            }
            switch (waiter->callType) {
            case CallType::CT_Message: {
                waiter->call(answerMsg);
                break;
            }
            case CallType::CT_Frame: {
                waiter->call(frame);
                break;
            }
            default:
                break;
            }
        } else {
            qDebug() << "[TRACE-SID] no waiter for sid=" << frame->sessionId
                     << "type=" << static_cast<int>(frame->type) << "(answer ignored)";
        }
        break;
    }
    }
}

void ControlSession::RegisterPubCall(FrameType type, std::function<void(FramePtr frame)> callback)
{
    QMutexLocker locker(&pubCallLock_);
    if (callback) {
        pubCall_[type] = std::move(callback);
    } else {
        pubCall_.remove(type);
    }
}

bool ControlSession::SendWithCall(const Message& msg, FrameType type, std::function<void(FramePtr)> callback)
{
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->type = type;
    frame->to = clientCore_->oInfo_.clientId;
    return SendCall(frame, std::move(callback));
}

bool ControlSession::SendWithCall(FramePtr frame, std::function<void(MessagePtr)> callback)
{
    return SendCall(frame, std::move(callback));
}

bool ControlSession::SendWithCall(const Message& msg, FrameType type, std::function<void(MessagePtr)> callback)
{
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->type = type;
    frame->to = clientCore_->oInfo_.clientId;
    return SendCall(frame, std::move(callback));
}

bool ControlSession::SendWithCall(FramePtr frame, std::function<void(FramePtr)> callback)
{
    return SendCall(frame, std::move(callback));
}

void ControlSession::AskOwnID(const QString& name)
{
    Message msg;
    msg.from.clientName = name.toStdString();
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->sessionId = clientCore_->GetSessionId();
    frame->type = FrameType::kMsgType_Ask_ID;
    emit signalRequestSend(frame);
}

std::shared_ptr<ControlSession::TaskWorker> ControlSession::TaskWorker::CreateWorker(FramePtr frame)
{
    auto r = std::make_shared<ControlSession::TaskWorker>();
    r->frame = frame;
    r->isDone = false;
    r->sessionId = frame->sessionId;
    return r;
}

void ControlSession::clearWorker()
{
    QMutexLocker locker(&responseWaitLock_);
    for (auto iter = responseWaitWorker_.begin(); iter != responseWaitWorker_.end();) {
        if (iter.value()->isDone) {
            iter = responseWaitWorker_.erase(iter);
        } else {
            ++iter;
        }
    }
}