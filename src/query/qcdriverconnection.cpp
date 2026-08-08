#include "qcdriverconnection.h"

QcNamedResultSet toNamedResultSet(const QcSqlBase::QcStringList & columnNames, const QcResultSet & rows)
{
    QcNamedResultSet named;
    named.reserve(rows.size());
    for (const QcResultRow & row : rows) {
        QcNamedRow namedRow;
        for (std::size_t c = 0; c < row.size() && c < columnNames.size(); ++c) {
            namedRow[columnNames[c]] = row[c];
        }
        named.push_back(std::move(namedRow));
    }
    return named;
}

std::optional<QcResultSet> IQcDriverConnection::executeReturning(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                                                   std::size_t returningColumnCount)
{
    (void)returningColumnCount;
    return execute(sql, params);
}

std::optional<QcNamedResultSet> IQcDriverConnection::executeReturningNamed(const std::string & sql,
                                                                            const QcSqlBase::QcVariantList & params,
                                                                            std::size_t returningColumnCount,
                                                                            const QcSqlBase::QcStringList & returningColumnNames)
{
    (void)returningColumnCount;
    (void)returningColumnNames;
    QcSqlBase::QcStringList columnNames;
    auto rows = execute(sql, params, &columnNames);
    if (!rows) {
        return std::nullopt;
    }
    return toNamedResultSet(columnNames, *rows);
}
