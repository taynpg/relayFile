#include "RemoteAskDF.h"

#include <Protocol/Serialize.hpp>
#include <future>

#include "../BaseHelper.h"

RemoteAskDF::RemoteAskDF()
{
    clientControl_ = GlobalData::getInstance()->getClientControl();
}

template <typename HandleResp> bool RemoteAskDF::request(Message& msg, HandleResp handleResp)
{
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);

    auto promise = std::make_shared<std::promise<MessagePtr>>();
    auto future = promise->get_future();

    QMetaObject::invokeMethod(clientControl_, [this, promise, frame]() {
        clientControl_->SendWithCall(frame, [promise](MessagePtr m) { promise->set_value(m); });
    });

    MessagePtr ret = future.get();
    if (!ret) {
        qWarning() << "请求远端" << clientControl_->getClientFullName() << "失败";
        return false;
    }

    return handleResp(ret);
}

bool RemoteAskDF::AskFileList(const std::string& path, std::vector<FileMeta>& fileList)
{
    Message msg;
    msg.msType = MessageType::kMessageAskFileList;
    msg.msData = path;

    fileList.clear();

    return request(msg, [&](MessagePtr ret) {
        if (ret == nullptr) {
            return false;
        }
        auto& listData = ret->mapData;
        for (auto& item : listData) {
            fileList = item.second;
            break;
        }
        qInfo() << "获取远端" << clientControl_->getClientFullName() << "文件列表个数:" << fileList.size();
        return true;
    });
}
bool RemoteAskDF::AskHome(std::string& home)
{
    Message msg;
    msg.msType = MessageType::kMessageAskHome;

    return request(msg, [&](MessagePtr ret) {
        if (ret == nullptr) {
            return false;
        }
        home = ret->msData;
        qInfo() << "获取远端" << clientControl_->getClientFullName() << "目录:" << QString::fromStdString(home);
        return true;
    });
}