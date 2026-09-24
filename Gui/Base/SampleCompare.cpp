#include "SampleCompare.h"

#include <Protocol/FileMeta.h>

CompareResult SampleCompare::Compare(const std::shared_ptr<BaseAskDF>& dfA, const std::string& pathA,
                                     const std::shared_ptr<BaseAskDF>& dfB, const std::string& pathB)
{
    CompareResult r;
    if (!dfA || !dfB) {
        r.errMsg = "校验对象为空";
        return r;
    }

    FileMeta metaA;
    FileMeta metaB;
    if (!dfA->AskFileMeta(pathA, metaA)) {
        r.errMsg = "读取A端文件信息失败";
        return r;
    }
    if (!dfB->AskFileMeta(pathB, metaB)) {
        r.errMsg = "读取B端文件信息失败";
        return r;
    }

    r.aExist = (metaA.exist != 0);
    r.bExist = (metaB.exist != 0);
    r.aSize = metaA.size;
    r.bSize = metaB.size;

    if (!r.aExist || !r.bExist) {
        r.ok = true;
        r.same = false;
        return r;
    }

    // 大小不一致直接判定不同
    if (r.aSize != r.bSize) {
        r.ok = true;
        r.same = false;
        return r;
    }

    // 大小一致时抽取采样块比对
    std::vector<SampleBlock> samplesA;
    std::vector<SampleBlock> samplesB;
    if (!dfA->AskFileSamples(pathA, samplesA)) {
        r.errMsg = "A端文件采样失败";
        return r;
    }
    if (!dfB->AskFileSamples(pathB, samplesB)) {
        r.errMsg = "B端文件采样失败";
        return r;
    }

    r.ok = true;
    r.same = false;
    if (samplesA.size() != samplesB.size()) {
        return r;
    }
    r.same = true;
    for (size_t i = 0; i < samplesA.size(); ++i) {
        if (samplesA[i].offset != samplesB[i].offset || samplesA[i].data != samplesB[i].data) {
            r.same = false;
            break;
        }
    }
    return r;
}
