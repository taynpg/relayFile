#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>
#include <sstream>
#include <vector>

#include "cereal/archives/binary.hpp"

template <typename T> std::vector<char> serializeStruct(const T& obj)
{
    std::stringstream ss;
    {
        cereal::BinaryOutputArchive ar(ss);
        ar(obj);
    }
    const std::string& str = ss.str();
    return std::vector<char>(str.begin(), str.end());
}

template <typename T> void deserializeStruct(const std::vector<char>& data, T& obj)
{
    std::stringstream ss;
    ss.write(data.data(), static_cast<std::streamsize>(data.size()));
    cereal::BinaryInputArchive ar(ss);
    ar(obj);
}