#pragma once

#include <QSqlDatabase>


class ComparisonSql
{
public:
    ComparisonSql();
    ~ComparisonSql();

public:
    bool Open(const QString& configPath);
    void Close();

private:
    QString configPath_;
    QSqlDatabase db_;
};