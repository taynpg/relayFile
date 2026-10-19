#include "FileSession.h"

#include <QDebug>
#include <QMetaObject>

#include "FileSessionIO.h"

FileSession::FileSession(QObject* parent) : QObject(parent)
{
    clientCore_ = new ClientCore();
    clientCore_->setIsControl(false);
    clientWorker_ = new ClientWorker(clientCore_, nullptr);
    clientCore_->moveToThread(clientWorker_);
    clientWorker_->start();

    // 传输引擎独占一个 IO 线程：写盘、ACK、重传都不经过 GUI 线程
    ioThread_ = new QThread();
    ioThread_->setObjectName("rf-transfer-io");
    io_ = new FileSessionIO(clientCore_);
    io_->moveToThread(ioThread_);
    // 线程结束时在它自己的线程里销毁引擎（含 OneFileTrans / QFile / QTimer）
    connect(ioThread_, &QThread::finished, io_, &QObject::deleteLater);

    connect(io_, &FileSessionIO::signalRequestSend, this, &FileSession::signalRequestSend);
    connect(io_, &FileSessionIO::signalCurFileProgress, this, &FileSession::signalCurFileProgress);
    connect(io_, &FileSessionIO::signalCurFile, this, &FileSession::signalCurFile);
    connect(io_, &FileSessionIO::signalTaskStateChanged, this, &FileSession::signalTaskStateChanged);
    connect(io_, &FileSessionIO::signalNeedFileConnect, this, &FileSession::signalNeedFileConnect);

    ioThread_->start();
}

FileSession::~FileSession()
{
    Quit();
}

void FileSession::Quit()
{
    if (quit_.exchange(true)) {
        return;
    }

    // 1) 先在 IO 线程内停掉所有传输、清空映射（QTimer/QFile 都在那个线程里）
    if (io_ && ioThread_ && ioThread_->isRunning()) {
        if (QThread::currentThread() != ioThread_) {
            QMetaObject::invokeMethod(io_, [this]() { io_->shutdown(); }, Qt::BlockingQueuedConnection);
            ioThread_->quit();
            ioThread_->wait();
        } else {
            io_->shutdown();
        }
    }
    io_ = nullptr;
    if (ioThread_) {
        delete ioThread_;
        ioThread_ = nullptr;
    }

    // 2) 再处理套接字线程
    if (!clientCore_) {
        delete clientWorker_;
        clientWorker_ = nullptr;
        return;
    }
    // ClientCore 及其 QTcpSocket 都是在 worker 线程里创建并使用的，
    // 必须回到 worker 线程内部销毁：直接在主线程 delete 会触发
    // "QSocketNotifier: Socket notifiers cannot be enabled or disabled from another thread"。
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
    if (clientWorker_) {
        delete clientWorker_;
        clientWorker_ = nullptr;
    }
}

void FileSession::handleFrame(FramePtr frame)
{
    if (quit_.load() || !io_) {
        return;
    }
    auto* io = io_.data();
    QMetaObject::invokeMethod(io, [io, frame]() { io->handleFrame(frame); }, Qt::QueuedConnection);
}

bool FileSession::getTransStatus(OneFileTrans::TransStatus& status, const std::string& baseUUID, bool isSend)
{
    auto* io = io_.data();
    if (!io) {
        return false;
    }
    return io->getTransStatus(status, baseUUID, isSend);
}

void FileSession::clearTask(const std::string& uuid, bool isSend)
{
    if (quit_.load() || !io_) {
        return;
    }
    auto* io = io_.data();
    const auto quuid = QString::fromStdString(uuid);
    QMetaObject::invokeMethod(io, [io, quuid, isSend]() { io->clearTask(quuid, isSend); }, Qt::QueuedConnection);
}

void FileSession::AskOwnID()
{
    if (quit_.load() || !io_) {
        return;
    }
    auto* io = io_.data();
    QMetaObject::invokeMethod(io, [io]() { io->askOwnID(); }, Qt::QueuedConnection);
}

void FileSession::setServerEndpoint(const QString& ip, qint16 port)
{
    if (quit_.load() || !io_) {
        return;
    }
    auto* io = io_.data();
    QMetaObject::invokeMethod(io, [io, ip, port]() { io->setServerEndpoint(ip, port); }, Qt::QueuedConnection);
}

ClientCore* FileSession::getClientCore()
{
    return clientCore_;
}

FileSessionIO* FileSession::io() const
{
    return io_.data();
}
