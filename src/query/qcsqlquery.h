#ifndef QCSQLQUERY_H
#define QCSQLQUERY_H

#include "qcdbdriver.h"
#include "qcdeepptr.h"
#include "qcsqlbase.h"
#include "qcsqlqueryelement.h"
#include "qcsqlqueryvalue.h"
#include <string>
#include <utility>
#include <vector>

class QcSqlQueryWhiteBoxTest;
class QcSqlQuery : public QcSqlBase
{
public:
    QcSqlQuery();

    // Sets the dialect toSql()/toSql(QcVariantList&) render for once, up
    // front -- so a query built for a specific driver doesn't need that
    // driver repeated on every toSql() call (see the explicit-driver
    // overloads below for the cases that still want one: rendering the same
    // query for more than one driver, or QueryCreator's own automatic
    // per-pool driver threading). Defaults to QcDbDriver::PostgreSQL,
    // matching every other default in this API.
    QcSqlQuery & useDriver(QcDbDriver driver);

    // Raw SQL condition AND-ed onto the WHERE clause (an escape hatch for
    // predicates the builder has no dedicated method for). Write '?' for
    // each value in `values`, in order, regardless of the target driver's
    // native placeholder syntax -- toSql() renumbers each '?'
    // into the correctly-positioned driver placeholder when this fragment
    // is stitched into the rest of the statement (its own hardcoded "$1" or
    // similar would collide with placeholders from other conditions).
    QcSqlQuery & addFreeText(const std::string & text, const QcVariantList & values);

    /* return values*/
    QcSqlQuery & distinct();
    QcSqlQuery & distinctOn(const QcStringList & columns);
    bool addReturnValues(const QcStringList & columns);
    QcSqlQueryValue & addReturnValue(const std::string & name);


    /*subqueries*/
    QcSqlQuery & fromTable(const std::string & name);
    QcSqlQuery & fromSubQuery(const std::string & relationKeyOrAlias, const QcSqlQuery & joinQuery);

    /*common table expressions*/
    QcSqlQuery & with_(const std::string & name, const QcSqlQuery & cteQuery);

    /* join */
    QcSqlQuery & addLeftJoin(const std::string & alias, const QcSqlQuery & joinQuery, const std::string & onCondition, const bool & asSubQuery = false);
    QcSqlQuery & addJoin(const std::string & alias, const QcSqlQuery & joinQuery, const std::string & onCondition, const bool & asSubQuery = false);
    QcSqlQuery & addRightJoin(const std::string & alias, const QcSqlQuery & joinQuery, const std::string & onCondition, const bool & asSubQuery = false);
    QcSqlQuery & addFullJoin(const std::string & alias, const QcSqlQuery & joinQuery, const std::string & onCondition, const bool & asSubQuery = false);
    QcSqlQuery & addCrossJoin(const std::string & alias, const QcSqlQuery & joinQuery, const bool & asSubQuery = false);

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


    /*order-group*/
    // `nulls` (a QcSqlBase::nullsPosition) applies to every column in this
    // call -- to mix NULLS FIRST/LAST/default across columns within one
    // ORDER BY, call orderAsc()/orderDesc() once per column instead of
    // batching them into a single call. See QcSqlDialect::orderByEntry() for
    // per-driver rendering, including the two (MySQL/MSSQL) with no native
    // NULLS FIRST/LAST syntax.
    QcSqlQuery & orderAsc(const QcStringList & columns, int nulls = _nullsDefault_);
    QcSqlQuery & orderDesc(const QcStringList & columns, int nulls = _nullsDefault_);
    QcSqlQuery & groupBy(const QcStringList & columns);
    QcSqlQuery & limit(int rowsCount, int startRow = 0, bool withTies = false);

    /*having (post-aggregate conditions)*/
    QcSqlQueryElement & having(const std::string & column);
    QcSqlQueryElement & and_Having(const std::string & column);
    QcSqlQueryElement & or_Having(const std::string & column);

    /*set operations*/
    QcSqlQuery & unionWith(const QcSqlQuery & query);
    QcSqlQuery & unionAllWith(const QcSqlQuery & query);
    QcSqlQuery & intersectWith(const QcSqlQuery & query);
    QcSqlQuery & exceptWith(const QcSqlQuery & query);

    // Checks this query's own directly-held state (not any nested subquery's
    // -- FROM/JOIN/WHERE-subquery/CTE/set-operation members validate
    // themselves independently the moment their own toSql() runs, whether
    // called directly or as part of this query rendering around them) for
    // structural gaps that would otherwise make toSql() silently emit broken
    // SQL: a non-CROSS JOIN with no ON condition, an openParenthesis()/
    // *_OpenParenthesis() left unclosed, or a where()/and_()/or_()/having()/
    // and_Having()/or_Having() call whose returned element never got a
    // comparator chained onto it (see QcSqlQueryElement::isIncomplete()).
    // Returns one human-readable problem string per issue found, empty if
    // none -- toSql() calls this itself and throws QcQueryBuildError built
    // from the result, but it's public so a caller can check "is this query
    // well-formed yet?" without a try/catch. Deliberately not a check
    // against a real schema (no such thing exists here) -- a WHERE
    // referencing a column that doesn't exist, or a JOIN alias nothing else
    // references, are not flagged; only gaps in the query's own internal
    // structure are.
    QcStringList validate() const;

