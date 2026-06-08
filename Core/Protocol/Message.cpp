#include "Message.h"

Message::Message(const Message& o) : msType(o.msType)
{
    msData = o.msData;
    errData = o.errData;
    from = o.from;
    to = o.to;
    clientList = o.clientList;
    mapData = o.mapData;
}

Message& Message::operator=(const Message& o)
{
    if (this == &o) {
        return *this;
    }
    msType = o.msType;
    msData = o.msData;
    errData = o.errData;
    from = o.from;
    to = o.to;
    clientList = o.clientList;
    mapData = o.mapData;
    return *this;
}