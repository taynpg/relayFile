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
    this->lastModified = o.lastModified;
    this->permission = o.permission;
}

FileMeta& FileMeta::operator=(const FileMeta& o)
{
    if (this == &o) {
        return *this;
    }
    this->dir = o.dir;
    this->name = o.name;
    this->size = o.size;
    this->type = o.type;
    this->sizeStr = o.sizeStr;
    this->fullPath = o.fullPath;
    this->lastModified = o.lastModified;
    this->permission = o.permission;
    return *this;
}

FileMeta::FileMeta(FileMeta&& s) noexcept
{
    this->dir = std::move(s.dir);
    this->name = std::move(s.name);
    this->size = std::move(s.size);
    this->type = std::move(s.type);
    this->sizeStr = std::move(s.sizeStr);
    this->fullPath = std::move(s.fullPath);
    this->lastModified = std::move(s.lastModified);
    this->permission = std::move(s.permission);
}
