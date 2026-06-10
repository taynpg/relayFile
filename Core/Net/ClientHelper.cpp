#include "ClientHelper.h"

DoubleLinker::DoubleLinker(QObject* parent) : QObject(parent)
{
}

DoubleLinker::~DoubleLinker()
{
}

void DoubleLinker::SetControlSession(std::shared_ptr<ControlSession> session)
{
    controlSession_ = session;
    auto* cliCore = controlSession_->getClientCore();
    connect(cliCore, &ClientCore::signalDeliverFrame, this, &DoubleLinker::onDeliverControl);
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

void DoubleLinker::SetFileSession(std::shared_ptr<FileSession> session)
{
    fileSession_ = session;
    auto* cliCore = fileSession_->getClientCore();
    connect(cliCore, &ClientCore::signalDeliverFrame, this, &DoubleLinker::onDeliverFile);
}

template <typename HandleResp> bool DoubleLinker::Request(FramePtr frame, HandleResp handleResp)
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

void DoubleLinker::onDeliverControl(FramePtr frame)
{
    controlSession_->handleFrame(frame);
}

void DoubleLinker::onDeliverFile(FramePtr frame)
{
    fileSession_->handleFrame(frame);
}