#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "Base/AskDirFile/BaseAskDF.h"

// 文件粗校验：先比大小，大小一致再抽取采样块比对内容
struct CompareResult
{
    bool ok{false};        // 比对过程是否成功（两端文件均可访问）
    bool same{false};      // 内容是否一致（仅 ok=true 时有意义）
    bool aExist{false};
    bool bExist{false};
    std::uint64_t aSize{};
    std::uint64_t bSize{};
    std::string errMsg;
};

class SampleCompare
{
public:
    // 粗校验两端文件内容是否一致
    // dfA/pathA 为一端，dfB/pathB 为另一端
    static CompareResult Compare(const std::shared_ptr<BaseAskDF>& dfA, const std::string& pathA,
                                 const std::shared_ptr<BaseAskDF>& dfB, const std::string& pathB);
};
