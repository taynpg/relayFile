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

bool RemoteAskDF::AskFileList(const std::string& path, std::vector<FileMeta>& fileList, bool recursive)
{
    Message msg;
    msg.comStr = path;

    if (recursive) {
        msg.mark = 1;
    }

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

bool RemoteAskDF::AskFileMeta(const std::string& path, FileMeta& meta)
{
    Message msg;
    msg.comStr = path;
    return Request(
        msg,
        [&meta](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            meta = ret->ff;
            return true;
        },
        FrameType::kMsgType_Ask_FileMeta);
}

bool RemoteAskDF::AskDelete(const std::vector<std::string>& fileList, std::vector<std::string>& failedList)
{
    Message msg;
    msg.strVec = fileList;
    return Request(
        msg,
        [&failedList](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            failedList = ret->strVec;
            return true;
        },
        FrameType::kMsgType_Ask_Delete);
}

bool RemoteAskDF::AskSha256(const std::string& path, std::string& sha256)
{
    Message msg;
    msg.comStr = path;
    return Request(
        msg,
        [&sha256](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            sha256 = ret->comStr;
            return true;
        },
        FrameType::kMsgType_Ask_Sha256);
}

bool RemoteAskDF::AskRename(const std::string& oldName, const std::string& newName)
{
    Message msg;
    msg.ff.fullPath = oldName;
    msg.ft.fullPath = newName;
    return Request(
        msg,
        [](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            return ret->mark == 1;
        },
        FrameType::kMsgType_Ask_Rename);
}

bool RemoteAskDF::AskCreateDir(const std::string& path)
{
    Message msg;
    msg.comStr = path;
    return Request(
        msg,
        [](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            return ret->mark == 1;
        },
        FrameType::kMsgType_Ask_CreateDir);
}

bool RemoteAskDF::AskArchive(const std::vector<FileMeta>& fileList, const std::string& archivePath)
{
    Message msg;
    msg.mapData[""] = fileList;
    msg.comStr = archivePath;
    return Request(
        msg,
        [](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            return ret->mark == 1;
        },
        FrameType::kMsgType_Ask_Archive);
}

bool RemoteAskDF::AskUnArchive(const std::string& archivePath, const std::string& extractPath)
{
    Message msg;
    msg.ff.fullPath = archivePath;
    msg.ft.fullPath = extractPath;
    return Request(
        msg,
        [](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            return ret->mark == 1;
        },
        FrameType::kMsgType_Ask_UnArchive);
}

bool RemoteAskDF::AskHomeAndDriver(std::vector<std::string>& drivers, std::string& home)
{
    Message msg;
    return Request(
        msg,
        [&drivers, &home](MessagePtr ret) {
            if (ret == nullptr) {
                return false;
            }
            drivers = ret->strVec;
            home = ret->comStr;
            return true;
        },
        FrameType::kMsgType_Ask_HomeAndDriver);
}
