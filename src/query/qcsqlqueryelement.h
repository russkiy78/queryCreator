#ifndef QCSQLQUERYELEMENT_H
#define QCSQLQUERYELEMENT_H

#include "qcdbdriver.h"
#include "qcdeepptr.h"
#include "qcsqlbase.h"
#include <string>
#include <vector>

// This header only forward-declares QcSqlQuery (to avoid a header cycle with
// qcsqlquery.h, which owns the WHERE/HAVING chains built from these
// elements) — QcDeepPtr<QcSqlQuery> tolerates that like std::unique_ptr
// would, but any translation unit that actually constructs/destroys a
// QcSqlQueryElement (not just declares a pointer/reference to one) needs
// qcsqlquery.h included too, for QcSqlQuery to be a complete type there.
class QcSqlQuery;
class QcSqlQueryElementWhiteBoxTest;
// QcSqlQuery's own white-box test fixture also needs to inspect elements it
// builds (e.g. that where("col") really threaded "col" through) — see
// qcsqlquery.h.
class QcSqlQueryWhiteBoxTest;
class QcSqlQueryElement  : public QcSqlBase
{
public:
    QcSqlQueryElement();

    // Column-only: how QcSqlQuery::where()/and_()/or_() seed an element
    // before the caller chains on a comparator (e.g. .isEqualTo(...)) that
    // fills in the rest. compareType defaults to _isEqualTo_ via the member
    // initializer below until a comparator overwrites it.
    explicit QcSqlQueryElement(const std::string & columnName);

    // Group-parenthesis marker: compareType must be _openParenthesis_ or
    // _closeParenthesis_ — these tokens sit in the same WHERE/HAVING chain as
    // real comparisons (see qcsqlbase.h) but carry no column/value.
    explicit QcSqlQueryElement(const int & compareType);

    QcSqlQueryElement(const int & compareType, const std::string & columnName, const QcVariant & val);
    QcSqlQueryElement(const int & compareType, const std::string & columnName, const QcSqlQuery & subQuery);

    // Sets the dialect toSql() renders for once, up front -- see
    // QcSqlQuery::useDriver()'s doc comment for the full rationale. Rarely
    // needed directly: QcSqlQuery::toSql() (via renderChain()) already
    // overrides this with its own driver when rendering each WHERE/HAVING
    // element.
    QcSqlQueryElement & useDriver(QcDbDriver driver);

private:
    QcDbDriver m_driver = QcDbDriver::PostgreSQL;
    int m_functionType = _no_function_;
    std::vector<QcVariant> m_functionParams;
    int m_compareType = _isEqualTo_;
    std::string m_columnName;
    std::string m_columnAlias;
    QcDeepPtr<QcSqlQuery> m_subQuery;
    QcVariant m_value;
    QcVariantList m_values; // isIn/isNotIn (list) and isBetween/isNotBetween (2 operands)

    // Set only by the isXxxJson...() comparator family below -- when true,
    // toSql() compares QcSqlDialect::jsonExtractExpr(column, m_jsonSearchPath,
    // m_jsonValueKind, driver) instead of the bare column, using the exact
    // same m_compareType-driven switch (_isEqualTo_/_isLike_/...) every
    // ordinary comparator already renders through. m_jsonSearchPath follows
    // the path convention documented on QcSqlDialect::jsonExtractExpr() in
    // qcsqldialect.h ("a.b[2].c", no leading root marker).
    bool m_isJsonComparison = false;
    int m_jsonValueKind = _jsonAsText_;
    std::string m_jsonSearchPath;

    // Every comparator below sets exactly one of m_value/m_values/m_subQuery
    // and must clear the other two — otherwise state from a previous call on
    // the same element (e.g. isIn() followed by isEqualTo()) would leak into
    // the new comparison.
    void resetOperands();

    // Shared body for the isXxxJson...() family below -- every one of them
    // is otherwise just an ordinary value comparator (see resetOperands()
    // above) plus the three JSON fields, so this is the one place that
    // combines them instead of repeating both sets of assignments 18 times.
    bool compareJson(int compareType, const QcVariant & val, const std::string & jsonSearchPath, int kind);

    friend class QcSqlQueryElementWhiteBoxTest;
    friend class QcSqlQueryWhiteBoxTest;

public:

    /*compare*/

    bool isLike(const std::string & val);
    bool isIlike(const std::string & val);
    bool isNotLike(const std::string & val);
    bool isNotILike(const std::string & val);

    bool isEqualTo(const QcVariant & val);
    bool isEqualTo(const QcSqlQuery & subQuery);

    bool isNotEqualTo(const QcVariant & val);
    bool isNotEqualTo(const QcSqlQuery & subQuery);

    bool isGreaterThan(const QcVariant & val);
    bool isGreaterThanOrEqualTo(const QcVariant & val);
    bool isLessThan(const QcVariant & val);
    bool isLessThanOrEqualTo(const QcVariant & val);
    bool isNull();
    bool isNotNull();

    bool isIn(const QcVariantList & val);
    bool isIn(const QcSqlQuery & subQuery);
    bool isNotIn(const QcVariantList & val);
    bool isNotIn(const QcSqlQuery & subQuery);

