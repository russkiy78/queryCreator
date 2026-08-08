#ifndef QCSQLDIALECT_H
#define QCSQLDIALECT_H

#include <string>
#include <vector>

#include "qcdbdriver.h"
#include "qcsqlbase.h"

// Small, centralized home for the handful of places SQL generation
// (QcSqlQueryValue/QcSqlQueryElement/QcSqlQuery's toSql()) actually differs
// per driver -- everything else about generation is dialect-agnostic. Every
// function below takes an explicit `QcDbDriver driver` parameter (defaulted
// to QcDbDriver::PostgreSQL, this project's long-standing default) and
// switches on it at runtime -- unlike QcNativeConnection, nothing here has
// an external dependency (it's all string rendering), so every driver's
// branch is always compiled regardless of QC_DB_DRIVERS: rendering
// PostgreSQL SQL text doesn't require libpq to be linked.
namespace QcSqlDialect {

// Native positional bind placeholder for the 1-based position `index` among
// all parameters bound so far in the *whole* statement (not just the current
// fragment) -- e.g. PostgreSQL's 3rd parameter is "$3" regardless of which
// WHERE/JOIN/subquery fragment it came from. Callers pass
// `params.size()` right after appending the value, which is exactly that
// 1-based index.
std::string placeholder(std::size_t index, QcDbDriver driver = QcDbDriver::PostgreSQL);

// SQL type name for a QcSqlBase::dataTypes value, used to render CAST(expr AS
// <name>). Deliberately approximate for drivers with narrow/unusual CAST
// target lists (MySQL, Oracle) or no native type for a given dataTypes value
// (MSSQL/Oracle JSON) -- see qcsqldialect.cpp for the per-driver mapping and
// its documented compromises.
std::string dataTypeName(int dataType, QcDbDriver driver = QcDbDriver::PostgreSQL);

// Whole LIMIT/OFFSET-equivalent clause (including the leading space), or an
// empty string if neither a row cap nor an offset was requested
// (rowsCount <= 0 && startRow <= 0). PostgreSQL/MySQL/SQLite use
// `LIMIT n OFFSET m`; MSSQL/Oracle use the ANSI `OFFSET m ROWS FETCH NEXT n
// ROWS ONLY` form (the only one they support). `withTies` always selects the
// ANSI form (`... WITH TIES` instead of `... ONLY`) since none of
// PostgreSQL/MySQL/MSSQL/Oracle support WITH TIES on the bare LIMIT syntax;
// SQLite has no WITH TIES support at all in any form -- requesting it there
// still emits ANSI syntax, which will fail at execution time (a documented
// limitation, not silently ignored).
std::string limitOffsetClause(int rowsCount, int startRow, bool withTies, QcDbDriver driver = QcDbDriver::PostgreSQL);

// String-concatenation expression joining `operands` (already-rendered SQL
// expressions, at least one) in order. This is a real structural difference,
// not just a spelling one: PostgreSQL/MySQL/MSSQL have a variadic CONCAT(...)
// that takes any number of arguments, but Oracle's CONCAT() only ever takes
// exactly two, and SQLite has no CONCAT() function at all in versions before
// 3.44 (2023) -- `operand1 || operand2 || ...` (the `||` operator) is what
// both of those actually need, and is standard SQL supported everywhere,
// which is why it's used for them here instead of chaining binary CONCAT()
// calls.
std::string concatExpr(const std::vector<std::string> & operands, QcDbDriver driver = QcDbDriver::PostgreSQL);

// Whole "give me back the affected row's columns" clause fragment for
// INSERT/UPDATE/DELETE, including its leading space -- or an empty string
// if `columns` is empty (no clause requested) or the active driver has no
// way to express this within a single statement. `mssqlRowKeyword` selects
// which of MSSQL's two virtual OUTPUT tables to qualify columns with
// ("inserted" for INSERT/UPDATE, "deleted" for DELETE) -- ignored by every
// other driver. `oracleFirstPlaceholderIndex` is the 1-based index the first
// Oracle OUT-bind placeholder should use -- the caller already knows exactly
// how many ordinary IN placeholders it used (`params.size()` right after
// binding the last one), so this is always just `params.size() + 1`;
// ignored by every other driver.
//
//   - PostgreSQL/SQLite (3.35+, see third_party/sqlite): native
//     `RETURNING col1, col2`, identical syntax on both.
//   - MSSQL: `OUTPUT inserted.col1, inserted.col2` (or `deleted.` for
//     DELETE) -- needs the row-keyword qualifier MSSQL requires on every
//     column, unlike RETURNING.
//   - MySQL: no equivalent at all, in any form -- always empty. (INSERT can
//     still get an emulated equivalent via a follow-up SELECT keyed on
//     LAST_INSERT_ID() -- see QcSqlInsert::returning()'s autoIncrementColumn
//     overload and QueryCreator::execute(const QcSqlInsert&) -- but that's a
//     multi-statement caller-side mechanism, not something expressible as
//     one clause fragment here.)
//   - Oracle: `RETURNING col1, col2 INTO :N, :N+1` -- unlike the other
//     RETURNING-capable drivers, the returned values come back through OUT
//     binds, not extra result-set rows, so this fragment also has to name
//     the bind placeholders (see QcNativeConnection::executeReturning() for
//     the OUT-bind side, and QcSqlStatement::returningColumnCount for how
//     callers learn how many trailing placeholders that is).
//
// MySQL alone still silently producing no clause (rather than emitting text
// that fails at execution) mirrors distinctOn() degrading to plain DISTINCT
// on non-PostgreSQL drivers (see qcsqlquery.cpp) -- both are cases where the
// requested semantics genuinely cannot be expressed within a single
// statement on that driver, as opposed to WITH TIES (qcsqldialect.cpp's
// limitOffsetClause), which is left to fail at execution time because
// there's no silent equivalent to degrade to.
//
// Position in the generated statement is deliberately NOT handled here --
// it differs by statement shape (RETURNING/RETURNING...INTO is always
// appended at the very end; MSSQL's OUTPUT is injected between the
// column/SET list and VALUES/WHERE) -- callers
// (qcsqlinsert.cpp/qcsqlupdate.cpp/qcsqldelete.cpp) splice this fragment
// into the right place themselves.
std::string returningClause(const QcSqlBase::QcStringList & columns, const std::string & mssqlRowKeyword,
                             std::size_t oracleFirstPlaceholderIndex, QcDbDriver driver = QcDbDriver::PostgreSQL);

// Quotes a single identifier segment (a table name, column name, alias, or
// CTE name -- never a dotted reference, see quoteRef() for that) for
// `driver`, doubling any embedded quote character to escape it per that
// dialect's own rule. Returns `name` unchanged if it is empty or
// exactly "*" -- the SELECT/`table.*` wildcard is never an identifier to
// quote.
//   - PostgreSQL/SQLite/Oracle: ANSI double quotes, case preserved, `"a""b"`
//     for `a"b`. Oracle's quoted identifiers are case-sensitive ANSI SQL,
//     identical in spirit to PostgreSQL's -- verified directly against a
//     real Oracle instance (quoted lowercase names round-trip exactly as
//     typed, same as PostgreSQL). The one Oracle-specific consequence:
//     Oracle folds an *unquoted* identifier to UPPERCASE at creation time
//     (PostgreSQL/SQLite fold to lowercase), so a schema built the ordinary
//     way -- unquoted DDL, e.g. `CREATE TABLE qc_bt_employees (id NUMBER,
//     ...)` -- really creates `QC_BT_EMPLOYEES`/`ID`. Once this function is
//     in the rendering path, that schema's DDL has to be (re)written with
//     quoted, case-matching identifiers too, exactly the same requirement
//     PostgreSQL/SQLite already have (their unquoted-DDL default is
//     lowercase, which happens to already match this project's
//     lowercase-snake_case naming, so it was never visible there) -- see
//     test_integration_select.cpp/test_integration_dml.cpp's Oracle DDL
//     branches for the actual fix. This is not a reason to special-case
//     Oracle's *quoting* behavior itself, which is standard and correctly
//     case-preserving.
//   - MySQL: backticks, case preserved, `` `a``b` `` for `` a`b ``.
//   - MSSQL: brackets, case preserved, `[a]]b]` for `a]b`.
std::string quoteIdentifier(const std::string & name, QcDbDriver driver = QcDbDriver::PostgreSQL);

// Quotes a possibly dot-qualified identifier reference (a plain column, or
// `table.column`/`alias.column`/`schema.table.column`) by splitting on '.'
// and quoting each segment individually via quoteIdentifier() -- every one
// of the five drivers spells a qualified reference this way; none of them
// treats "a.b.c" as a single quotable token. A "*" segment (bare, or after a
// table/alias qualifier as in "t.*") is left bare, same as
// quoteIdentifier(). Empty input is returned unchanged -- some call sites
// carry a meaningless empty column name through this code path on purpose
// (e.g. QcSqlQueryElement::exists(), whose EXISTS (subquery) rendering never
// looks at the column name it was seeded with).
//
// A '.'-separated reference is only quoted if *every* segment looks like a
// plain identifier (`[A-Za-z_][A-Za-z0-9_$]*`, or "*") -- otherwise `name`
// is returned completely unchanged. This matters because "column name"
// parameters throughout this API (where()/groupBy()/orderAsc()/
// addReturnValue()/having()/...) are also this builder's only way to reach
// SQL it has no dedicated method for yet -- aggregate functions
// (`"COUNT(*)"`, `"AVG(salary) <avg_salary>"`), JSON operators
// (`"metadata->>'level'"`), and similar raw expressions are routinely
// passed through them (see e.g. GroupByWithSumAvgMinMaxMatchesComputedExpectations
// / JsonFieldExtractionReturnsSeededValue in test_integration_select.cpp).
// A real identifier and a raw expression are indistinguishable from the
// type system's point of view -- both are just `std::string` -- so the only
// safe move on anything that *isn't* unambiguously identifier-shaped is to
// leave it exactly as it was before this function existed, rather than
// quote it partially/incorrectly and corrupt it. This is the same
// "expressible or an explicit, untouched escape hatch, nothing in between"
// principle QcSqlQuery::addFreeText/JOIN's onCondition/
// QcSqlQueryValue::concat()'s operand list already follow.
std::string quoteRef(const std::string & name, QcDbDriver driver = QcDbDriver::PostgreSQL);

// Quotes a FROM/JOIN table reference, which -- unlike every other
// identifier-shaped parameter in this API -- can carry an inline alias:
// QcSqlQuery::fromTable() has no separate alias parameter (unlike
// fromSubQuery()/addXJoin(), which do), so an aliased FROM table (needed for
// a self-join, e.g. "qc_bt_employees e") is conventionally written as one
// "table alias" string, the two separated by whitespace -- see
// qcsqlquery.cpp/test_integration_select.cpp's self-join tests. Splits on
// the first whitespace run: the part before is a (possibly dot-qualified)
// table name, quoted via quoteRef(); the part after, if any, is a plain
// alias, quoted via quoteIdentifier(). No whitespace at all -- the common
// case -- is just quoteRef(name).
std::string quoteTableRef(const std::string & name, QcDbDriver driver = QcDbDriver::PostgreSQL);

// Character-length function wrapping `expr` (already-rendered, e.g. an
// already-quoted column). PostgreSQL/SQLite/Oracle/MySQL all spell this
// `LENGTH(expr)`; MSSQL alone has no `LENGTH()` at all -- only `LEN(expr)`,
// which additionally trims trailing spaces before counting (a real
// behavioral difference from the other four, not just a spelling one --
// `LEN('ab  ')` is 2, not 4) that this function does not attempt to paper
// over.
std::string lengthExpr(const std::string & expr, QcDbDriver driver = QcDbDriver::PostgreSQL);

// Substring function: characters of `expr` starting at the 1-based position
// `start`, `length` characters long. MSSQL is the one driver here with no
// `SUBSTR()` at all -- only `SUBSTRING(expr, start, length)`, same argument
// order; the other four (PostgreSQL/SQLite/Oracle/MySQL) all support the
// shorter `SUBSTR(expr, start, length)` form.
std::string substringExpr(const std::string & expr, int start, int length, QcDbDriver driver = QcDbDriver::PostgreSQL);

// EXTRACT(part FROM expr)-equivalent, pulling one date/time component
// (QcSqlBase::datePartTypes) out of `expr`. A real structural difference,
// not just spelling: PostgreSQL/MySQL/Oracle all support the ANSI
// `EXTRACT(YEAR FROM expr)` form directly; MSSQL has no `EXTRACT()` at all,
// only `DATEPART(year, expr)`; SQLite has neither -- `strftime('%Y', expr)`
// (which returns text, hence the `CAST(... AS INTEGER)` wrapper to match
// the numeric result every other driver gives) is the closest equivalent.
std::string extractExpr(const std::string & expr, int part, QcDbDriver driver = QcDbDriver::PostgreSQL);

// Adds `amount` of the given date/time component (QcSqlBase::datePartTypes,
// negative `amount` subtracts) to `expr`. The most dialect-divergent
// function in this file: PostgreSQL uses `expr + INTERVAL 'N unit'`; MySQL
// `DATE_ADD(expr, INTERVAL N unit)`; MSSQL `DATEADD(unit, N, expr)`; SQLite
// `datetime(expr, '+N unit')` (explicit sign required in SQLite's modifier
// syntax, unlike the other four); Oracle `expr + INTERVAL 'N' unit` -- which
// (unlike the ANSI standard, where a literal INTERVAL only covers
// day/hour/minute/second) Oracle additionally accepts for MONTH/YEAR too,
// verified directly, so no ADD_MONTHS()-style fallback is needed for those.
std::string dateAddExpr(const std::string & expr, int part, int amount, QcDbDriver driver = QcDbDriver::PostgreSQL);

// One ORDER BY entry: `column` (already quoted/rendered by the caller --
// this function only adds direction/NULLS positioning around it, it does
// not call quoteRef() itself) plus its ASC/DESC direction and
// QcSqlBase::nullsPosition. `nulls` == _nullsDefault_ renders a bare
// "column ASC"/"column DESC" with no NULLS clause at all, leaving each
// driver's own unspecified-NULLS-order default in effect -- which is *not*
// the same default across drivers even then: PostgreSQL/Oracle sort NULL as
// if larger than every value (ASC puts them last), MySQL/SQLite/MSSQL sort
// it as if smaller (ASC puts them first).
//   - PostgreSQL/SQLite (3.30+, see third_party/sqlite)/Oracle: native
//     trailing `NULLS FIRST`/`NULLS LAST`.
//   - MySQL/MSSQL: no such syntax in any form -- emulated with a leading
//     tiebreaker sort key ahead of the real column,
//     `CASE WHEN <column> IS NULL THEN 0 ELSE 1 END`/`... THEN 1 ELSE 0
//     END` (always ascending on the tiebreaker itself, regardless of the
//     real column's own direction), which needs `column`'s text a second
//     time.
std::string orderByEntry(const std::string & column, bool descending, int nulls, QcDbDriver driver = QcDbDriver::PostgreSQL);

// One value pulled out of `expr` (an already-rendered SQL expression --
// typically a quoted JSON column reference) at `jsonSearchPath`, feeding
// QcSqlQueryElement's isXxxJson...() comparator family (isEqualToJsonNumber/
// isLikeJsonText/isLikeJsonArrayAsText/... -- see qcsqlqueryelement.h). This
// is a real structural difference per driver, not just a spelling one: each
// of the five has its own native JSON path-extraction function/operator, and
// three different shapes of value it can hand back (see
// QcSqlBase::jsonValueKind for what `kind` selects).
//
// `jsonSearchPath` is a plain dotted path with no leading root marker --
// "." separates nested object keys, "[N]" indexes into an array -- e.g.
// "a.b[2].c" for the value at `.a.b[2].c` (an object "a", nested object
// "b", array index 2, nested object "c"). PostgreSQL is the one driver
// whose own path-extraction functions (jsonb_extract_path()/
// jsonb_extract_path_text()) don't take a path string at all -- they want
// the path already split into separate text arguments -- so this function
// parses it apart for that branch only; the other four drivers' native JSON
// functions want a JSONPath string instead, so this function prepends the
// "$"/"$." root marker their syntax requires (the input path itself never
// carries one).
//   - PostgreSQL: `jsonb_extract_path_text(expr::jsonb, 'a', 'b', '2', 'c')`
//     (_jsonAsText_), the same wrapped in `(...)::numeric` (_jsonAsNumber_),
//     or `jsonb_extract_path(...)::text` (_jsonAsRaw_, keeping the
//     extracted value's own JSON quoting/brackets rather than unwrapping
//     it) -- `expr::jsonb` tolerates both `json` and `jsonb` columns.
//   - SQLite: `json_extract(expr, '$.a.b[2].c')` for all three `kind`s --
//     it already returns a scalar JSON string/number fully unwrapped (with
//     real numeric affinity for numbers), and an object/array's own
//     serialized JSON text otherwise, so no further wrapping is needed
//     for any of the three.
//   - MySQL: `JSON_UNQUOTE(JSON_EXTRACT(expr, path))` (_jsonAsText_),
//     `(JSON_EXTRACT(expr, path) + 0)` (_jsonAsNumber_ -- MySQL's own idiom
//     for forcing numeric context instead of a lexicographic comparison),
//     or bare `JSON_EXTRACT(expr, path)` (_jsonAsRaw_).
//   - MSSQL: `JSON_VALUE(expr, path)` (_jsonAsText_), `CAST(JSON_VALUE(expr,
//     path) AS FLOAT)` (_jsonAsNumber_), or `JSON_QUERY(expr, path)`
//     (_jsonAsRaw_) -- JSON_VALUE is scalar-only (NULL on an object/array
//     path), JSON_QUERY is MSSQL's counterpart for an object/array fragment.
//   - Oracle: `JSON_VALUE(expr, path)` (_jsonAsText_), `JSON_VALUE(expr,
//     path RETURNING NUMBER)` (_jsonAsNumber_), or `JSON_QUERY(expr, path)`
//     (_jsonAsRaw_) -- same JSON_VALUE-is-scalar-only/JSON_QUERY-for-
//     fragments split as MSSQL.
std::string jsonExtractExpr(const std::string & expr, const std::string & jsonSearchPath, int kind,
                             QcDbDriver driver = QcDbDriver::PostgreSQL);

} // namespace QcSqlDialect

// isDistinctFrom()/isNotDistinctFrom() rendering is dialect-branched directly
// in qcsqlqueryelement.cpp rather than here: unlike the helpers above, it
// needs to call back into QcSqlQueryElement's own `bind()` (to append
// operands to the shared parameter list, and on drivers with positional "?"
// placeholders, potentially bind the same value twice -- see the comment at
// its call site) rather than only stitching together pre-rendered strings.

#endif // QCSQLDIALECT_H
