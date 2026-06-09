#include "RemoteAskDF.h"

#include <Protocol/Serialize.hpp>
#include <future>

#include "../BaseHelper.h"

RemoteAskDF::RemoteAskDF()
{
    clientControl_ = GlobalData::getInstance()->getClientControl();
}

bool RemoteAskDF::GetFileList(const std::string& path, std::vector<FileMeta>& fileList)
{
    return false;
}
bool RemoteAskDF::AskHome(std::string& home)
{
    Message msg;
    msg.msType = MessageType::kMessageAskHome;
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);

    std::promise<MessagePtr> promise;
    auto future = promise.get_future();

    QMetaObject::invokeMethod(clientControl_, [this, &promise, frame]() {
        clientControl_->SendWithCall(frame, [this, &promise](MessagePtr m) { promise.set_value(m); });
    });

    MessagePtr ret = future.get();
    if (ret) {
        home = ret->msData;
        return true;
    }
    qWarning() << "获取远端" << clientControl_->getClientFullName() << "目录失败";
    return false;
}