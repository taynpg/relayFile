#include "InfoDrop.h"

void InfoDropItem::serialize(QDataStream& stream) const
{
    stream << fileName << type;
}

void InfoDropItem::deserialize(QDataStream& stream)
{
    stream >> fileName >> type;
}

QDataStream& operator<<(QDataStream& stream, const InfoDropItem& item)
{
    item.serialize(stream);
    return stream;
}

QDataStream& operator>>(QDataStream& stream, InfoDropItem& item)
{
    item.deserialize(stream);
    return stream;
}

void InfoDrop::serialize(QDataStream& stream) const
{
    stream << from << to;
    uint32_t itemCount = items.size();
    stream << itemCount;
    for (const auto& item : items) {
        stream << item;
    }
}

void InfoDrop::deserialize(QDataStream& stream)
{
    stream >> from >> to;
    uint32_t itemCount;
    stream >> itemCount;
    items.resize(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        stream >> items[i];
    }
}

QDataStream& operator<<(QDataStream& stream, const InfoDrop& infoDrop)
{
    infoDrop.serialize(stream);
    return stream;
}

QDataStream& operator>>(QDataStream& stream, InfoDrop& infoDrop)
{
    infoDrop.deserialize(stream);
    return stream;
}
