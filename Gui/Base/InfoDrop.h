#pragma once

#include <QBuffer>
#include <QDataStream>
#include <QString>
#include <QVector>

#define MY_MIME_DROP_TYPE "application/x-info-drop"

template <typename T> QByteArray infoPack(const T& obj)
{
    QByteArray byteArray;
    QDataStream stream(&byteArray, QIODevice::ReadWrite);
    obj.serialize(stream);
    stream.device()->seek(0);
    return byteArray;
}

template <typename T> T infoUnpack(const QByteArray& byteArray)
{
    T obj;
    QDataStream stream(byteArray);
    obj.deserialize(stream);
    return obj;
}

struct InfoDropItem {
    QString fileName;
    quint32 type;
    void serialize(QDataStream& stream) const;
    void deserialize(QDataStream& stream);
};

struct InfoDrop {
    QString from;
    QString to;
    QVector<InfoDropItem> items;
    void serialize(QDataStream& stream) const;
    void deserialize(QDataStream& stream);
};

QDataStream& operator<<(QDataStream& stream, const InfoDropItem& item);
QDataStream& operator>>(QDataStream& stream, InfoDropItem& item);
QDataStream& operator<<(QDataStream& stream, const InfoDrop& infoDrop);
QDataStream& operator>>(QDataStream& stream, InfoDrop& infoDrop);