#include "BaseAskDF.h"

#include "LocalAskDF.h"
#include "RemoteAskDF.h"

std::shared_ptr<BaseAskDF> BaseAskDF::Create(AskType askType)
{
    switch (askType) {
    case AskType::ASK_TYPE_LOCAL:
        return std::make_shared<LocalAskDF>();
    case AskType::ASK_TYPE_REMOTE:
        return std::make_shared<RemoteAskDF>();
    default:
        return nullptr;
    }
}