    /*SQL generation*/

    // Top-level entry point: renders this query as a full, standalone
    // statement with its own fresh parameter list, using the dialect
    // useDriver() configured (QcDbDriver::PostgreSQL if it was never
    // called). The explicit-driver overload renders for `driver` instead,
    // ignoring whatever useDriver() set -- used internally when this query
    // is rendered as a subquery (the *outer* statement's driver always
    // wins, see toSql(QcVariantList&, QcDbDriver) below) and by callers that
    // want one query's SQL for more than one driver (e.g. tests).
    QcSqlStatement toSql() const;
    QcSqlStatement toSql(QcDbDriver driver) const;

    // Renders this query as SQL text, appending its bind values onto
    // `params` (a *shared* list threaded through the whole statement) rather
    // than starting a fresh one -- this is what makes placeholder numbering
    // correct for $N/:N-style dialects when this query is embedded as a
    // subquery (FROM/JOIN/WHERE/CTE/set-operation) inside another one: each
    // nested toSql() call continues numbering from where the caller left
    // off instead of restarting at 1. Returns bare `SELECT ...` text with no
    // wrapping parentheses -- the embedding context (JOIN/WHERE/CTE/set
    // operation rendering) adds those itself, since the same bare text is
    // also needed unwrapped for `WITH name AS (...)`.
    std::string toSql(QcVariantList & params) const;
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;

private:

    // Logical connector glueing a WHERE/HAVING element to whatever came
    // before it in the chain; meaningless (never rendered) for the first
    // element of a chain.
    enum LogicalConnector { _and_, _or_ };
    // Direction tag for m_orderBy entries.
    enum SortDirection { _ascending_, _descending_ };

    struct JoinClause
    {
        int type = _innerJoin_;
        std::string alias;
        QcDeepPtr<QcSqlQuery> query;
        std::string onCondition;
        bool asSubQuery = false;
    };

    struct CteClause
    {
        std::string name;
        QcDeepPtr<QcSqlQuery> query;
    };

    struct SetOperation
    {
        int type = _union_;
        QcDeepPtr<QcSqlQuery> query;
    };

    struct FreeTextClause
    {
        std::string text;
        QcVariantList values;
    };

    struct OrderByClause
    {
        int direction = _ascending_; // SortDirection
        int nulls = _nullsDefault_; // QcSqlBase::nullsPosition
        std::string column;
    };

    // See useDriver()'s doc comment above.
    QcDbDriver m_driver = QcDbDriver::PostgreSQL;

    // Doubles as the subquery's alias when m_fromSubquery is set (FROM has
    // exactly one source — a table name or an aliased subquery — never
    // both), so a separate alias field isn't needed.
    std::string m_fromTable;
    QcDeepPtr<QcSqlQuery> m_fromSubquery;

    bool m_distinct = false;
    QcStringList m_distinctOn;

    std::vector<CteClause> m_ctes;
    std::vector<JoinClause> m_joins;
    std::vector<FreeTextClause> m_freeTextFragments;

    std::vector<QcSqlQueryValue> m_values;
    std::vector<std::pair<int,QcSqlQueryElement>> m_whereElements;
    std::vector<std::pair<int,QcSqlQueryElement>> m_havingElements;
    std::vector<OrderByClause> m_orderBy;
    std::vector<std::pair<int,std::string>> m_groupBy; // int: reserved (e.g. future GROUPING SETS/ROLLUP), always 0 for now
    int m_limitFrom = 0;
    int m_limitTo = 0;
    bool m_limitWithTies = false;
    // Open/close balance for the WHERE chain's openParenthesis()/
    // closeParenthesis() and the *_OpenParenthesis() sugar methods.
    int m_whereParenDepth = 0;

    std::vector<SetOperation> m_setOperations;

    // Substitutes each '?' in a FreeTextClause's raw text (in order) with
    // the correctly-numbered dialect placeholder for the *global* parameter
    // position, and appends the corresponding value to `params` -- see
    // qcsqlquery.cpp for why '?' (not the driver's native placeholder
    // syntax) is what addFreeText()'s callers are expected to write.
    std::string renderFreeText(const FreeTextClause & clause, QcVariantList & params, QcDbDriver driver) const;

    friend class QcSqlQueryWhiteBoxTest;

};



#endif // QCSQLQUERY_H
