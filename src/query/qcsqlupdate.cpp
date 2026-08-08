#include "qcsqlupdate.h"

#include "qcsqldialect.h"

QcSqlUpdate::QcSqlUpdate()
{
}

QcSqlUpdate & QcSqlUpdate::useDriver(QcDbDriver driver)
{
    m_driver = driver;
    return *this;
}

QcSqlUpdate & QcSqlUpdate::table(const std::string & name)
{
    m_table = name;
    return *this;
}

QcSqlUpdate & QcSqlUpdate::set(const std::string & column, const QcVariant & value)
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

QcSqlUpdate & QcSqlUpdate::returning(const QcStringList & columns)
{
    m_returning = columns;
    return *this;
}

QcSqlQueryElement & QcSqlUpdate::where(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlUpdate::where_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlUpdate::and_(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlUpdate::and_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlUpdate::or_(const std::string & column)
{
    m_whereElements.push_back({_or_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlUpdate::or_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_or_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

bool QcSqlUpdate::openParenthesis()
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    return true;
}

bool QcSqlUpdate::closeParenthesis()
{
    if (m_whereParenDepth <= 0) {
        return false;
    }

    --m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(_closeParenthesis_)});
    return true;
}

std::string QcSqlUpdate::toSql(QcVariantList & params, QcDbDriver driver) const
{
    std::string sql = "UPDATE " + QcSqlDialect::quoteTableRef(m_table, driver) + " SET ";

    for (std::size_t i = 0; i < m_columns.size(); ++i) {
        if (i > 0) {
            sql += ", ";
        }
        params.push_back(m_columns[i].second);
        sql += QcSqlDialect::quoteIdentifier(m_columns[i].first, driver) + " = " + QcSqlDialect::placeholder(params.size(), driver);
    }

    if (driver == QcDbDriver::MSSQL) {
        // MSSQL's OUTPUT sits between the SET list and WHERE -- everywhere else
        // (RETURNING/RETURNING...INTO, or no clause at all) goes at the very end
        // instead.
        sql += QcSqlDialect::returningClause(m_returning, "inserted", params.size() + 1, driver);
        if (!m_whereElements.empty()) {
            sql += " WHERE " + QcSqlQueryElement::renderChain(m_whereElements, params, driver);
        }
    } else {
        if (!m_whereElements.empty()) {
            sql += " WHERE " + QcSqlQueryElement::renderChain(m_whereElements, params, driver);
        }
        sql += QcSqlDialect::returningClause(m_returning, "inserted", params.size() + 1, driver);
    }

    return sql;
}

QcSqlStatement QcSqlUpdate::toSql() const
{
    return toSql(m_driver);
}

std::string QcSqlUpdate::toSql(QcVariantList & params) const
{
    return toSql(params, m_driver);
}

QcSqlStatement QcSqlUpdate::toSql(QcDbDriver driver) const
{
    QcSqlStatement statement;
    statement.sql = toSql(statement.params, driver);
    // Unconditional -- see the identical comment in QcSqlInsert::toSql().
    statement.returningColumnNames = m_returning;
    if (driver == QcDbDriver::Oracle) {
        // See the identical comment in QcSqlInsert::toSql() -- same OUT-bind
        // contract, read back by QcNativeConnection::executeReturning().
        statement.returningColumnCount = m_returning.size();
    }
    return statement;
}
