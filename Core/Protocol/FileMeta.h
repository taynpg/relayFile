#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>
#include <string>

enum class FileType {
    FILE_TYPE_FILE = 0,
    FILE_TYPE_DIR,
};

struct FileMeta {
    FileMeta() = default;
    FileMeta(const FileMeta& o);
    FileMeta& operator=(const FileMeta& o);
    FileMeta(FileMeta&& s) noexcept;
    std::string dir;
    std::string name;
    std::string fullPath;
    std::uint64_t size;
    std::string sizeStr;
    FileType type;
    int64_t lastModified;
    std::uint16_t permission;
    template <class Archive> void serialize(Archive& ar)
    {
        ar(dir, name, fullPath, size, sizeStr, type, lastModified, permission);
    }
};
