#ifndef QCSQLQUERYVALUE_H
#define QCSQLQUERYVALUE_H

#include "qcdbdriver.h"
#include "qcsqlbase.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QcSqlQuery;
class QcSqlQueryValueWhiteBoxTest;
class QcSqlQueryValue  : public QcSqlBase
{
public:
    QcSqlQueryValue();

    // Parses the "column <alias>" convention used throughout this API (see
    // QcSqlQuery::addReturnValues) — the alias is optional. Used by
    // QcSqlQuery::addReturnValue()/addReturnValues() to build a value that
    // already knows which column it refers to, without exposing a separate
    // setter for private state.
    explicit QcSqlQueryValue(const std::string & name);

    // Sets the dialect toSql() renders for once, up front -- see
    // QcSqlQuery::useDriver()'s doc comment for the full rationale. Rarely
    // needed directly: QcSqlQuery::toSql() already overrides this with its
    // own driver when rendering each of its return values.
    QcSqlQueryValue & useDriver(QcDbDriver driver);

private:
    QcDbDriver m_driver = QcDbDriver::PostgreSQL;

    /* functions chain <type, params> */
    std::vector<std::pair<int,std::vector<QcVariant>>> m_functions;

    /* main */
    std::string m_columnName;
    std::string m_columnAlias;
    QcSqlQuery * m_subQuery = nullptr;

    friend class QcSqlQueryValueWhiteBoxTest;

public:
    /*functions*/
    QcSqlQueryValue & concat(const QcStringList & concateVal);
    QcSqlQueryValue & cast(const int & from, const int & to);
    QcSqlQueryValue & round(const int & precision);
    QcSqlQueryValue & upperCase();
    QcSqlQueryValue & lowerCase();

    // Aggregate functions -- COUNT(expr)/COUNT(DISTINCT expr), SUM(expr),
    // AVG(expr), MIN(expr), MAX(expr). Identical syntax on all five drivers
    // (standard SQL aggregates), so no QcSqlDialect involvement needed.
    // count()'s `distinct` wraps the current expression in "DISTINCT "
    // first. COUNT(*) is reached the ordinary way -- start from
    // QcSqlQueryValue("*") (quoteIdentifier() never quotes the "*"
    // wildcard, see qcsqldialect.h) rather than a separate no-argument
    // overload here.
    QcSqlQueryValue & count(bool distinct = false);
    QcSqlQueryValue & sum();
    QcSqlQueryValue & avg();
    QcSqlQueryValue & min();
    QcSqlQueryValue & max();

    // COALESCE(expr, fallback1, fallback2, ...) -- `fallbacks` follow the
    // same convention as concat()'s operand list: each entry is either a
    // raw SQL expression (typically a column name, quoted automatically if
    // it looks like one -- see QcSqlDialect::quoteRef()) or a literal
    // written out as SQL text by the caller (e.g. "'N/A'"), never a bound
    // parameter -- this class never produces bind parameters, see toSql().
    QcSqlQueryValue & coalesce(const QcStringList & fallbacks);

    // NULLIF(expr, compareExpr) -- compareExpr follows the same
    // raw-expression convention as coalesce()'s fallbacks.
    QcSqlQueryValue & nullIf(const std::string & compareExpr);

    // TRIM(expr) -- ANSI form (leading and trailing whitespace), identical
    // syntax on all five drivers.
    QcSqlQueryValue & trim();

    // SUBSTRING/SUBSTR(expr, start, length) -- 1-based `start`,
    // dialect-dependent function name (see QcSqlDialect::substringExpr()).
    QcSqlQueryValue & substring(int start, int length);

    // LENGTH/LEN(expr) -- dialect-dependent function name and (MSSQL only)
    // trailing-space-trimming behavior, see QcSqlDialect::lengthExpr().
    QcSqlQueryValue & length();

    // REPLACE(expr, search, replacement) -- search/replacement follow the
    // same raw-expression convention as coalesce()'s fallbacks.
    QcSqlQueryValue & replace(const std::string & search, const std::string & replacement);

    // Searched CASE WHEN: `CASE WHEN cond1 THEN result1 [WHEN cond2 THEN
    // result2 ...] [ELSE elseResult] END`. Each cond/result (including
    // elseResult) is a raw SQL expression, run through quoteRef() the same
    // way concat()'s/coalesce()'s operands are -- a bare column name in a
    // result position is quoted automatically, a full boolean condition or
    // a literal written out as SQL text is left alone. Unlike every other
    // function above, this one does *not* wrap the value's current
    // expression -- a searched CASE has no single "subject" column to wrap
    // (contrast ANSI SQL's *simple* CASE, `CASE expr WHEN val THEN ...`,
    // which this method does not implement) -- so calling it discards
    // whatever the chain had accumulated so far and starts a fresh
    // expression. Intended as the first (and typically only) call in a
    // chain -- call it on a default-constructed QcSqlQueryValue, or before
    // any other function, to avoid a surprise silent discard.
    QcSqlQueryValue & case_(const std::vector<std::pair<std::string, std::string>> & whenThenPairs,
                            const std::optional<std::string> & elseResult = std::nullopt);

    // EXTRACT(part FROM expr)-equivalent (QcSqlBase::datePartTypes) -- see
    // QcSqlDialect::extractExpr() for the per-driver rendering.
    QcSqlQueryValue & extract(int part);

    // Adds `amount` of the given date/time component
    // (QcSqlBase::datePartTypes, negative `amount` subtracts) to expr --
    // see QcSqlDialect::dateAddExpr() for the per-driver rendering, the
    // most dialect-divergent function here.
    QcSqlQueryValue & dateAdd(int part, int amount);

    // Pulls a value out of a JSON column at `jsonSearchPath`
    // (QcSqlDialect::jsonExtractExpr()'s path convention -- "a.b[2].c", no
    // leading root marker) and wraps the current chain expression with it,
    // the same as every other function above -- e.g.
    // `addReturnValue("metadata <level>").jsonExtract("level")`. Mirrors the
    // isXxxJson...() family on QcSqlQueryElement (qcsqlqueryelement.h),
    // which uses the exact same QcSqlDialect::jsonExtractExpr() to compare
    // instead of select. Three variants pick how the extracted value comes
    // back (QcSqlBase::jsonValueKind):
    //   - jsonExtract(): scalar text, unquoted (a JSON string "admin" comes
    //     back as the bare text admin) -- the common case.
    //   - jsonExtractNumber(): coerced to a numeric SQL type, so ordering/
    //     arithmetic on the selected column is numeric, not lexicographic.
    //   - jsonExtractRaw(): the value's own JSON-encoded text, unwrapped (an
    //     array/object keeps its brackets/braces) -- for selecting a JSON
    //     fragment (an array or object) rather than one scalar.
    QcSqlQueryValue & jsonExtract(const std::string & jsonSearchPath);
    QcSqlQueryValue & jsonExtractNumber(const std::string & jsonSearchPath);
    QcSqlQueryValue & jsonExtractRaw(const std::string & jsonSearchPath);

    // Renders this SELECT expression (column, wrapped by its function chain
    // in call order, plus " AS alias" if one was given). No bind parameters
    // are ever produced here -- concat()'s QcStringList and cast()/round()'s
    // int operands are all rendered as literal SQL text, not data (see
    // qcsqlqueryvalue.cpp for why). Uses the dialect useDriver() configured;
    // the explicit-driver overload renders for `driver` instead -- see
    // QcSqlQuery::toSql()'s doc comment for the full rationale.
    std::string toSql() const;
    std::string toSql(QcDbDriver driver) const;
};

#endif // QCSQLQUERYVALUE_H
