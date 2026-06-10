#include "RemoteAskDF.h"

#include <Protocol/Serialize.hpp>
#include <future>

#include "../BaseHelper.h"

RemoteAskDF::RemoteAskDF()
{
    controlSession_ = GlobalData::getInstance()->getControlSession();
}

template <typename HandleResp> bool RemoteAskDF::Request(Message& msg, HandleResp handleResp, FrameType type)
{
    auto frame = OneFrame::Create();
    frame->data = serializeStruct(msg);
    frame->type = type;

    auto promise = std::make_shared<std::promise<MessagePtr>>();
    auto future = promise->get_future();

    controlSession_->SendWithCall(frame, [promise](MessagePtr m) { promise->set_value(m); });

    MessagePtr ret = future.get();
    if (!ret) {
        qWarning() << "请求远端" << controlSession_->getOtherInfo().clientId << "失败";
        return false;
    }

    return handleResp(ret);
}

bool RemoteAskDF::AskFileList(const std::string& path, std::vector<FileMeta>& fileList)
{
    Message msg;
    msg.comStr = path;

    fileList.clear();

    return Request(
        msg,
        [&](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            auto& listData = ret->mapData;
            for (auto& item : listData) {
                fileList = item.second;
                break;
            }
            qInfo() << "获取远端" << controlSession_->getOtherInfo().clientId << "文件列表个数:" << fileList.size();
            return true;
        },
        FrameType::kMsgType_Ask_FileList);
}

bool RemoteAskDF::AskHome(std::string& home)
{
    Message msg;
    return Request(
        msg,
        [&](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            home = ret->comStr;
            qInfo() << "获取远端" << controlSession_->getOtherInfo().clientId << "目录:" << QString::fromStdString(home);
            return true;
        },
        FrameType::kMsgType_Ask_Home);
}