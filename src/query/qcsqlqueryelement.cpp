#include "qcsqlqueryelement.h"

#include "qcsqldialect.h"
#include "qcsqlquery.h"

QcSqlQueryElement::QcSqlQueryElement()
{
}

QcSqlQueryElement::QcSqlQueryElement(const std::string & columnName)
    : m_columnName(columnName)
{
}

QcSqlQueryElement::QcSqlQueryElement(const int & compareType)
    : m_compareType(compareType)
{
}

QcSqlQueryElement::QcSqlQueryElement(const int & compareType, const std::string & columnName, const QcVariant & val)
    : m_compareType(compareType)
    , m_columnName(columnName)
    , m_value(val)
{
}

QcSqlQueryElement::QcSqlQueryElement(const int & compareType, const std::string & columnName, const QcSqlQuery & subQuery)
    : m_compareType(compareType)
    , m_columnName(columnName)
    , m_subQuery(subQuery)
{
}

QcSqlQueryElement & QcSqlQueryElement::useDriver(QcDbDriver driver)
{
    m_driver = driver;
    return *this;
}

void QcSqlQueryElement::resetOperands()
{
    m_value = QcVariant{};
    m_values.clear();
    m_subQuery = nullptr;
    m_isJsonComparison = false;
    m_jsonValueKind = _jsonAsText_;
    m_jsonSearchPath.clear();
}

bool QcSqlQueryElement::compareJson(int compareType, const QcVariant & val, const std::string & jsonSearchPath, int kind)
{
    resetOperands();
    m_compareType = compareType;
    m_value = val;
    m_isJsonComparison = true;
    m_jsonValueKind = kind;
    m_jsonSearchPath = jsonSearchPath;
    return true;
}