    bool isBetween(const QcVariant & val1, const QcVariant & val2);
    bool isNotBetween(const QcVariant & val1, const QcVariant & val2);

    bool isDistinctFrom(const QcVariant & val);
    bool isDistinctFrom(const QcSqlQuery & subQuery);
    bool isNotDistinctFrom(const QcVariant & val);
    bool isNotDistinctFrom(const QcSqlQuery & subQuery);

    bool exists(const QcSqlQuery & subQuery);
    bool notExists(const QcSqlQuery & subQuery);

    /*functions*/
    bool cast(const int & from, const int & to, const QcVariant & val);
    bool cast(const int & from, const int & to, const QcSqlQuery & subQuery);

    /*json*/
    // Compares a value pulled out of a JSON column at `jsonSearchPath`
    // (QcSqlDialect::jsonExtractExpr()'s path convention -- "a.b[2].c", no
    // leading root marker) instead of comparing the column itself -- e.g.
    // `where("metadata").isEqualToJsonNumber(5LL, "level")` renders as
    // `<json-extract-as-number>(metadata, 'level') = ?` on whichever driver
    // is active, no per-driver branching needed at the call site (contrast
    // the hand-written driver switch every addFreeText()-based JSON
    // condition needs, see usage.md). Each family below picks how the
    // extracted value is coerced before comparing (QcSqlBase::jsonValueKind):
    // Number for numeric comparisons ("10" > "9" would be true
    // lexicographically but false numerically), Text for a scalar string
    // compared/matched as its own unwrapped text, and (isXxxJsonArrayAsText
    // only) Raw for matching a LIKE pattern against a JSON array/object's own
    // serialized text instead of one unwrapped scalar element.
    bool isEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isNotEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isGreaterThanJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isGreaterThanOrEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLessThanJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLessThanOrEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);

    bool isEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isNotEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isGreaterThanJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isGreaterThanOrEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLessThanJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLessThanOrEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath);

    bool isLikeJsonText(const std::string & val, const std::string & jsonSearchPath);
    bool isIlikeJsonText(const std::string & val, const std::string & jsonSearchPath);
    bool isNotLikeJsonText(const std::string & val, const std::string & jsonSearchPath);
    bool isNotILikeJsonText(const std::string & val, const std::string & jsonSearchPath);

    // Unlike isLikeJsonText() (which matches a single scalar's own text),
    // these match the LIKE pattern against the raw JSON-encoded text of
    // whatever is at jsonSearchPath -- typically a JSON array, still bracketed
    // and quoted (e.g. `["c++","sql"]`), so a pattern like `%"sql"%` can
    // search across every element at once instead of one indexed element.
    bool isLikeJsonArrayAsText(const std::string & val, const std::string & jsonSearchPath);
    bool isIlikeJsonArrayAsText(const std::string & val, const std::string & jsonSearchPath);

    // Renders this element: a bare "(" / ")" for a paren-group marker, or a
    // full predicate ("column OP value|(subquery)") otherwise, wrapping the
    // column in CAST(...) first if cast() set one up. Bound values are
    // appended to `params` (shared across the whole enclosing statement, see
    // QcSqlQuery::toSql(QcVariantList&)) in the same order their
    // placeholders reference them. Uses the dialect useDriver() configured;
    // the explicit-driver overload renders for `driver` instead (forwarded
    // to any subquery's own toSql()) -- see QcSqlQuery::toSql()'s doc
    // comment for the full rationale. ILIKE/IS DISTINCT FROM have no shared
    // syntax at all across drivers, so this is where that choice matters most.
    std::string toSql(QcVariantList & params) const;
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;

    // Lets a WHERE/HAVING chain renderer (QcSqlQueryElement::renderChain)
    // decide whether a logical connector (AND/OR) belongs before this
    // element without needing friend access to m_compareType or resorting
    // to comparing rendered SQL text.
    bool isOpenParenthesisMarker() const;
    bool isCloseParenthesisMarker() const;

    // Renders a whole WHERE/HAVING-shaped chain -- {connector, element}
    // pairs, possibly interleaved with paren-group markers -- into one
    // parenthesis-and-AND/OR-glued expression, appending each element's
    // bind values to `params` in the order their placeholders reference
    // them. `connector` follows the 0 = AND / nonzero (1) = OR convention
    // shared by every private LogicalConnector enum that feeds this
    // function (QcSqlQuery::LogicalConnector, QcSqlUpdate::LogicalConnector,
    // QcSqlDelete::LogicalConnector) -- meaningless for the first rendered
    // element of the chain or for the element right after an open paren, so
    // never actually emitted there. Pulled out of QcSqlQuery (which
    // originally had the only WHERE/HAVING clause) so every statement type
    // with a WHERE clause shares this exact AND/OR/paren-gluing logic
    // instead of reimplementing it.
    static std::string renderChain(const std::vector<std::pair<int, QcSqlQueryElement>> & chain, QcVariantList & params,
                                    QcDbDriver driver = QcDbDriver::PostgreSQL);

};

#endif // QCSQLQUERYELEMENT_H
