#include "Message.h"

// 拷贝/移动语义统一使用编译器生成的版本（见 Message.h）。
// 早期手写实现漏掉了 transId / uuid，导致依赖拷贝的地方拿到空 uuid，
// 而 pushTask 里还要手动补字段来绕过——不要改回手写实现。

std::shared_ptr<Message> Message::Create()
{
    return std::make_shared<Message>();
}
