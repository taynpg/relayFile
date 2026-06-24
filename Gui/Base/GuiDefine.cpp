#include "GuiDefine.h"

FileItemData::FileItemData(const FileItemData& o)
{
    if (this == &o) {
        return;
    }
    localRoot = o.localRoot;
    remoteRoot = o.remoteRoot;
    name = o.name;
    path = o.path;
    type = o.type;
    size = o.size;
    sizeStr = o.sizeStr;
}

FileItemData& FileItemData::operator=(const FileItemData& o)
{
    if (this == &o) {
        return *this;
    }
    localRoot = o.localRoot;
    remoteRoot = o.remoteRoot;
    name = o.name;
    path = o.path;
    type = o.type;
    size = o.size;
    sizeStr = o.sizeStr;
    return *this;
}

FileItemData::FileItemData(FileItemData&& o) noexcept
{
    if (this == &o) {
        return;
    }
    localRoot = std::move(o.localRoot);
    remoteRoot = std::move(o.remoteRoot);
    name = std::move(o.name);
    path = std::move(o.path);
    type = o.type;
    size = o.size;
    sizeStr = std::move(o.sizeStr);
}
