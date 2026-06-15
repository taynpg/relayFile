#include "ComparisonSql.h"

ComparisonSql::ComparisonSql()
{
}

ComparisonSql::~ComparisonSql()
{
}

bool ComparisonSql::Open(const QString& configPath)
{
    configPath_ = configPath;
    db_ = QSqlDatabase::addDatabase("QSQLITE");
    db_.setDatabaseName(configPath_);
    return db_.open();
}
void ComparisonSql::Close()
{
    db_.close();
}
