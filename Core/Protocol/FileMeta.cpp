#include "FileMeta.h"

FileMeta::FileMeta(const FileMeta& o)
{
    if (this == &o) {
        return;
    }
    this->dir = o.dir;
    this->name = o.name;
    this->size = o.size;
    this->type = o.type;
    this->sizeStr = o.sizeStr;
    this->fullPath = o.fullPath;
    this->exist = o.exist;
    this->lastModified = o.lastModified;
    this->permission = o.permission;
    this->localRoot = o.localRoot;
    this->remoteRoot = o.remoteRoot;
}

FileMeta& FileMeta::operator=(const FileMeta& o)
{
    if (this == &o) {
        return *this;
    }
    this->dir = o.dir;
    this->name = o.name;
    this->size = o.size;
    this->exist = o.exist;
    this->type = o.type;
    this->sizeStr = o.sizeStr;
    this->fullPath = o.fullPath;
    this->lastModified = o.lastModified;
    this->permission = o.permission;
    this->localRoot = o.localRoot;
    this->remoteRoot = o.remoteRoot;
    return *this;
}

FileMeta::FileMeta(FileMeta&& s) noexcept
{
    this->dir = std::move(s.dir);
    this->name = std::move(s.name);
    this->size = std::move(s.size);
    this->type = std::move(s.type);
    this->sizeStr = std::move(s.sizeStr);
    this->exist = std::move(s.exist);
    this->fullPath = std::move(s.fullPath);
    this->lastModified = std::move(s.lastModified);
    this->permission = std::move(s.permission);
    this->localRoot = std::move(s.localRoot);
    this->remoteRoot = std::move(s.remoteRoot);
}
