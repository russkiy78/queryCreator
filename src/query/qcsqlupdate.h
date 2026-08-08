#ifndef QCSQLUPDATE_H
#define QCSQLUPDATE_H

#include "qcdbdriver.h"
#include "qcsqlbase.h"
// Not just qcsqlqueryelement.h: m_whereElements holds QcSqlQueryElement by
// value, whose m_subQuery is a QcDeepPtr<QcSqlQuery> -- constructing/
// destroying it needs QcSqlQuery to be a complete type. qcsqlqueryelement.h
// only forward-declares QcSqlQuery (to avoid a cycle with qcsqlquery.h,
// which includes it) -- QcSqlUpdate has no such cycle, so it can and should
// pull in the full header itself rather than pushing this requirement onto
// every .cpp that constructs a QcSqlUpdate.
#include "qcsqlquery.h"
#include <string>
#include <utility>
#include <vector>

// UPDATE builder: `UPDATE table SET col1 = ?, col2 = ? WHERE ...`.
//
// The WHERE side reuses QcSqlQueryElement -- the same comparator vocabulary
// (isEqualTo/isLike/isIn/isBetween/isNull/isDistinctFrom/exists/...) and
// AND/OR/parenthesis-group chaining QcSqlQuery's WHERE clause has (see
// qcsqlquery.h) -- so this class only owns the SET-list and WHERE-chain
// state, not a second implementation of condition rendering.
class QcSqlUpdateWhiteBoxTest;
class QcSqlUpdate : public QcSqlBase
{
public:
    QcSqlUpdate();

    // Sets the dialect toSql() renders for once, up front -- see
    // QcSqlQuery::useDriver()'s doc comment for the full rationale.
    QcSqlUpdate & useDriver(QcDbDriver driver);

    QcSqlUpdate & table(const std::string & name);

    // Sets column = value to assign in SET. A later set() for a column
    // already set overwrites its value in place -- see
    // QcSqlInsert::set() for the same rationale.
    QcSqlUpdate & set(const std::string & column, const QcVariant & value);

    // Requests the listed columns back from each updated row -- see
    // QcSqlInsert::returning() / QcSqlDialect::returningClause() for the
    // full per-driver rundown.
    QcSqlUpdate & returning(const QcStringList & columns);

    /*conditions*/
    QcSqlQueryElement & where(const std::string & column);
    QcSqlQueryElement & where_OpenParenthesis(const std::string & column);
    QcSqlQueryElement & and_(const std::string & column);
    QcSqlQueryElement & and_OpenParenthesis(const std::string & column);
    QcSqlQueryElement & or_(const std::string & column);
    QcSqlQueryElement & or_OpenParenthesis(const std::string & column);

    /*parenthesis*/
    bool openParenthesis();
    bool closeParenthesis();

    /*SQL generation*/
    QcSqlStatement toSql() const;
    QcSqlStatement toSql(QcDbDriver driver) const;
    std::string toSql(QcVariantList & params) const;
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;

private:
    // Same connector encoding QcSqlQuery::renderConditionChain's callers
    // used to rely on (0 = AND, 1 = OR) -- see
    // QcSqlQueryElement::renderChain's doc comment in qcsqlqueryelement.h.
    enum LogicalConnector { _and_, _or_ };

    QcDbDriver m_driver = QcDbDriver::PostgreSQL;
    std::string m_table;
    std::vector<std::pair<std::string, QcVariant>> m_columns;
    QcStringList m_returning;
    std::vector<std::pair<int, QcSqlQueryElement>> m_whereElements;
    // Open/close balance for openParenthesis()/closeParenthesis() and the
    // *_OpenParenthesis() sugar methods -- mirrors QcSqlQuery::m_whereParenDepth.
    int m_whereParenDepth = 0;

    friend class QcSqlUpdateWhiteBoxTest;
};

#endif // QCSQLUPDATE_H
