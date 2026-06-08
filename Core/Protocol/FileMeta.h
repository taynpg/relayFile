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
    std::string dir;
    std::string name;
    std::uint64_t size;
    FileType type;
    int64_t lastModified;
    template <class Archive> void serialize(Archive& ar)
    {
        ar(dir, name, size, type, lastModified);
    }
};
