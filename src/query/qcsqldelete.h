#ifndef QCSQLDELETE_H
#define QCSQLDELETE_H

#include "qcdbdriver.h"
#include "qcsqlbase.h"
// See the equivalent comment in qcsqlupdate.h: QcSqlQuery must be a complete
// type wherever a QcSqlDelete (whose m_whereElements holds QcSqlQueryElement
// by value) is constructed or destroyed.
#include "qcsqlquery.h"
#include <string>
#include <utility>
#include <vector>

// DELETE builder: `DELETE FROM table WHERE ...`.
//
// The WHERE side reuses QcSqlQueryElement, same as QcSqlUpdate -- see
// qcsqlupdate.h.
class QcSqlDeleteWhiteBoxTest;
class QcSqlDelete : public QcSqlBase
{
public:
    QcSqlDelete();

    // Sets the dialect toSql() renders for once, up front -- see
    // QcSqlQuery::useDriver()'s doc comment for the full rationale.
    QcSqlDelete & useDriver(QcDbDriver driver);

    QcSqlDelete & from(const std::string & table);

    // Requests the listed columns back from each deleted row -- see
    // QcSqlInsert::returning() / QcSqlDialect::returningClause() for the
    // full per-driver rundown.
    QcSqlDelete & returning(const QcStringList & columns);

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
    enum LogicalConnector { _and_, _or_ };

    QcDbDriver m_driver = QcDbDriver::PostgreSQL;
    std::string m_table;
    QcStringList m_returning;
    std::vector<std::pair<int, QcSqlQueryElement>> m_whereElements;
    int m_whereParenDepth = 0;

    friend class QcSqlDeleteWhiteBoxTest;
};

#endif // QCSQLDELETE_H
