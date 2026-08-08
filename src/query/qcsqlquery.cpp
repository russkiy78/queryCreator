#include "qcsqlquery.h"

#include "qcsqldialect.h"

namespace {

std::string joinKeyword(int type)
{
    switch (type) {
        case QcSqlBase::_leftJoin_: return "LEFT JOIN";
        case QcSqlBase::_rightJoin_: return "RIGHT JOIN";
        case QcSqlBase::_fullJoin_: return "FULL JOIN";
        case QcSqlBase::_crossJoin_: return "CROSS JOIN";
        case QcSqlBase::_innerJoin_:
        default:
            return "JOIN";
    }
}

std::string setOperationKeyword(int type)
{
    switch (type) {
        case QcSqlBase::_unionAll_: return "UNION ALL";
        case QcSqlBase::_intersect_: return "INTERSECT";
        case QcSqlBase::_except_: return "EXCEPT";
        case QcSqlBase::_union_:
        default:
            return "UNION";
    }
}

// Separator placed between a FROM-subquery/JOIN source and its alias.
// Oracle is the one driver here that rejects the ANSI "AS" keyword for a
// *table*/subquery alias outright (verified directly: `FROM dual AS d` ->
// ORA-00933; `FROM dual d` is required instead) -- unlike a *column* alias
// in the SELECT list, where "AS" is accepted everywhere including Oracle
// (see QcSqlQueryValue::toSql(), unaffected by this). WITH-clause CTE
// naming (`WITH name AS (...)`) is also unaffected -- verified directly
// that Oracle accepts "AS" there.
std::string tableAliasSeparator(QcDbDriver driver)
{
    return (driver == QcDbDriver::Oracle) ? " " : " AS ";
}

} // namespace

QcSqlQuery::QcSqlQuery()
{

}

QcSqlQuery & QcSqlQuery::useDriver(QcDbDriver driver)
{
    m_driver = driver;
    return *this;
}

QcSqlQuery & QcSqlQuery::addFreeText(const std::string & text, const QcVariantList & values)
{
    m_freeTextFragments.push_back({text, values});
    return *this;
}

QcSqlQuery & QcSqlQuery::distinct()
{
    m_distinct = true;
    return *this;
}

QcSqlQuery & QcSqlQuery::distinctOn(const QcStringList & columns)
{
    m_distinct = true;
    m_distinctOn = columns;
    return *this;
}

bool QcSqlQuery::addReturnValues(const QcStringList & columns)
{
    for (const std::string & column : columns) {
        m_values.emplace_back(column);
    }

    return true;
}

QcSqlQueryValue & QcSqlQuery::addReturnValue(const std::string & name)
{
    m_values.emplace_back(name);
    return m_values.back();
}

QcSqlQuery & QcSqlQuery::fromTable(const std::string & name)
{
    m_fromTable = name;
    m_fromSubquery = nullptr;
    return *this;
}

QcSqlQuery & QcSqlQuery::fromSubQuery(const std::string & relationKeyOrAlias, const QcSqlQuery & joinQuery)
{
    // m_fromTable doubles as the subquery's alias here -- FROM has exactly
    // one source, so the "table name" slot is free to reuse (see qcsqlquery.h).
    m_fromTable = relationKeyOrAlias;
    m_fromSubquery = QcDeepPtr<QcSqlQuery>(joinQuery);
    return *this;
}

QcSqlQuery & QcSqlQuery::with_(const std::string & name, const QcSqlQuery & cteQuery)
{
    m_ctes.push_back({name, QcDeepPtr<QcSqlQuery>(cteQuery)});
    return *this;
}

QcSqlQuery & QcSqlQuery::addLeftJoin(const std::string & alias, const QcSqlQuery & joinQuery, const std::string & onCondition, const bool & asSubQuery)
{
    m_joins.push_back({_leftJoin_, alias, QcDeepPtr<QcSqlQuery>(joinQuery), onCondition, asSubQuery});
    return *this;
}

