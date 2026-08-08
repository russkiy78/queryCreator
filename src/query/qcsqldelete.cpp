#include "qcsqldelete.h"

#include "qcsqldialect.h"

QcSqlDelete::QcSqlDelete()
{
}

QcSqlDelete & QcSqlDelete::useDriver(QcDbDriver driver)
{
    m_driver = driver;
    return *this;
}

QcSqlDelete & QcSqlDelete::from(const std::string & table)
{
    m_table = table;
    return *this;
}

QcSqlDelete & QcSqlDelete::returning(const QcStringList & columns)
{
    m_returning = columns;
    return *this;
}

QcSqlQueryElement & QcSqlDelete::where(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlDelete::where_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlDelete::and_(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlDelete::and_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlDelete::or_(const std::string & column)
{
    m_whereElements.push_back({_or_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlDelete::or_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_or_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

bool QcSqlDelete::openParenthesis()
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    return true;
}

bool QcSqlDelete::closeParenthesis()
{
    if (m_whereParenDepth <= 0) {
        return false;
    }

    --m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(_closeParenthesis_)});
    return true;
}

QcSqlDelete::QcStringList QcSqlDelete::validate() const
{
    QcStringList problems;

    if (m_table.empty()) {
        problems.push_back("DELETE has no target table (call from())");
    }

    if (m_whereParenDepth != 0) {
        problems.push_back("WHERE clause has " + std::to_string(m_whereParenDepth)
            + " unclosed parenthesis group(s) -- call closeParenthesis() once per openParenthesis()/*_OpenParenthesis()");
    }

    const QcStringList whereProblems = QcSqlQueryElement::validateChain(m_whereElements, "WHERE");
    problems.insert(problems.end(), whereProblems.begin(), whereProblems.end());

    return problems;
}

std::string QcSqlDelete::toSql(QcVariantList & params, QcDbDriver driver) const
{
    qcThrowIfQueryInvalid(validate());

    std::string sql = "DELETE FROM " + QcSqlDialect::quoteTableRef(m_table, driver);

    if (driver == QcDbDriver::MSSQL) {
        // MSSQL's OUTPUT sits between the table name and WHERE -- everywhere
        // else (RETURNING/RETURNING...INTO, or no clause at all) goes at the
        // very end instead. "deleted" (not "inserted"): DELETE's OUTPUT/RETURNING
        // only ever has pre-deletion values to offer.
        sql += QcSqlDialect::returningClause(m_returning, "deleted", params.size() + 1, driver);
        if (!m_whereElements.empty()) {
            sql += " WHERE " + QcSqlQueryElement::renderChain(m_whereElements, params, driver);
        }
    } else {
        if (!m_whereElements.empty()) {
            sql += " WHERE " + QcSqlQueryElement::renderChain(m_whereElements, params, driver);
        }
        sql += QcSqlDialect::returningClause(m_returning, "deleted", params.size() + 1, driver);
    }

    return sql;
}

QcSqlStatement QcSqlDelete::toSql() const
{
    return toSql(m_driver);
}

std::string QcSqlDelete::toSql(QcVariantList & params) const
{
    return toSql(params, m_driver);
}

QcSqlStatement QcSqlDelete::toSql(QcDbDriver driver) const
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
