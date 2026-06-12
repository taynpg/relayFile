#include "Message.h"

Message::Message(const Message& o)
{
    comStr = o.comStr;
    errMsg = o.errMsg;
    from = o.from;
    to = o.to;
    msgStateCode = o.msgStateCode;
    ff = o.ff;
    ft = o.ft;
    mark = o.mark;
    clientList = o.clientList;
    mapData = o.mapData;
}

Message& Message::operator=(const Message& o)
{
    if (this == &o) {
        return *this;
    }
    comStr = o.comStr;
    errMsg = o.errMsg;
    transId = o.transId;
    uuid = o.uuid;
    from = o.from;
    to = o.to;
    msgStateCode = o.msgStateCode;
    ff = o.ff;
    ft = o.ft;
    mark = o.mark;
    clientList = o.clientList;
    mapData = o.mapData;
    return *this;
}

std::shared_ptr<Message> Message::Create()
{
    return std::make_shared<Message>();
}