QcSqlQuery & QcSqlQuery::addJoin(const std::string & alias, const QcSqlQuery & joinQuery, const std::string & onCondition, const bool & asSubQuery)
{
    m_joins.push_back({_innerJoin_, alias, QcDeepPtr<QcSqlQuery>(joinQuery), onCondition, asSubQuery});
    return *this;
}

QcSqlQuery & QcSqlQuery::addRightJoin(const std::string & alias, const QcSqlQuery & joinQuery, const std::string & onCondition, const bool & asSubQuery)
{
    m_joins.push_back({_rightJoin_, alias, QcDeepPtr<QcSqlQuery>(joinQuery), onCondition, asSubQuery});
    return *this;
}

QcSqlQuery & QcSqlQuery::addFullJoin(const std::string & alias, const QcSqlQuery & joinQuery, const std::string & onCondition, const bool & asSubQuery)
{
    m_joins.push_back({_fullJoin_, alias, QcDeepPtr<QcSqlQuery>(joinQuery), onCondition, asSubQuery});
    return *this;
}

QcSqlQuery & QcSqlQuery::addCrossJoin(const std::string & alias, const QcSqlQuery & joinQuery, const bool & asSubQuery)
{
    // CROSS JOIN has no ON condition.
    m_joins.push_back({_crossJoin_, alias, QcDeepPtr<QcSqlQuery>(joinQuery), std::string(), asSubQuery});
    return *this;
}

