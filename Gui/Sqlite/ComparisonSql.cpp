#include "ComparisonSql.h"

#include <QDebug>
#include <QRegularExpression>
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
                                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                "name TEXT, "
                                "type TEXT, "
                                "mark TEXT, "
                                "localDir TEXT, "
                                "remoteDir TEXT"
                                ")")
                            .arg(tableName);

    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        qCritical() << "createTable error:" << query.lastError().text();
        return false;
    }
    return true;
}

QVector<CompDataItem> ComparisonSql::getAll()
{
    QVector<CompDataItem> items;

    const QString sql = QString("SELECT * FROM %1").arg(tableName_);
    QSqlQuery query(db_);

    if (!query.exec(sql)) {
        qCritical() << "getAll error:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        CompDataItem item;
        item.id = query.value(0).toInt();
        item.name = query.value(1).toString();
        item.type = query.value(2).toString();
        item.mark = query.value(3).toString();
        item.localDir = query.value(4).toString();
        item.remoteDir = query.value(5).toString();
        items.append(item);
    }
    return items;
}

bool ComparisonSql::getItem(int id, CompDataItem& item)
{
    const QString sql = QString("SELECT * FROM %1 WHERE id = :id").arg(tableName_);

    QSqlQuery query(db_);
    query.prepare(sql);
    query.bindValue(":id", id);

    if (!query.exec()) {
        qCritical() << "getItem error:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        item.id = query.value(0).toInt();
        item.name = query.value(1).toString();
        item.type = query.value(2).toString();
        item.mark = query.value(3).toString();
        item.localDir = query.value(4).toString();
        item.remoteDir = query.value(5).toString();
        return true;
    }
    return false;
}

bool ComparisonSql::addItem(CompDataItem& item)
{
    const QString sql = QString("INSERT INTO %1 (name, type, mark, localDir, remoteDir) "
                                "VALUES (:name, :type, :mark, :localDir, :remoteDir)")
                            .arg(tableName_);

    QSqlQuery query(db_);
    query.prepare(sql);

    query.bindValue(":name", item.name);
    query.bindValue(":type", item.type);
    query.bindValue(":mark", item.mark);
    query.bindValue(":localDir", item.localDir);
    query.bindValue(":remoteDir", item.remoteDir);

    if (!query.exec()) {
        qCritical() << "addItem error:" << query.lastError().text();
        return false;
    }

    item.id = query.lastInsertId().toInt();
    return true;
}

bool ComparisonSql::dropTable(const QString& tableName)
{
    const QString sql = QString("DROP TABLE IF EXISTS %1").arg(tableName);

    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        qCritical() << "dropTable error:" << query.lastError().text();
        return false;
    }
    return true;
}

bool ComparisonSql::updateItem(const CompDataItem& item)
{
    const QString sql = QString("UPDATE %1 SET name=:name, type=:type, mark=:mark, "
                                "localDir=:localDir, remoteDir=:remoteDir WHERE id=:id")
                            .arg(tableName_);

    QSqlQuery query(db_);
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

bool ComparisonSql::deleteItem(int id)
{
    const QString sql = QString("DELETE FROM %1 WHERE id=:id").arg(tableName_);

    QSqlQuery query(db_);
    query.prepare(sql);
    query.bindValue(":id", id);

    if (!query.exec()) {
        qCritical() << "deleteItem error:" << query.lastError().text();
        return false;
    }
    return true;
}

bool ComparisonSql::haveItem(int id)
{
    const QString sql = QString("SELECT id FROM %1 WHERE id=:id").arg(tableName_);

    QSqlQuery query(db_);
    query.prepare(sql);
    query.bindValue(":id", id);

    if (!query.exec()) {
        qCritical() << "haveItem error:" << query.lastError().text();
        return false;
    }
    return query.next();
}

bool ComparisonSql::getLatestItemId(int& id)
{
    const QString sql = QString("SELECT MAX(id) FROM %1").arg(tableName_);

    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        qCritical() << "getLatestItemId error:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        id = query.value(0).toInt();
        return true;
    }
    return false;
}

QVector<QString> ComparisonSql::tables() const
{
    QVector<QString> result;

    const QString sql = "SELECT name FROM sqlite_master "
                        "WHERE type='table' "
                        "AND name != 'sqlite_sequence' "
                        "ORDER BY name";

    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        qCritical() << "tables error:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        result.append(query.value(0).toString());
    }
    return result;
}

bool ComparisonSql::tableExists(const QString& tableName)
{
    const QString sql = "SELECT name FROM sqlite_master "
                        "WHERE type='table' AND name=:tableName";

    QSqlQuery query(db_);
    query.prepare(sql);
    query.bindValue(":tableName", tableName);

    if (!query.exec()) {
        qCritical() << "tableExists error:" << query.lastError().text();
        return false;
    }
    return query.next();
}

