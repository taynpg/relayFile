#pragma once

#include <File/FileDir.h>
#include <QString>
#include <QVector>

#define GUI_FILE_TYPE_DIR "Dir"
#define GUI_FILE_TYPE_FILE "File"
#define GUI_FILE_TYPE_UNKNOWN "Unknown"

#define GUI_FILE_TRAN_STATE_WAIT "等待"
#define GUI_FILE_TRAN_STATE_TRANS "传输中"
#define GUI_FILE_TRAN_STATE_DONE "已完成"
#define GUI_FILE_TRAN_STATE_FAILED "失败"
#define GUI_FILE_TRAN_STATE_SKIP "跳过"

#define GUI_DIRECTION_LOCAL "本地"
#define GUI_DIRECTION_REMOTE "远端"

struct FileItemData {
    QString name;
    QString path;
    RFileType type;
    std::uint64_t size{};
    QString sizeStr{};
};

struct RelayTaskData {
    QString localRoot;
    QString remoteRoot;
    QVector<FileItemData> fileList;
    bool isUpload{};
};

enum class RelayTaskStatus {
    Init,
    Checking,
    Transing,
    TransComplete,
    TransFail,
};