QcSqlQueryElement & QcSqlQuery::where(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlQuery::where_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlQuery::and_(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlQuery::and_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlQuery::or_(const std::string & column)
{
    m_whereElements.push_back({_or_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

QcSqlQueryElement & QcSqlQuery::or_OpenParenthesis(const std::string & column)
{
    m_whereElements.push_back({_or_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_whereElements.back().second;
}

bool QcSqlQuery::openParenthesis()
{
    m_whereElements.push_back({_and_, QcSqlQueryElement(_openParenthesis_)});
    ++m_whereParenDepth;
    return true;
}

bool QcSqlQuery::closeParenthesis()
{
    if (m_whereParenDepth <= 0) {
        return false;
    }

    --m_whereParenDepth;
    m_whereElements.push_back({_and_, QcSqlQueryElement(_closeParenthesis_)});
    return true;
}

QcSqlQuery & QcSqlQuery::orderAsc(const QcStringList & columns, int nulls)
{
    for (const std::string & column : columns) {
        m_orderBy.push_back({_ascending_, nulls, column});
    }
    return *this;
}

QcSqlQuery & QcSqlQuery::orderDesc(const QcStringList & columns, int nulls)
{
    for (const std::string & column : columns) {
        m_orderBy.push_back({_descending_, nulls, column});
    }
    return *this;
}

QcSqlQuery & QcSqlQuery::groupBy(const QcStringList & columns)
{
    for (const std::string & column : columns) {
        m_groupBy.push_back({0, column});
    }
    return *this;
}

QcSqlQuery & QcSqlQuery::limit(int rowsCount, int startRow, bool withTies)
{
    m_limitFrom = startRow;
    m_limitTo = rowsCount;
    m_limitWithTies = withTies;
    return *this;
}

QcSqlQueryElement & QcSqlQuery::having(const std::string & column)
{
    m_havingElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_havingElements.back().second;
}

QcSqlQueryElement & QcSqlQuery::and_Having(const std::string & column)
{
    m_havingElements.push_back({_and_, QcSqlQueryElement(column)});
    return m_havingElements.back().second;
}

QcSqlQueryElement & QcSqlQuery::or_Having(const std::string & column)
{
    m_havingElements.push_back({_or_, QcSqlQueryElement(column)});
    return m_havingElements.back().second;
}

QcSqlQuery & QcSqlQuery::unionWith(const QcSqlQuery & query)
{
    m_setOperations.push_back({_union_, QcDeepPtr<QcSqlQuery>(query)});
    return *this;
}

QcSqlQuery & QcSqlQuery::unionAllWith(const QcSqlQuery & query)
{
    m_setOperations.push_back({_unionAll_, QcDeepPtr<QcSqlQuery>(query)});
    return *this;
}

QcSqlQuery & QcSqlQuery::intersectWith(const QcSqlQuery & query)
{
    m_setOperations.push_back({_intersect_, QcDeepPtr<QcSqlQuery>(query)});
    return *this;
}

QcSqlQuery & QcSqlQuery::exceptWith(const QcSqlQuery & query)
{
    m_setOperations.push_back({_except_, QcDeepPtr<QcSqlQuery>(query)});
    return *this;
}

std::string QcSqlQuery::renderFreeText(const FreeTextClause & clause, QcVariantList & params, QcDbDriver driver) const
{
    std::string sql;
    std::size_t valueIndex = 0;

    for (char ch : clause.text) {
        if (ch == '?' && valueIndex < clause.values.size()) {
            params.push_back(clause.values[valueIndex]);
            ++valueIndex;
            sql += QcSqlDialect::placeholder(params.size(), driver);
        } else {
            sql += ch;
        }
    }

    return sql;
}

QcSqlQuery::QcStringList QcSqlQuery::validate() const
{
    QcStringList problems;

    for (const JoinClause & join : m_joins) {
        if (join.type != _crossJoin_ && join.onCondition.empty()) {
            problems.push_back("JOIN \"" + join.alias + "\" has no ON condition (use addCrossJoin() instead if a cross join was intended)");
        }
    }

    if (m_whereParenDepth != 0) {
        problems.push_back("WHERE clause has " + std::to_string(m_whereParenDepth)
            + " unclosed parenthesis group(s) -- call closeParenthesis() once per openParenthesis()/*_OpenParenthesis()");
    }

    const QcStringList whereProblems = QcSqlQueryElement::validateChain(m_whereElements, "WHERE");
    problems.insert(problems.end(), whereProblems.begin(), whereProblems.end());

    const QcStringList havingProblems = QcSqlQueryElement::validateChain(m_havingElements, "HAVING");
    problems.insert(problems.end(), havingProblems.begin(), havingProblems.end());

    return problems;
}

std::string QcSqlQuery::toSql(QcVariantList & params, QcDbDriver driver) const
{
    qcThrowIfQueryInvalid(validate());

    std::string sql;

    if (!m_ctes.empty()) {
        sql += "WITH ";
        for (std::size_t i = 0; i < m_ctes.size(); ++i) {
            if (i > 0) {
                sql += ", ";
            }
            sql += QcSqlDialect::quoteIdentifier(m_ctes[i].name, driver) + " AS (" + m_ctes[i].query->toSql(params, driver) + ")";
        }
        sql += " ";
    }

    sql += "SELECT ";
    if (m_distinct) {
        sql += "DISTINCT ";
        // DISTINCT ON is PostgreSQL-only -- other dialects have no
        // equivalent, so distinctOn() degrades to plain DISTINCT there
        // (see qcsqlquery.h / architecture.md).
        if (driver == QcDbDriver::PostgreSQL && !m_distinctOn.empty()) {
            sql += "ON (";
            for (std::size_t i = 0; i < m_distinctOn.size(); ++i) {
                if (i > 0) {
                    sql += ", ";
                }
                sql += QcSqlDialect::quoteRef(m_distinctOn[i], driver);
            }
            sql += ") ";
        }
    }

    if (m_values.empty()) {
        sql += "*";
    } else {
        for (std::size_t i = 0; i < m_values.size(); ++i) {
            if (i > 0) {
                sql += ", ";
            }
            sql += m_values[i].toSql(driver);
        }
    }

    if (m_fromSubquery) {
        sql += " FROM (" + m_fromSubquery->toSql(params, driver) + ")" + tableAliasSeparator(driver) + QcSqlDialect::quoteIdentifier(m_fromTable, driver);
    } else if (!m_fromTable.empty()) {
        sql += " FROM " + QcSqlDialect::quoteTableRef(m_fromTable, driver);
    }

    for (const JoinClause & join : m_joins) {
        sql += " " + joinKeyword(join.type) + " ";
        // asSubQuery decides how `join.query` (always present -- every
        // addXJoin() takes a QcSqlQuery, even for a plain table join) is
        // read: as a literal table name (its own m_fromTable, unwrapped) or
        // as a real correlated subquery rendered in full.
        if (join.asSubQuery) {
            sql += "(" + join.query->toSql(params, driver) + ")";
        } else {
            sql += QcSqlDialect::quoteTableRef(join.query->m_fromTable, driver);
        }
        sql += tableAliasSeparator(driver) + QcSqlDialect::quoteIdentifier(join.alias, driver);
        if (!join.onCondition.empty()) {
            sql += " ON " + join.onCondition;
        }
    }

    std::string whereSql = m_whereElements.empty() ? std::string() : QcSqlQueryElement::renderChain(m_whereElements, params, driver);
    for (const FreeTextClause & clause : m_freeTextFragments) {
        const std::string fragment = renderFreeText(clause, params, driver);
        if (!whereSql.empty()) {
            whereSql += " AND ";
        }
        whereSql += fragment;
    }
    if (!whereSql.empty()) {
        sql += " WHERE " + whereSql;
    }

    if (!m_groupBy.empty()) {
        sql += " GROUP BY ";
        for (std::size_t i = 0; i < m_groupBy.size(); ++i) {
            if (i > 0) {
                sql += ", ";
            }
            sql += QcSqlDialect::quoteRef(m_groupBy[i].second, driver);
        }
    }

    if (!m_havingElements.empty()) {
        sql += " HAVING " + QcSqlQueryElement::renderChain(m_havingElements, params, driver);
    }

    if (!m_orderBy.empty()) {
        sql += " ORDER BY ";
        for (std::size_t i = 0; i < m_orderBy.size(); ++i) {
            if (i > 0) {
                sql += ", ";
            }
            const OrderByClause & clause = m_orderBy[i];
            sql += QcSqlDialect::orderByEntry(QcSqlDialect::quoteRef(clause.column, driver), clause.direction == _descending_, clause.nulls, driver);
        }
    }

    sql += QcSqlDialect::limitOffsetClause(m_limitTo, m_limitFrom, m_limitWithTies, driver);

    for (const SetOperation & op : m_setOperations) {
        if (driver == QcDbDriver::PostgreSQL || driver == QcDbDriver::MySQL) {
            // Each side of a set operation is explicitly parenthesized: cheap
            // insurance against vendor-specific precedence differences between
            // UNION/EXCEPT vs INTERSECT, and what lets a side keep its own
            // ORDER BY/LIMIT unambiguously (see architecture.md for the drivers
            // where this is known not to hold, e.g. non-final-SELECT ORDER BY
            // on MSSQL). Only PostgreSQL/MySQL accept a parenthesized member
            // query here.
            sql = "(" + sql + ") " + setOperationKeyword(op.type) + " (" + op.query->toSql(params, driver) + ")";
        } else {
            // SQLite rejects "(SELECT ...) UNION (SELECT ...)" outright
            // (verified directly: "Error: in prepare, near '(': syntax error")
            // -- MSSQL/Oracle don't accept a parenthesized compound-select
            // member either, per their documented grammar. Bare, unparenthesized
            // members are what all three actually require.
            sql = sql + " " + setOperationKeyword(op.type) + " " + op.query->toSql(params, driver);
        }
    }

    return sql;
}

QcSqlStatement QcSqlQuery::toSql() const
{
    return toSql(m_driver);
}

QcSqlStatement QcSqlQuery::toSql(QcDbDriver driver) const
{
    QcSqlStatement statement;
    statement.sql = toSql(statement.params, driver);
    return statement;
}

std::string QcSqlQuery::toSql(QcVariantList & params) const
{
    return toSql(params, m_driver);
}
