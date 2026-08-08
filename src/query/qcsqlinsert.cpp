#include "qcsqlinsert.h"

#include "qcsqldialect.h"

QcSqlInsert::QcSqlInsert()
{
}

QcSqlInsert & QcSqlInsert::useDriver(QcDbDriver driver)
{
    m_driver = driver;
    return *this;
}

QcSqlInsert & QcSqlInsert::into(const std::string & table)
{
    m_table = table;
    return *this;
}

QcSqlInsert & QcSqlInsert::set(const std::string & column, const QcVariant & value)
{
    for (auto & entry : m_columns) {
        if (entry.first == column) {
            entry.second = value;
            return *this;
        }
    }

    m_columns.emplace_back(column, value);
    return *this;
}

QcSqlInsert & QcSqlInsert::returning(const QcStringList & columns)
{
    m_returning = columns;
    m_autoIncrementColumn.clear();
    return *this;
}

QcSqlInsert & QcSqlInsert::returning(const QcStringList & columns, const std::string & autoIncrementColumn)
{
    m_returning = columns;
    m_autoIncrementColumn = autoIncrementColumn;
    return *this;
}

QcSqlInsert::QcStringList QcSqlInsert::validate() const
{
    QcStringList problems;

    if (m_table.empty()) {
        problems.push_back("INSERT has no target table (call into())");
    }

    return problems;
}

std::string QcSqlInsert::toSql(QcVariantList & params, QcDbDriver driver) const
{
    qcThrowIfQueryInvalid(validate());

    std::string columnsSql;
    std::string valuesSql;

    for (std::size_t i = 0; i < m_columns.size(); ++i) {
        if (i > 0) {
            columnsSql += ", ";
            valuesSql += ", ";
        }
        columnsSql += QcSqlDialect::quoteIdentifier(m_columns[i].first, driver);
        params.push_back(m_columns[i].second);
        valuesSql += QcSqlDialect::placeholder(params.size(), driver);
    }

    std::string sql = "INSERT INTO " + QcSqlDialect::quoteTableRef(m_table, driver) + " (" + columnsSql + ")";
    if (driver == QcDbDriver::MSSQL) {
        // MSSQL's OUTPUT sits between the column list and VALUES -- everywhere
        // else (RETURNING/RETURNING...INTO, or no clause at all) goes at the
        // very end instead.
        sql += QcSqlDialect::returningClause(m_returning, "inserted", params.size() + 1, driver);
        sql += " VALUES (" + valuesSql + ")";
    } else {
        sql += " VALUES (" + valuesSql + ")";
        sql += QcSqlDialect::returningClause(m_returning, "inserted", params.size() + 1, driver);
    }
    return sql;
}

QcSqlStatement QcSqlInsert::toSql() const
{
    return toSql(m_driver);
}

std::string QcSqlInsert::toSql(QcVariantList & params) const
{
    return toSql(params, m_driver);
}

QcSqlStatement QcSqlInsert::toSql(QcDbDriver driver) const
{
    QcSqlStatement statement;
    statement.sql = toSql(statement.params, driver);
    // Unconditional, not just for Oracle below -- cheap (a QcStringList
    // copy) and lets QcNativeConnection::executeReturningNamed() rely on it
    // being populated on every driver, even though only Oracle's OUT-bind
    // path actually reads it back (see the field's doc comment in qcsqlbase.h).
    statement.returningColumnNames = m_returning;
    if (driver == QcDbDriver::Oracle) {
        // RETURNING ... INTO's OUT-bind placeholders, if returning() was used --
        // returningClause() rendered exactly m_returning.size() of them (0 if
        // returning() was never called), immediately after the ordinary IN
        // placeholders toSql(params) already appended -- see
        // QcNativeConnection::executeReturning().
        statement.returningColumnCount = m_returning.size();
    } else if (driver == QcDbDriver::MySQL) {
        // MySQL has no INSERT ... RETURNING syntax at all -- when returning()'s
        // autoIncrementColumn overload was used, the caller (QueryCreator) is
        // expected to run this follow-up SELECT, keyed on LAST_INSERT_ID(),
        // right after this INSERT in the same transaction instead. See the doc
        // comment on QcSqlStatement::mysqlReturningSelectSql.
        if (!m_returning.empty() && !m_autoIncrementColumn.empty()) {
            std::string columns;
            for (std::size_t i = 0; i < m_returning.size(); ++i) {
                if (i > 0) {
                    columns += ", ";
                }
                columns += QcSqlDialect::quoteIdentifier(m_returning[i], driver);
            }
            statement.mysqlReturningSelectSql = "SELECT " + columns + " FROM " + QcSqlDialect::quoteTableRef(m_table, driver)
                + " WHERE " + QcSqlDialect::quoteIdentifier(m_autoIncrementColumn, driver) + " = ?";
        }
    }
    return statement;
}
