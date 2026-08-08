#ifndef QCSQLBASE_H
#define QCSQLBASE_H

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

class QcSqlBase
{
public:
    using QcVariant = std::variant<std::monostate, long long, double, std::string, std::vector<std::byte>>;
    using QcVariantList = std::vector<QcVariant>;
    using QcStringList = std::vector<std::string>;

    /*enums*/
    enum compareTypes { _isLike_, _isILike_, _isNotLike_, _isNotILike_, _isEqualTo_, _isNotEqualTo_,
                     _isGreaterThan_, _isGreaterThanOrEqualTo_, _isLessThan_, _isLessThanOrEqualTo_,
                     _isNull_, _isNotNull_, _isIn_, _isNotIn_, _isBetween_, _isNotBetween_,
                     _isDistinctFrom_, _isNotDistinctFrom_, _exists_, _notExists_,
                     _openParenthesis_, _closeParenthesis_};

    enum functionTypes {_no_function_, _cast_, _concat_, _round_, _upperCase_, _lowerCase_,
                     _count_, _sum_, _avg_, _min_, _max_,
                     _coalesce_, _nullIf_, _trim_, _substring_, _length_, _replace_, _case_,
                     _extract_, _dateAdd_, _jsonExtract_};
    enum dataTypes {_int_, _float_, _string_, _date_, _bool_, _decimal_, _datetime_, _text_, _blob_, _json_};

    enum joinTypes {_innerJoin_, _leftJoin_, _rightJoin_, _fullJoin_, _crossJoin_};
    enum setOperationTypes {_union_, _unionAll_, _intersect_, _except_};

    // NULL placement for one QcSqlQuery::orderAsc()/orderDesc() column.
    // _nullsDefault_ renders no explicit NULLS clause at all (each driver's
    // own, otherwise-unspecified default -- see QcSqlDialect::orderByEntry()
    // for what that actually is per driver); _nullsFirst_/_nullsLast_ force
    // it explicitly, emulated on the two drivers (MySQL/MSSQL) with no
    // native NULLS FIRST/LAST syntax.
    enum nullsPosition {_nullsDefault_, _nullsFirst_, _nullsLast_};

    // Date/time component vocabulary for functionTypes::_extract_/_dateAdd_
    // (QcSqlQueryValue::extract()/dateAdd()) -- kept deliberately small
    // (the six components every one of the five drivers can name plainly)
    // rather than exhaustive (e.g. QUARTER/WEEK/DOY, whose spelling and
    // even availability varies far more per-driver) -- see
    // QcSqlDialect::extractExpr()/dateAddExpr() in qcsqldialect.h for the
    // per-driver rendering this enum feeds.
    enum datePartTypes {_year_, _month_, _day_, _hour_, _minute_, _second_};

    // How QcSqlQueryElement's isXxxJson...() comparator family (see
    // qcsqlqueryelement.h) wants a value extracted from a JSON column at a
    // jsonSearchPath before comparing it -- fed straight into
    // QcSqlDialect::jsonExtractExpr(), which is what actually differs per
    // driver:
    //   - _jsonAsText_: scalar text, unquoted (a JSON string "admin" becomes
    //     the bare text admin) -- isEqualToJsonText()/isLikeJsonText()/...
    //   - _jsonAsNumber_: coerced to a numeric SQL type, so the comparison is
    //     numeric (10 > 9) rather than lexicographic ("10" > "9" is false) --
    //     isEqualToJsonNumber()/isGreaterThanJsonNumber()/...
    //   - _jsonAsRaw_: the value's own JSON-encoded text, unwrapped (a JSON
    //     string keeps its quotes, an array/object keeps its
    //     brackets/braces) -- isLikeJsonArrayAsText(), matching a pattern
    //     against a serialized JSON array/object rather than one scalar.
    enum jsonValueKind {_jsonAsText_, _jsonAsNumber_, _jsonAsRaw_};

};