bool QcSqlQueryElement::isLike(const std::string & val)
{
    resetOperands();
    m_compareType = _isLike_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isIlike(const std::string & val)
{
    resetOperands();
    m_compareType = _isILike_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isNotLike(const std::string & val)
{
    resetOperands();
    m_compareType = _isNotLike_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isNotILike(const std::string & val)
{
    resetOperands();
    m_compareType = _isNotILike_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isEqualTo(const QcVariant & val)
{
    resetOperands();
    m_compareType = _isEqualTo_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isEqualTo(const QcSqlQuery & subQuery)
{
    resetOperands();
    m_compareType = _isEqualTo_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::isNotEqualTo(const QcVariant & val)
{
    resetOperands();
    m_compareType = _isNotEqualTo_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isNotEqualTo(const QcSqlQuery & subQuery)
{
    resetOperands();
    m_compareType = _isNotEqualTo_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::isGreaterThan(const QcVariant & val)
{
    resetOperands();
    m_compareType = _isGreaterThan_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isGreaterThanOrEqualTo(const QcVariant & val)
{
    resetOperands();
    m_compareType = _isGreaterThanOrEqualTo_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isLessThan(const QcVariant & val)
{
    resetOperands();
    m_compareType = _isLessThan_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isLessThanOrEqualTo(const QcVariant & val)
{
    resetOperands();
    m_compareType = _isLessThanOrEqualTo_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isNull()
{
    resetOperands();
    m_compareType = _isNull_;
    return true;
}

bool QcSqlQueryElement::isNotNull()
{
    resetOperands();
    m_compareType = _isNotNull_;
    return true;
}

bool QcSqlQueryElement::isIn(const QcVariantList & val)
{
    resetOperands();
    m_compareType = _isIn_;
    m_values = val;
    return true;
}

bool QcSqlQueryElement::isIn(const QcSqlQuery & subQuery)
{
    resetOperands();
    m_compareType = _isIn_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::isNotIn(const QcVariantList & val)
{
    resetOperands();
    m_compareType = _isNotIn_;
    m_values = val;
    return true;
}

bool QcSqlQueryElement::isNotIn(const QcSqlQuery & subQuery)
{
    resetOperands();
    m_compareType = _isNotIn_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::isBetween(const QcVariant & val1, const QcVariant & val2)
{
    resetOperands();
    m_compareType = _isBetween_;
    m_values = {val1, val2};
    return true;
}

bool QcSqlQueryElement::isNotBetween(const QcVariant & val1, const QcVariant & val2)
{
    resetOperands();
    m_compareType = _isNotBetween_;
    m_values = {val1, val2};
    return true;
}

bool QcSqlQueryElement::isDistinctFrom(const QcVariant & val)
{
    resetOperands();
    m_compareType = _isDistinctFrom_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isDistinctFrom(const QcSqlQuery & subQuery)
{
    resetOperands();
    m_compareType = _isDistinctFrom_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::isNotDistinctFrom(const QcVariant & val)
{
    resetOperands();
    m_compareType = _isNotDistinctFrom_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::isNotDistinctFrom(const QcSqlQuery & subQuery)
{
    resetOperands();
    m_compareType = _isNotDistinctFrom_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::exists(const QcSqlQuery & subQuery)
{
    // EXISTS (subquery) isn't a per-column comparison — m_columnName (set by
    // the where()/and_()/or_() entry point that produced this element) is
    // left as-is but is meaningless here; SQL generation must special-case
    // _exists_/_notExists_ to skip rendering a column.
    resetOperands();
    m_compareType = _exists_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::notExists(const QcSqlQuery & subQuery)
{
    resetOperands();
    m_compareType = _notExists_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::cast(const int & from, const int & to, const QcVariant & val)
{
    resetOperands();
    m_functionType = _cast_;
    m_functionParams = {static_cast<long long>(from), static_cast<long long>(to)};
    m_compareType = _isEqualTo_;
    m_value = val;
    return true;
}

bool QcSqlQueryElement::cast(const int & from, const int & to, const QcSqlQuery & subQuery)
{
    resetOperands();
    m_functionType = _cast_;
    m_functionParams = {static_cast<long long>(from), static_cast<long long>(to)};
    m_compareType = _isEqualTo_;
    m_subQuery = QcDeepPtr<QcSqlQuery>(subQuery);
    return true;
}

bool QcSqlQueryElement::isEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isEqualTo_, val, jsonSearchPath, _jsonAsNumber_);
}

bool QcSqlQueryElement::isNotEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isNotEqualTo_, val, jsonSearchPath, _jsonAsNumber_);
}

bool QcSqlQueryElement::isGreaterThanJsonNumber(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isGreaterThan_, val, jsonSearchPath, _jsonAsNumber_);
}

bool QcSqlQueryElement::isGreaterThanOrEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isGreaterThanOrEqualTo_, val, jsonSearchPath, _jsonAsNumber_);
}

bool QcSqlQueryElement::isLessThanJsonNumber(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isLessThan_, val, jsonSearchPath, _jsonAsNumber_);
}

bool QcSqlQueryElement::isLessThanOrEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isLessThanOrEqualTo_, val, jsonSearchPath, _jsonAsNumber_);
}

bool QcSqlQueryElement::isEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isEqualTo_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isNotEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isNotEqualTo_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isGreaterThanJsonText(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isGreaterThan_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isGreaterThanOrEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isGreaterThanOrEqualTo_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isLessThanJsonText(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isLessThan_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isLessThanOrEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath)
{
    return compareJson(_isLessThanOrEqualTo_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isLikeJsonText(const std::string & val, const std::string & jsonSearchPath)
{
    return compareJson(_isLike_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isIlikeJsonText(const std::string & val, const std::string & jsonSearchPath)
{
    return compareJson(_isILike_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isNotLikeJsonText(const std::string & val, const std::string & jsonSearchPath)
{
    return compareJson(_isNotLike_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isNotILikeJsonText(const std::string & val, const std::string & jsonSearchPath)
{
    return compareJson(_isNotILike_, val, jsonSearchPath, _jsonAsText_);
}

bool QcSqlQueryElement::isLikeJsonArrayAsText(const std::string & val, const std::string & jsonSearchPath)
{
    return compareJson(_isLike_, val, jsonSearchPath, _jsonAsRaw_);
}

bool QcSqlQueryElement::isIlikeJsonArrayAsText(const std::string & val, const std::string & jsonSearchPath)
{
    return compareJson(_isILike_, val, jsonSearchPath, _jsonAsRaw_);
}

std::string QcSqlQueryElement::toSql(QcVariantList & params) const
{
    return toSql(params, m_driver);
}

std::string QcSqlQueryElement::toSql(QcVariantList & params, QcDbDriver driver) const
{
    if (m_compareType == _openParenthesis_) {
        return "(";
    }
    if (m_compareType == _closeParenthesis_) {
        return ")";
    }

    std::string lhs = QcSqlDialect::quoteRef(m_columnName, driver);
    if (m_isJsonComparison) {
        // isXxxJson...() family (see qcsqlqueryelement.h) -- compare a value
        // pulled out of a JSON column instead of the column itself. Mutually
        // exclusive with cast() below: no isXxxJson...() method touches
        // m_functionType, so this branch and the _cast_ one never both
        // apply to the same element.
        lhs = QcSqlDialect::jsonExtractExpr(lhs, m_jsonSearchPath, m_jsonValueKind, driver);
    } else if (m_functionType == _cast_) {
        const int to = static_cast<int>(std::get<long long>(m_functionParams[1]));
        lhs = "CAST(" + lhs + " AS " + QcSqlDialect::dataTypeName(to, driver) + ")";
    }

    auto bind = [&params, driver](const QcVariant & value) {
        params.push_back(value);
        return QcSqlDialect::placeholder(params.size(), driver);
    };

    switch (m_compareType) {
        case _isLike_:
            return lhs + " LIKE " + bind(m_value);
        case _isNotLike_:
            return lhs + " NOT LIKE " + bind(m_value);
        case _isILike_:
            if (driver == QcDbDriver::PostgreSQL) {
                return lhs + " ILIKE " + bind(m_value);
            }
            // Only PostgreSQL has ILIKE -- LOWER() on both sides is the
            // portable case-insensitive equivalent everywhere else.
            return "LOWER(" + lhs + ") LIKE LOWER(" + bind(m_value) + ")";
        case _isNotILike_:
            if (driver == QcDbDriver::PostgreSQL) {
                return lhs + " NOT ILIKE " + bind(m_value);
            }
            return "LOWER(" + lhs + ") NOT LIKE LOWER(" + bind(m_value) + ")";
        case _isEqualTo_:
            return m_subQuery ? (lhs + " = (" + m_subQuery->toSql(params, driver) + ")") : (lhs + " = " + bind(m_value));
        case _isNotEqualTo_:
            return m_subQuery ? (lhs + " != (" + m_subQuery->toSql(params, driver) + ")") : (lhs + " != " + bind(m_value));
        case _isGreaterThan_:
            return lhs + " > " + bind(m_value);
        case _isGreaterThanOrEqualTo_:
            return lhs + " >= " + bind(m_value);
        case _isLessThan_:
            return lhs + " < " + bind(m_value);
        case _isLessThanOrEqualTo_:
            return lhs + " <= " + bind(m_value);
        case _isNull_:
            return lhs + " IS NULL";
        case _isNotNull_:
            return lhs + " IS NOT NULL";
        case _isIn_:
        case _isNotIn_: {
            const bool isPositive = (m_compareType == _isIn_);
            if (!m_subQuery && m_values.empty()) {
                // "column IN ()" is a syntax error on most engines -- an
                // empty set makes IN always false and NOT IN always true,
                // so render the equivalent tautology instead.
                return isPositive ? "1=0" : "1=1";
            }
            std::string args;
            if (m_subQuery) {
                args = m_subQuery->toSql(params, driver);
            } else {
                for (std::size_t i = 0; i < m_values.size(); ++i) {
                    if (i > 0) {
                        args += ", ";
                    }
                    args += bind(m_values[i]);
                }
            }
            return lhs + (isPositive ? " IN (" : " NOT IN (") + args + ")";
        }
        case _isBetween_: {
            // bind() has a side effect (appends to params) -- two calls in
            // one expression would have unspecified evaluation order (C++
            // does not sequence operands of user-defined operator+/function
            // arguments left-to-right), which on this compiler evaluated
            // right-to-left and silently swapped the bound values. Force
            // the order with separate statements instead.
            const std::string first = bind(m_values[0]);
            const std::string second = bind(m_values[1]);
            return lhs + " BETWEEN " + first + " AND " + second;
        }
        case _isNotBetween_: {
            const std::string first = bind(m_values[0]);
            const std::string second = bind(m_values[1]);
            return lhs + " NOT BETWEEN " + first + " AND " + second;
        }
        case _isDistinctFrom_:
        case _isNotDistinctFrom_: {
            // No two of these five drivers agree on how to spell a
            // NULL-safe "not equal" -- this isn't a placeholder/keyword
            // difference like ILIKE, the *shape* of the expression changes
            // per driver.
            const bool wantDistinct = (m_compareType == _isDistinctFrom_);
            switch (driver) {
                case QcDbDriver::PostgreSQL: {
                    const std::string rhs = m_subQuery ? ("(" + m_subQuery->toSql(params, driver) + ")") : bind(m_value);
                    return lhs + (wantDistinct ? " IS DISTINCT FROM " : " IS NOT DISTINCT FROM ") + rhs;
                }
                case QcDbDriver::SQLite: {
                    // SQLite's IS/IS NOT already have exactly these NULL-safe
                    // semantics, just spelled the other way around.
                    const std::string rhs = m_subQuery ? ("(" + m_subQuery->toSql(params, driver) + ")") : bind(m_value);
                    return lhs + (wantDistinct ? " IS NOT " : " IS ") + rhs;
                }
                case QcDbDriver::MySQL: {
                    // MySQL's <=> ("spaceship") operator is NULL-safe equality, i.e.
                    // exactly IS NOT DISTINCT FROM.
                    const std::string rhs = m_subQuery ? ("(" + m_subQuery->toSql(params, driver) + ")") : bind(m_value);
                    return wantDistinct ? ("NOT (" + lhs + " <=> " + rhs + ")") : (lhs + " <=> " + rhs);
                }
                case QcDbDriver::Oracle: {
                    // No native operator. The "obvious" spelled-out form (`a = b OR
                    // (a IS NULL AND b IS NULL)`) is a three-valued-logic trap:
                    // when exactly one side is NULL, `a = b` itself evaluates to
                    // NULL (not FALSE), and `NULL OR FALSE` is NULL, not FALSE --
                    // verified directly this silently drops rows from `IS DISTINCT
                    // FROM` results that should match (a NULL condition in WHERE
                    // excludes the row exactly like FALSE would, but here it's
                    // wrongly NULL for the wantDistinct=true/NOT(NULL)=NULL case
                    // where the row should have matched). Fixed by guarding the `=`
                    // so it only ever runs once both sides are known non-NULL --
                    // AND short-circuits to a definite FALSE on a NULL operand
                    // (unlike OR), so this never produces an unresolved NULL:
                    // verified directly against all four NULL/value combinations.
                    const std::string rhs = m_subQuery ? ("(" + m_subQuery->toSql(params, driver) + ")") : bind(m_value);
                    const std::string equalOrBothNull = "((" + lhs + " IS NULL AND " + rhs + " IS NULL) OR ("
                        + lhs + " IS NOT NULL AND " + rhs + " IS NOT NULL AND " + lhs + " = " + rhs + "))";
                    return wantDistinct ? ("NOT " + equalOrBothNull) : equalOrBothNull;
                }
                case QcDbDriver::MSSQL:
                default: {
                    // Same NULL-safe spelled-out form as Oracle above (same
                    // three-valued-logic reasoning applies here too -- MSSQL has no
                    // native operator either), but "?" is positional -- each
                    // *textual* occurrence consumes its own bound value (unlike
                    // Oracle's ":N", a named placeholder that can be referenced
                    // repeatedly for one bind), so a value operand needs one
                    // bind() per textual occurrence of rhs below (three: both
                    // IS NULL checks, plus the equality) -- each in its own
                    // statement so evaluation order isn't left to chance (see the
                    // isBetween/isNotBetween comment above for why that matters).
                    // A subquery operand doesn't have this problem (re-rendering
                    // its already-complete SQL text a second/third time is
                    // harmless repetition, not a missing bind) -- though a
                    // subquery that itself contains "?" placeholders would still
                    // need each copy independently bound, an edge case left alone
                    // here since MSSQL wasn't verified against a live server when
                    // this was written.
                    std::string rhsNullCheck1;
                    std::string rhsNullCheck2;
                    std::string rhsEquality;
                    if (m_subQuery) {
                        rhsNullCheck1 = rhsNullCheck2 = rhsEquality = "(" + m_subQuery->toSql(params, driver) + ")";
                    } else {
                        rhsNullCheck1 = bind(m_value);
                        rhsNullCheck2 = bind(m_value);
                        rhsEquality = bind(m_value);
                    }
                    const std::string equalOrBothNull = "((" + lhs + " IS NULL AND " + rhsNullCheck1 + " IS NULL) OR ("
                        + lhs + " IS NOT NULL AND " + rhsNullCheck2 + " IS NOT NULL AND " + lhs + " = " + rhsEquality + "))";
                    return wantDistinct ? ("NOT " + equalOrBothNull) : equalOrBothNull;
                }
            }
        }
        case _exists_:
            return "EXISTS (" + m_subQuery->toSql(params, driver) + ")";
        case _notExists_:
            return "NOT EXISTS (" + m_subQuery->toSql(params, driver) + ")";
        default:
            return {};
    }
}

bool QcSqlQueryElement::isOpenParenthesisMarker() const
{
    return m_compareType == _openParenthesis_;
}

bool QcSqlQueryElement::isCloseParenthesisMarker() const
{
    return m_compareType == _closeParenthesis_;
}

bool QcSqlQueryElement::isIncomplete() const
{
    if (isOpenParenthesisMarker() || isCloseParenthesisMarker()) {
        return false;
    }

    return m_compareType == _isEqualTo_ && !m_isJsonComparison && !m_subQuery && m_values.empty()
        && std::holds_alternative<std::monostate>(m_value);
}

QcSqlBase::QcStringList QcSqlQueryElement::validateChain(const std::vector<std::pair<int, QcSqlQueryElement>> & chain, const std::string & clauseLabel)
{
    QcStringList problems;

    for (const auto & entry : chain) {
        const QcSqlQueryElement & element = entry.second;
        if (element.isIncomplete()) {
            problems.push_back(clauseLabel + " condition on \"" + element.m_columnName
                + "\" has no comparator set (call e.g. isEqualTo()/isLike()/isNull() on the element where()/and_()/or_() returned)");
        }
    }

    return problems;
}

std::string QcSqlQueryElement::renderChain(const std::vector<std::pair<int, QcSqlQueryElement>> & chain, QcVariantList & params,
                                            QcDbDriver driver)
{
    std::string sql;
    // Treat "nothing rendered yet" the same as "just after an open paren":
    // in both cases the next element needs no connector in front of it.
    bool previousWasOpenParen = true;

    for (const auto & entry : chain) {
        const QcSqlQueryElement & element = entry.second;
        const bool isClose = element.isCloseParenthesisMarker();

        if (!previousWasOpenParen && !isClose) {
            sql += (entry.first != 0) ? " OR " : " AND ";
        }

        sql += element.toSql(params, driver);
        previousWasOpenParen = element.isOpenParenthesisMarker();
    }

    return sql;
}