bool ComparisonSql::isNameValid(const QString& name)
{
    if (name.isEmpty()) {
        qWarning() << "名称不能为空";
        return false;
    }

    if (name.length() > 128) {
        qWarning() << "名称长度不能超过128个字符";
        return false;
    }

    if (name.startsWith("sqlite_", Qt::CaseInsensitive)) {
        qWarning() << "名称不能以 'sqlite_' 开头";
        return false;
    }

    if (name.contains(' ')) {
        qWarning() << "名称不能包含空格";
        return false;
    }

    static const QRegularExpression re("^[A-Za-z_][A-Za-z0-9_]*$");
    if (!re.match(name).hasMatch()) {
        qWarning() << "名称只能包含字母、数字和下划线，且不能以数字开头";
        return false;
    }

    static const QSet<QString> keywords = {"ABORT",
                                           "ACTION",
                                           "ADD",
                                           "AFTER",
                                           "ALL",
                                           "ALTER",
                                           "ANALYZE",
                                           "AND",
                                           "AS",
                                           "ASC",
                                           "ATTACH",
                                           "AUTOINCREMENT",
                                           "BEFORE",
                                           "BEGIN",
                                           "BETWEEN",
                                           "BY",
                                           "CASCADE",
                                           "CASE",
                                           "CAST",
                                           "CHECK",
                                           "COLLATE",
                                           "COLUMN",
                                           "COMMIT",
                                           "CONFLICT",
                                           "CONSTRAINT",
                                           "CREATE",
                                           "CROSS",
                                           "CURRENT",
                                           "CURRENT_DATE",
                                           "CURRENT_TIME",
                                           "CURRENT_TIMESTAMP",
                                           "DATABASE",
                                           "DEFAULT",
                                           "DEFERRABLE",
                                           "DEFERRED",
                                           "DELETE",
                                           "DESC",
                                           "DETACH",
                                           "DISTINCT",
                                           "DO",
                                           "DROP",
                                           "EACH",
                                           "ELSE",
                                           "END",
                                           "ESCAPE",
                                           "EXCEPT",
                                           "EXCLUDE",
                                           "EXCLUSIVE",
                                           "EXISTS",
                                           "EXPLAIN",
                                           "FAIL",
                                           "FILTER",
                                           "FOLLOWING",
                                           "FOR",
                                           "FOREIGN",
                                           "FROM",
                                           "FULL",
                                           "GLOB",
                                           "GROUP",
                                           "HAVING",
                                           "IF",
                                           "IGNORE",
                                           "IMMEDIATE",
                                           "IN",
                                           "INDEX",
                                           "INDEXED",
                                           "INITIALLY",
                                           "INNER",
                                           "INSERT",
                                           "INSTEAD",
                                           "INTERSECT",
                                           "INTO",
                                           "IS",
                                           "ISNULL",
                                           "JOIN",
                                           "KEY",
                                           "LEFT",
                                           "LIKE",
                                           "LIMIT",
                                           "MATCH",
                                           "NATURAL",
                                           "NO",
                                           "NOT",
                                           "NOTHING",
                                           "NOTNULL",
                                           "NULL",
                                           "OF",
                                           "OFFSET",
                                           "ON",
                                           "OR",
                                           "ORDER",
                                           "OTHERS",
                                           "OUTER",
                                           "OVER",
                                           "PARTITION",
                                           "PLAN",
                                           "PRAGMA",
                                           "PRECEDING",
                                           "PRIMARY",
                                           "QUERY",
                                           "RAISE",
                                           "RANGE",
                                           "RECURSIVE",
                                           "REFERENCES",
                                           "REGEXP",
                                           "REINDEX",
                                           "RELEASE",
                                           "RENAME",
                                           "REPLACE",
                                           "RESTRICT",
                                           "RETURNING",
                                           "RIGHT",
                                           "ROLLBACK",
                                           "ROW",
                                           "ROWS",
                                           "SAVEPOINT",
                                           "SELECT",
                                           "SET",
                                           "TABLE",
                                           "TEMP",
                                           "TEMPORARY",
                                           "THEN",
                                           "TIES",
                                           "TO",
                                           "TRANSACTION",
                                           "TRIGGER",
                                           "UNBOUNDED",
                                           "UNION",
                                           "UNIQUE",
                                           "UPDATE",
                                           "USING",
                                           "VACUUM",
                                           "VALUES",
                                           "VIEW",
                                           "WHEN",
                                           "WHERE",
                                           "WINDOW",
                                           "WITH",
                                           "WITHOUT"};

    if (keywords.contains(name.toUpper())) {
        qWarning() << "名称不能是SQLite的关键字:" << name;
        return false;
    }
    return true;
}