// Thrown by QcSqlQuery/QcSqlInsert/QcSqlUpdate/QcSqlDelete::toSql() when
// validate() finds the statement structurally incomplete -- e.g. RETURNING
// with no target table, a JOIN with no ON condition, a where()/and_()/or_()
// call with no comparator ever chained onto it, an openParenthesis() left
// unclosed. Each builder's own validate() (callable directly too, without
// triggering the throw -- see e.g. QcSqlQuery::validate()) is the source of
// truth for exactly what's checked; this only exists to turn its findings
// into a single, structural exception at the point toSql() would otherwise
// have silently emitted broken SQL. Never thrown for anything content-level
// (a nonexistent column name, a value of the wrong type) -- the builders
// have no schema to check that against, only their own gathered state.
class QcQueryBuildError : public std::logic_error
{
public:
    using std::logic_error::logic_error;
};

// Joins `problems` (as gathered by a builder's own validate()) into one
// QcQueryBuildError, or does nothing if `problems` is empty -- the one-line
// call every builder's toSql() makes right before it starts rendering, so
// every validate() implementation stays free of exception-formatting
// concerns and every builder throws with the exact same message shape.
inline void qcThrowIfQueryInvalid(const std::vector<std::string> & problems)
{
    if (problems.empty()) {
        return;
    }

    std::string message = "Incomplete query:";
    for (const std::string & problem : problems) {
        message += " " + problem + ";";
    }
    message.pop_back();

    throw QcQueryBuildError(message);
}

// Result of QcSqlQuery::toSql(): the generated SQL text (native positional
// placeholders for whichever QcDbDriver it was rendered for -- see
// qcsqldialect.h) paired with the bind values in the same order the
// placeholders reference them. This is exactly the (sql, params) pair
// QcNativeConnection::execute() expects.
struct QcSqlStatement
{
    std::string sql;
    QcSqlBase::QcVariantList params;

    // Nonzero only for a QcSqlInsert/Update/Delete's returning() on Oracle:
    // the number of trailing "RETURNING ... INTO :N, :N+1, ..." OUT-bind
    // placeholders `sql` ends with, immediately following the ordinary
    // `params.size()` IN placeholders -- QcNativeConnection::executeReturning()
    // needs this count to know how many trailing placeholders to bind as OUT
    // rather than IN, then read back as the one result row. Zero (default) on
    // every other driver, where RETURNING/OUTPUT already rides back as an
    // ordinary result row through plain execute() -- executeReturning() with
    // count 0 is exactly equivalent to execute().
    std::size_t returningColumnCount = 0;

    // Set only for a QcSqlInsert on MySQL whose returning() was given an
    // auto-increment column name -- MySQL has no INSERT ... RETURNING syntax
    // at all, so `sql`/`params` above is just the plain INSERT, and the
    // requested returning columns instead have to come from this follow-up
    // "SELECT <cols> FROM <table> WHERE <autoIncrementColumn> = ?" text,
    // run in the same transaction right after the insert, with the single
    // "?" bound to MySQL's LAST_INSERT_ID() for that insert (see
    // QueryCreator::execute(const QcSqlInsert&)). Empty (default) means "no
    // MySQL follow-up needed" -- every other driver, and MySQL without an
    // auto-increment column, leave this untouched.
    std::string mysqlReturningSelectSql;

    // The column names passed to QcSqlInsert/Update/Delete::returning(), in
    // the same order as returningColumnCount trailing placeholders/RETURNING
    // list entries above -- set unconditionally by every driver (this is
    // just a copy of the caller's own returning() argument, cheap regardless
    // of whether it ends up needed). Only actually consulted by
    // QcNativeConnection::executeReturningNamed() on Oracle, where a
    // RETURNING...INTO OUT bind has no result-set metadata of its own to read
    // column names from (see the doc comment there) -- every other driver's
    // RETURNING/OUTPUT rides back as an ordinary result set and gets its
    // column names from driver metadata instead, ignoring this field.
    QcSqlBase::QcStringList returningColumnNames;
};

#endif // QCSQLBASE_H
