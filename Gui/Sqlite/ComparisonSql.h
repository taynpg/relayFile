#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>

struct CompDataItem {
    int id;
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

    // 表管理
    bool createTable(const QString& tableName);
    void setTableName(const QString& tableName);
    QString tableName() const;
    bool tableExists(const QString& tableName);
    bool dropTable(const QString& tableName);
    QVector<QString> tables() const;

public:
    // CRUD
    QVector<CompDataItem> getAll();
    bool getItem(int id, CompDataItem& item);
    bool addItem(CompDataItem& item);
    bool updateItem(const CompDataItem& item);
    bool deleteItem(int id);
    bool haveItem(int id);
    bool getLatestItemId(int& id);
    bool isNameValid(const QString& name);

private:
    QString configPath_;
    QString tableName_;
    QSqlDatabase db_;
};