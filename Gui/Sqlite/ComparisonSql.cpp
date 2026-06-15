#include "ComparisonSql.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

ComparisonSql::ComparisonSql()
{
}

ComparisonSql::~ComparisonSql()
{
    close();
}

bool ComparisonSql::open(const QString& dbPath)
{
    configPath_ = dbPath;

    db_ = QSqlDatabase::addDatabase("QSQLITE");
    db_.setDatabaseName(configPath_);

    if (!db_.open()) {
        qCritical() << "Failed to open database:" << db_.lastError().text();
        return false;
    }
    return true;
}

void ComparisonSql::close()
{
    if (db_.isOpen()) {
        db_.close();
    }
}

void ComparisonSql::setTableName(const QString& tableName)
{
    tableName_ = tableName;
}

QString ComparisonSql::tableName() const
{
    return tableName_;
}

bool ComparisonSql::createTable(const QString& tableName)
{
    const QString sql = QString("CREATE TABLE IF NOT EXISTS %1 ("
                                "id TEXT PRIMARY KEY, "
                                "name TEXT, "
                                "type TEXT, "
                                "mark TEXT, "
                                "localDir TEXT, "
                                "remoteDir TEXT"
                                ")")
                            .arg(tableName);

    QSqlQuery query(sql);
    if (!query.exec()) {
        qCritical() << "createTable error:" << query.lastError().text();
        return false;
    }
    return true;
}

QVector<CompDataItem> ComparisonSql::getAll()
{
    QVector<CompDataItem> items;

    const QString sql = QString("SELECT * FROM %1").arg(tableName_);
    QSqlQuery query(sql);

    if (!query.exec()) {
        qCritical() << "getAll error:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        CompDataItem item;
        item.id = query.value(0).toString();
        item.name = query.value(1).toString();
        item.type = query.value(2).toString();
        item.mark = query.value(3).toString();
        item.localDir = query.value(4).toString();
        item.remoteDir = query.value(5).toString();
        items.append(item);
    }
    return items;
}

bool ComparisonSql::getItem(const QString& id, CompDataItem& item)
{
    const QString sql = QString("SELECT * FROM %1 WHERE id = :id").arg(tableName_);
    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(":id", id);

    if (!query.exec()) {
        qCritical() << "getItem error:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        item.id = query.value(0).toString();
        item.name = query.value(1).toString();
        item.type = query.value(2).toString();
        item.mark = query.value(3).toString();
        item.localDir = query.value(4).toString();
        item.remoteDir = query.value(5).toString();
        return true;
    }
    return false;
}

bool ComparisonSql::addItem(const CompDataItem& item)
{
    const QString sql = QString("INSERT INTO %1 (id, name, type, mark, localDir, remoteDir) "
                                "VALUES (:id, :name, :type, :mark, :localDir, :remoteDir)")
                            .arg(tableName_);

    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(":id", item.id);
    query.bindValue(":name", item.name);
    query.bindValue(":type", item.type);
    query.bindValue(":mark", item.mark);
    query.bindValue(":localDir", item.localDir);
    query.bindValue(":remoteDir", item.remoteDir);

    if (!query.exec()) {
        qCritical() << "addItem error:" << query.lastError().text();
        return false;
    }
    return true;
}

bool ComparisonSql::updateItem(const CompDataItem& item)
{
    const QString sql = QString("UPDATE %1 SET name=:name, type=:type, mark=:mark, "
                                "localDir=:localDir, remoteDir=:remoteDir WHERE id=:id")
                            .arg(tableName_);

    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(":id", item.id);
    query.bindValue(":name", item.name);
    query.bindValue(":type", item.type);
    query.bindValue(":mark", item.mark);
    query.bindValue(":localDir", item.localDir);
    query.bindValue(":remoteDir", item.remoteDir);

    if (!query.exec()) {
        qCritical() << "updateItem error:" << query.lastError().text();
        return false;
    }
    return true;
}

bool ComparisonSql::deleteItem(const QString& id)
{
    const QString sql = QString("DELETE FROM %1 WHERE id=:id").arg(tableName_);
    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(":id", id);

    if (!query.exec()) {
        qCritical() << "deleteItem error:" << query.lastError().text();
        return false;
    }
    return true;
}

bool ComparisonSql::haveItem(const QString& id)
{
    const QString sql = QString("SELECT id FROM %1 WHERE id=:id").arg(tableName_);
    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(":id", id);

    if (!query.exec()) {
        qCritical() << "haveItem error:" << query.lastError().text();
        return false;
    }
    return query.next();
}

bool ComparisonSql::getLatestItemId(QString& id)
{
    const QString sql = QString("SELECT MAX(id) FROM %1").arg(tableName_);
    QSqlQuery query(sql);

    if (!query.exec()) {
        qCritical() << "getLatestItemId error:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        id = query.value(0).toString();
        return true;
    }
    return false;
}

bool ComparisonSql::tableExists(const QString& tableName)
{
    const QString sql = "SELECT name FROM sqlite_master "
                        "WHERE type='table' AND name=:tableName";

    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(":tableName", tableName);

    if (!query.exec()) {
        qCritical() << "tableExists error:" << query.lastError().text();
        return false;
    }

    return query.next();
}