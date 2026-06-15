#pragma once

#include <QSqlDatabase>
#include <QVector>

struct CompDataItem {
    QString id;
    QString name;
    QString type;
    QString mark;
    QString localDir;
    QString remoteDir;
};

class ComparisonSql
{
public:
    ComparisonSql();
    ~ComparisonSql();

public:
    bool open(const QString& dbPath);
    void close();

    bool createTable(const QString& tableName);
    void setTableName(const QString& tableName);
    QString tableName() const;
    bool tableExists(const QString& tableName);

public:
    QVector<CompDataItem> getAll();
    bool getItem(const QString& id, CompDataItem& item);
    bool addItem(const CompDataItem& item);
    bool updateItem(const CompDataItem& item);
    bool deleteItem(const QString& id);
    bool haveItem(const QString& id);
    bool getLatestItemId(QString& id);

private:
    QString configPath_;
    QString tableName_;
    QSqlDatabase db_;
};