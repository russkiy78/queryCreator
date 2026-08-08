#include "qcsqldialect.h"

#include <cctype>

namespace {

// True for a single '.'-separated segment that is safe to quote as a plain
// identifier: starts with a letter/underscore, followed by letters/digits/
// underscore/'$' -- or is the bare "*" wildcard. Anything else (parens,
// operators, embedded quotes/spaces, ...) means the caller handed a raw SQL
// expression through a "column name" parameter rather than an identifier --
// see quoteRef()'s doc comment in qcsqldialect.h for why that has to stay
// possible.
bool isPlainIdentifierSegment(const std::string & segment)
{
    if (segment == "*") {
        return true;
    }
    if (segment.empty() || !(std::isalpha(static_cast<unsigned char>(segment.front())) || segment.front() == '_')) {
        return false;
    }
    for (char ch : segment) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$')) {
            return false;
        }
    }
    return true;
}

// ANSI EXTRACT()/Oracle-interval keyword for a QcSqlBase::datePartTypes
// value (YEAR/MONTH/DAY/HOUR/MINUTE/SECOND) -- shared by every branch below
// that spells the part as a bare SQL keyword (PostgreSQL/MySQL/Oracle's
// EXTRACT, MySQL/Oracle's dateAdd interval unit).
const char * datePartKeyword(int part)
{
    switch (part) {
        case QcSqlBase::_year_: return "YEAR";
        case QcSqlBase::_month_: return "MONTH";
        case QcSqlBase::_day_: return "DAY";
        case QcSqlBase::_hour_: return "HOUR";
        case QcSqlBase::_minute_: return "MINUTE";
        case QcSqlBase::_second_:
        default:
            return "SECOND";
    }
}

// strftime() format specifier for the same enum -- SQLite's only
// date/time-component-extraction primitive (see extractExpr()'s SQLite
// branch).
const char * strftimeSpecifier(int part)
{
    switch (part) {
        case QcSqlBase::_year_: return "%Y";
        case QcSqlBase::_month_: return "%m";
        case QcSqlBase::_day_: return "%d";
        case QcSqlBase::_hour_: return "%H";
        case QcSqlBase::_minute_: return "%M";
        case QcSqlBase::_second_:
        default:
            return "%S";
    }
}

// SQLite datetime() modifier unit -- plural, lowercase (e.g. "days") --
// SQLite's own vocabulary for dateAddExpr()'s SQLite branch, distinct from
// datePartKeyword()'s ANSI singular uppercase form.
const char * sqliteDateAddUnit(int part)
{
    switch (part) {
        case QcSqlBase::_year_: return "years";
        case QcSqlBase::_month_: return "months";
        case QcSqlBase::_day_: return "days";
        case QcSqlBase::_hour_: return "hours";
        case QcSqlBase::_minute_: return "minutes";
        case QcSqlBase::_second_:
        default:
            return "seconds";
    }
}

// MSSQL DATEPART()/DATEADD() datepart keyword -- lowercase by convention
// (MSSQL's datepart keywords are case-insensitive either way, matching
// dateAddExpr()'s MSSQL branch).
const char * mssqlDatePart(int part)
{
    switch (part) {
        case QcSqlBase::_year_: return "year";
        case QcSqlBase::_month_: return "month";
        case QcSqlBase::_day_: return "day";
        case QcSqlBase::_hour_: return "hour";
        case QcSqlBase::_minute_: return "minute";
        case QcSqlBase::_second_:
        default:
            return "second";
    }
}

// Splits a jsonExtractExpr() jsonSearchPath ("a.b[2].c", no leading root
// marker -- see its doc comment in qcsqldialect.h) into its raw
// object-key/array-index segments ("a", "b", "2", "c") -- PostgreSQL's own
// jsonb_extract_path()/jsonb_extract_path_text() take the path as a list of
// separate text arguments rather than one JSONPath string like the other
// four drivers' native JSON functions, so this is the one driver that needs
// the path taken apart. A numeric segment (from "[N]" bracket syntax) is
// passed through as plain text too -- jsonb_extract_path's variadic text[]
// argument uses a segment that looks like an integer as an array index when
// the value at that point in the document actually is an array, and as an
// object key otherwise, so there is nothing PostgreSQL-specific left for
// this function to decide.
std::vector<std::string> parseJsonPathSegments(const std::string & jsonSearchPath)
{
    std::vector<std::string> segments;
    std::string current;
    for (std::size_t i = 0; i < jsonSearchPath.size(); ++i) {
        const char ch = jsonSearchPath[i];
        if (ch == '.') {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
        } else if (ch == '[') {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
            const std::size_t close = jsonSearchPath.find(']', i);
            if (close != std::string::npos) {
                segments.push_back(jsonSearchPath.substr(i + 1, close - i - 1));
                i = close;
            }
        } else {
            current += ch;
        }
    }
    if (!current.empty()) {
        segments.push_back(current);
    }
    return segments;
}

// Prepends the "$"/"$." root marker SQLite/MySQL/MSSQL/Oracle's native JSON
// path functions all require in front of their JSONPath argument --
// jsonExtractExpr()'s own jsonSearchPath convention never carries one (see
// its doc comment in qcsqldialect.h), so every one of those four branches
// needs this instead of splicing the raw path in directly. A path starting
// with "[" (a leading array index, e.g. "[0]") gets "$" with no "."
// in between ("$[0]", not "$.[0]", which would be invalid JSONPath syntax).
std::string toNativeJsonPath(const std::string & jsonSearchPath)
{
    if (jsonSearchPath.empty()) {
        return "$";
    }
    return (jsonSearchPath.front() == '[') ? ("$" + jsonSearchPath) : ("$." + jsonSearchPath);
}

// Single-quoted SQL string literal for `value`, doubling any embedded quote
// to escape it -- the standard SQL escaping rule shared by every driver
// here. Only ever applied to jsonSearchPath segments/the whole path this
// file splices into generated SQL text as a literal, never to caller data
// (which always goes through QcSqlQueryElement's bind() instead).
std::string sqlStringLiteral(const std::string & value)
{
    std::string literal = "'";
    for (char ch : value) {
        literal += ch;
        if (ch == '\'') {
            literal += ch;
        }
    }
    literal += '\'';
    return literal;
}

} // namespace

namespace QcSqlDialect {

std::string placeholder(std::size_t index, QcDbDriver driver)
{
    switch (driver) {
        case QcDbDriver::PostgreSQL:
            return "$" + std::to_string(index);
        case QcDbDriver::Oracle:
            return ":" + std::to_string(index);
        case QcDbDriver::MySQL:
        case QcDbDriver::SQLite:
        case QcDbDriver::MSSQL:
        default:
            // Positional, unnamed -- both the MySQL C API's prepared statements and
            // raw ODBC (MSSQL) bind parameters this way; SQLite accepts plain "?"
            // too (as well as "?NNN", which isn't needed here).
            (void)index;
            return "?";
    }
}

std::string dataTypeName(int dataType, QcDbDriver driver)
{
    switch (driver) {
        case QcDbDriver::PostgreSQL:
            switch (dataType) {
                case QcSqlBase::_int_: return "INTEGER";
                case QcSqlBase::_float_: return "DOUBLE PRECISION";
                case QcSqlBase::_string_: return "VARCHAR";
                case QcSqlBase::_date_: return "DATE";
                case QcSqlBase::_bool_: return "BOOLEAN";
                case QcSqlBase::_decimal_: return "DECIMAL";
                case QcSqlBase::_datetime_: return "TIMESTAMP";
                case QcSqlBase::_text_: return "TEXT";
                case QcSqlBase::_blob_: return "BYTEA";
                case QcSqlBase::_json_: return "JSON";
                default: return "TEXT";
            }
        case QcDbDriver::MySQL:
            // MySQL's CAST() only accepts a narrow, fixed list of target types
            // (BINARY/CHAR/DATE/DATETIME/DECIMAL/DOUBLE/FLOAT/JSON/NCHAR/REAL/
            // SIGNED/TIME/UNSIGNED) -- there is no VARCHAR/TEXT/BOOLEAN/BLOB target,
            // so those map onto the closest allowed keyword rather than failing.
            switch (dataType) {
                case QcSqlBase::_int_: return "SIGNED";
                case QcSqlBase::_float_: return "DOUBLE";
                case QcSqlBase::_string_: return "CHAR";
                case QcSqlBase::_date_: return "DATE";
                case QcSqlBase::_bool_: return "SIGNED";
                case QcSqlBase::_decimal_: return "DECIMAL";
                case QcSqlBase::_datetime_: return "DATETIME";
                case QcSqlBase::_text_: return "CHAR";
                case QcSqlBase::_blob_: return "BINARY";
                case QcSqlBase::_json_: return "JSON";
                default: return "CHAR";
            }
        case QcDbDriver::SQLite:
            // SQLite CAST() assigns one of 5 storage-class affinities by substring
            // matching on the type name (see sqlite.org/datatype3.html#affname) --
            // any name containing the right substring works, these are just
            // conventional spellings for readability.
            switch (dataType) {
                case QcSqlBase::_int_: return "INTEGER";
                case QcSqlBase::_float_: return "REAL";
                case QcSqlBase::_string_: return "TEXT";
                case QcSqlBase::_date_: return "TEXT";
                case QcSqlBase::_bool_: return "INTEGER";
                case QcSqlBase::_decimal_: return "NUMERIC";
                case QcSqlBase::_datetime_: return "TEXT";
                case QcSqlBase::_text_: return "TEXT";
                case QcSqlBase::_blob_: return "BLOB";
                case QcSqlBase::_json_: return "TEXT";
                default: return "TEXT";
            }
        case QcDbDriver::MSSQL:
            // VARCHAR(MAX)/DECIMAL(18,2) below are reasonable defaults, not the only
            // valid choice -- MSSQL's CAST requires an explicit length/precision for
            // these, and the builder doesn't currently have a way to ask for a
            // different one.
            switch (dataType) {
                case QcSqlBase::_int_: return "INT";
                case QcSqlBase::_float_: return "FLOAT";
                case QcSqlBase::_string_: return "VARCHAR(MAX)";
                case QcSqlBase::_date_: return "DATE";
                case QcSqlBase::_bool_: return "BIT";
                case QcSqlBase::_decimal_: return "DECIMAL(18,2)";
                case QcSqlBase::_datetime_: return "DATETIME2";
                case QcSqlBase::_text_: return "VARCHAR(MAX)";
                case QcSqlBase::_blob_: return "VARBINARY(MAX)";
                case QcSqlBase::_json_: return "NVARCHAR(MAX)"; // no native JSON type
                default: return "VARCHAR(MAX)";
            }
        case QcDbDriver::Oracle:
        default:
            // VARCHAR2(4000) below is a reasonable default, not the only valid
            // choice -- Oracle's CAST requires an explicit length, and the builder
            // doesn't currently have a way to ask for a different one. Oracle SQL
            // has no BOOLEAN type at all (only in PL/SQL) -- NUMBER(1) is the
            // conventional 0/1 stand-in.
            switch (dataType) {
                case QcSqlBase::_int_: return "NUMBER";
                case QcSqlBase::_float_: return "BINARY_DOUBLE";
                case QcSqlBase::_string_: return "VARCHAR2(4000)";
                case QcSqlBase::_date_: return "DATE";
                case QcSqlBase::_bool_: return "NUMBER(1)";
                case QcSqlBase::_decimal_: return "NUMBER";
                case QcSqlBase::_datetime_: return "TIMESTAMP";
                case QcSqlBase::_text_: return "CLOB";
                case QcSqlBase::_blob_: return "BLOB";
                case QcSqlBase::_json_: return "CLOB"; // conservative; 21c+ has a native JSON type
                default: return "VARCHAR2(4000)";
            }
    }
}

std::string limitOffsetClause(int rowsCount, int startRow, bool withTies, QcDbDriver driver)
{
    if (rowsCount <= 0 && startRow <= 0) {
        return {};
    }

    if (driver == QcDbDriver::PostgreSQL && !withTies) {
        std::string clause;
        if (rowsCount > 0) {
            clause += " LIMIT " + std::to_string(rowsCount);
        }
        if (startRow > 0) {
            clause += " OFFSET " + std::to_string(startRow);
        }
        return clause;
    }
    if ((driver == QcDbDriver::MySQL || driver == QcDbDriver::SQLite) && !withTies) {
        // Unlike PostgreSQL, neither of these accepts a bare OFFSET without a
        // LIMIT (verified directly against SQLite: "SELECT ... OFFSET 1" fails
        // with "Error: in prepare, near '1': syntax error") -- when only an
        // offset was requested, both have a documented "no cap" sentinel for
        // LIMIT to pair it with instead of omitting LIMIT.
        std::string clause;
        if (rowsCount > 0) {
            clause += " LIMIT " + std::to_string(rowsCount);
        } else if (startRow > 0) {
            clause += (driver == QcDbDriver::MySQL)
                ? " LIMIT 18446744073709551615" // MySQL's documented "no limit" sentinel (2^64-1)
                : " LIMIT -1"; // SQLite's documented "no limit" sentinel (any negative LIMIT)
        }
        if (startRow > 0) {
            clause += " OFFSET " + std::to_string(startRow);
        }
        return clause;
    }

    // ANSI OFFSET/FETCH form -- the only form MSSQL/Oracle support, and the
    // only form (on any of the five drivers) that can express WITH TIES.
    // FETCH NEXT/FIRST requires ORDER BY on most engines; the builder
    // doesn't enforce that (it's a caller responsibility, not something
    // toSql() can validate without rejecting otherwise-valid queries that
    // add ORDER BY separately).
    std::string clause = " OFFSET " + std::to_string(startRow > 0 ? startRow : 0) + " ROWS";
    if (rowsCount > 0) {
        clause += " FETCH NEXT " + std::to_string(rowsCount) + " ROWS " + (withTies ? "WITH TIES" : "ONLY");
    }
    return clause;
}

std::string concatExpr(const std::vector<std::string> & operands, QcDbDriver driver)
{
    if (operands.empty()) {
        return {};
    }

    if (driver == QcDbDriver::SQLite || driver == QcDbDriver::Oracle) {
        std::string expr = operands[0];
        for (std::size_t i = 1; i < operands.size(); ++i) {
            expr += " || " + operands[i];
        }
        return expr;
    }

    std::string args = operands[0];
    for (std::size_t i = 1; i < operands.size(); ++i) {
        args += ", " + operands[i];
    }
    return "CONCAT(" + args + ")";
}

std::string returningClause(const QcSqlBase::QcStringList & columns, const std::string & mssqlRowKeyword,
                             std::size_t oracleFirstPlaceholderIndex, QcDbDriver driver)
{
    if (columns.empty()) {
        return {};
    }

    switch (driver) {
        case QcDbDriver::PostgreSQL:
        case QcDbDriver::SQLite: {
            (void)oracleFirstPlaceholderIndex;
            (void)mssqlRowKeyword;
            std::string clause = " RETURNING ";
            for (std::size_t i = 0; i < columns.size(); ++i) {
                if (i > 0) {
                    clause += ", ";
                }
                clause += quoteIdentifier(columns[i], driver);
            }
            return clause;
        }
        case QcDbDriver::MSSQL: {
            (void)oracleFirstPlaceholderIndex;
            std::string clause = " OUTPUT ";
            for (std::size_t i = 0; i < columns.size(); ++i) {
                if (i > 0) {
                    clause += ", ";
                }
                // mssqlRowKeyword ("inserted"/"deleted") is an internal constant
                // supplied by qcsqlinsert.cpp/qcsqlupdate.cpp/qcsqldelete.cpp, never
                // caller data -- only the column name is a caller-supplied
                // identifier that needs quoting.
                clause += mssqlRowKeyword + "." + quoteIdentifier(columns[i], driver);
            }
            return clause;
        }
        case QcDbDriver::Oracle: {
            (void)mssqlRowKeyword;
            std::string clause = " RETURNING ";
            for (std::size_t i = 0; i < columns.size(); ++i) {
                if (i > 0) {
                    clause += ", ";
                }
                clause += quoteIdentifier(columns[i], driver);
            }
            clause += " INTO ";
            for (std::size_t i = 0; i < columns.size(); ++i) {
                if (i > 0) {
                    clause += ", ";
                }
                clause += placeholder(oracleFirstPlaceholderIndex + i, driver);
            }
            return clause;
        }
        case QcDbDriver::MySQL:
        default:
            // MySQL: no single-statement equivalent at all -- see the doc comment in
            // qcsqldialect.h for the separate, caller-side follow-up-SELECT
            // mechanism INSERT can use instead.
            (void)mssqlRowKeyword;
            (void)oracleFirstPlaceholderIndex;
            return {};
    }
}

std::string quoteIdentifier(const std::string & name, QcDbDriver driver)
{
    if (name.empty() || name == "*") {
        return name;
    }

    switch (driver) {
        case QcDbDriver::MySQL: {
            std::string quoted;
            quoted.reserve(name.size() + 2);
            quoted += '`';
            for (char ch : name) {
                quoted += ch;
                if (ch == '`') {
                    quoted += ch;
                }
            }
            quoted += '`';
            return quoted;
        }
        case QcDbDriver::MSSQL: {
            std::string quoted;
            quoted.reserve(name.size() + 2);
            quoted += '[';
            for (char ch : name) {
                quoted += ch;
                if (ch == ']') {
                    quoted += ch;
                }
            }
            quoted += ']';
            return quoted;
        }
        case QcDbDriver::PostgreSQL:
        case QcDbDriver::SQLite:
        case QcDbDriver::Oracle:
        default: {
            // PostgreSQL/SQLite/Oracle -- ANSI double-quote, doubled to escape an
            // embedded quote character, case preserved exactly as given. Oracle's
            // quoted identifiers are case-sensitive ANSI SQL, identical in spirit to
            // PostgreSQL's -- the only Oracle-specific consequence of quoting
            // consistently is that a *schema* built the ordinary way (unquoted DDL,
            // which Oracle folds to UPPERCASE at creation time, e.g. `CREATE TABLE
            // qc_bt_departments (...)` really creates `QC_BT_DEPARTMENTS`) has to be
            // (re)created with quoted, case-matching DDL too, so its catalog names
            // agree with what this function emits -- exactly the same requirement
            // PostgreSQL/SQLite already have, not a reason to special-case Oracle's
            // quoting behavior itself. See qcsqldialect.h and
            // test_integration_select.cpp/test_integration_dml.cpp's Oracle DDL
            // branches (quoted, lowercase-matching) for the actual fix.
            std::string quoted;
            quoted.reserve(name.size() + 2);
            quoted += '"';
            for (char ch : name) {
                quoted += ch;
                if (ch == '"') {
                    quoted += ch;
                }
            }
            quoted += '"';
            return quoted;
        }
    }
}

std::string quoteRef(const std::string & name, QcDbDriver driver)
{
    if (name.empty()) {
        return name;
    }

    std::vector<std::string> segments;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = name.find('.', start);
        segments.push_back((dot == std::string::npos) ? name.substr(start) : name.substr(start, dot - start));
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }

    // Only quote if *every* segment looks like a plain identifier -- a
    // "column name" parameter that isn't (contains parens, operators,
    // spaces, ...) is a raw SQL expression smuggled through, and gets
    // returned untouched rather than mangled by partial/bogus quoting. See
    // the doc comment on the declaration in qcsqldialect.h.
    for (const std::string & segment : segments) {
        if (!isPlainIdentifierSegment(segment)) {
            return name;
        }
    }

    std::string result;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            result += '.';
        }
        result += quoteIdentifier(segments[i], driver);
    }
    return result;
}

std::string quoteTableRef(const std::string & name, QcDbDriver driver)
{
    const std::size_t space = name.find(' ');
    if (space == std::string::npos) {
        return quoteRef(name, driver);
    }

    const std::string table = name.substr(0, space);
    const std::size_t aliasStart = name.find_first_not_of(' ', space);
    if (aliasStart == std::string::npos) {
        // Trailing whitespace with nothing after it -- treat as if there
        // were no alias at all rather than quoting an empty identifier.
        return quoteRef(table, driver);
    }
    const std::string alias = name.substr(aliasStart);
    return quoteRef(table, driver) + " " + quoteIdentifier(alias, driver);
}

std::string lengthExpr(const std::string & expr, QcDbDriver driver)
{
    if (driver == QcDbDriver::MSSQL) {
        return "LEN(" + expr + ")";
    }
    return "LENGTH(" + expr + ")";
}

std::string substringExpr(const std::string & expr, int start, int length, QcDbDriver driver)
{
    if (driver == QcDbDriver::MSSQL) {
        return "SUBSTRING(" + expr + ", " + std::to_string(start) + ", " + std::to_string(length) + ")";
    }
    return "SUBSTR(" + expr + ", " + std::to_string(start) + ", " + std::to_string(length) + ")";
}

std::string extractExpr(const std::string & expr, int part, QcDbDriver driver)
{
    switch (driver) {
        case QcDbDriver::MSSQL:
            return "DATEPART(" + std::string(mssqlDatePart(part)) + ", " + expr + ")";
        case QcDbDriver::SQLite:
            // strftime() returns text -- CAST to INTEGER so the result is numeric
            // like every other driver's EXTRACT/DATEPART, not a zero-padded string.
            return "CAST(strftime('" + std::string(strftimeSpecifier(part)) + "', " + expr + ") AS INTEGER)";
        case QcDbDriver::PostgreSQL:
        case QcDbDriver::MySQL:
        case QcDbDriver::Oracle:
        default:
            // PostgreSQL/MySQL/Oracle -- ANSI EXTRACT(part FROM expr).
            return "EXTRACT(" + std::string(datePartKeyword(part)) + " FROM " + expr + ")";
    }
}

std::string dateAddExpr(const std::string & expr, int part, int amount, QcDbDriver driver)
{
    switch (driver) {
        case QcDbDriver::PostgreSQL:
            return expr + " + INTERVAL '" + std::to_string(amount) + " " + std::string(datePartKeyword(part)) + "'";
        case QcDbDriver::MySQL:
            return "DATE_ADD(" + expr + ", INTERVAL " + std::to_string(amount) + " " + std::string(datePartKeyword(part)) + ")";
        case QcDbDriver::SQLite: {
            // SQLite's datetime() modifier syntax requires an explicit sign, unlike
            // the literal-integer amount every other driver's form accepts as-is.
            const std::string signedAmount = (amount >= 0 ? "+" : "") + std::to_string(amount);
            return "datetime(" + expr + ", '" + signedAmount + " " + std::string(sqliteDateAddUnit(part)) + "')";
        }
        case QcDbDriver::MSSQL:
            return "DATEADD(" + std::string(mssqlDatePart(part)) + ", " + std::to_string(amount) + ", " + expr + ")";
        case QcDbDriver::Oracle:
        default:
            // Unlike the ANSI standard (where a literal INTERVAL only covers
            // day/hour/minute/second), Oracle also accepts MONTH/YEAR here --
            // verified directly (`DATE '2024-01-01' + INTERVAL '1' MONTH` succeeds)
            // -- so no ADD_MONTHS()-style fallback is needed for those two.
            return expr + " + INTERVAL '" + std::to_string(amount) + "' " + std::string(datePartKeyword(part));
    }
}

std::string orderByEntry(const std::string & column, bool descending, int nulls, QcDbDriver driver)
{
    const char * const directionKeyword = descending ? " DESC" : " ASC";

    switch (driver) {
        case QcDbDriver::PostgreSQL:
        case QcDbDriver::SQLite:
        case QcDbDriver::Oracle: {
            std::string entry = column + directionKeyword;
            if (nulls == QcSqlBase::_nullsFirst_) {
                entry += " NULLS FIRST";
            } else if (nulls == QcSqlBase::_nullsLast_) {
                entry += " NULLS LAST";
            }
            return entry;
        }
        case QcDbDriver::MySQL:
        case QcDbDriver::MSSQL:
        default:
            if (nulls == QcSqlBase::_nullsFirst_) {
                return "CASE WHEN " + column + " IS NULL THEN 0 ELSE 1 END, " + column + directionKeyword;
            }
            if (nulls == QcSqlBase::_nullsLast_) {
                return "CASE WHEN " + column + " IS NULL THEN 1 ELSE 0 END, " + column + directionKeyword;
            }
            return column + directionKeyword;
    }
}

std::string jsonExtractExpr(const std::string & expr, const std::string & jsonSearchPath, int kind, QcDbDriver driver)
{
    switch (driver) {
        case QcDbDriver::PostgreSQL: {
            std::string args = expr + "::jsonb";
            for (const std::string & segment : parseJsonPathSegments(jsonSearchPath)) {
                args += ", " + sqlStringLiteral(segment);
            }
            switch (kind) {
                case QcSqlBase::_jsonAsNumber_:
                    return "(jsonb_extract_path_text(" + args + "))::numeric";
                case QcSqlBase::_jsonAsRaw_:
                    return "(jsonb_extract_path(" + args + "))::text";
                case QcSqlBase::_jsonAsText_:
                default:
                    return "jsonb_extract_path_text(" + args + ")";
            }
        }
        case QcDbDriver::SQLite:
            // json_extract() already returns a scalar JSON string/number
            // fully unwrapped (real numeric affinity for numbers) and an
            // object/array's own serialized JSON text otherwise -- exactly
            // what all three `kind`s need, no further wrapping required.
            return "json_extract(" + expr + ", " + sqlStringLiteral(toNativeJsonPath(jsonSearchPath)) + ")";
        case QcDbDriver::MySQL: {
            const std::string extract = "JSON_EXTRACT(" + expr + ", " + sqlStringLiteral(toNativeJsonPath(jsonSearchPath)) + ")";
            switch (kind) {
                case QcSqlBase::_jsonAsNumber_:
                    // "+0" forces numeric context -- MySQL's own idiom for
                    // comparing/reading an extracted JSON scalar numerically
                    // instead of lexicographically.
                    return "(" + extract + " + 0)";
                case QcSqlBase::_jsonAsText_:
                    // "->>' equivalent -- JSON_UNQUOTE(JSON_EXTRACT(...))
                    // strips the quotes JSON_EXTRACT alone would leave
                    // around a JSON string scalar.
                    return "JSON_UNQUOTE(" + extract + ")";
                case QcSqlBase::_jsonAsRaw_:
                default:
                    return extract;
            }
        }
        case QcDbDriver::MSSQL: {
            const std::string path = sqlStringLiteral(toNativeJsonPath(jsonSearchPath));
            switch (kind) {
                case QcSqlBase::_jsonAsNumber_:
                    return "CAST(JSON_VALUE(" + expr + ", " + path + ") AS FLOAT)";
                case QcSqlBase::_jsonAsRaw_:
                    // JSON_VALUE is scalar-only (NULL on an object/array
                    // path) -- JSON_QUERY is MSSQL's counterpart for
                    // extracting an object/array fragment as its own
                    // serialized JSON text.
                    return "JSON_QUERY(" + expr + ", " + path + ")";
                case QcSqlBase::_jsonAsText_:
                default:
                    return "JSON_VALUE(" + expr + ", " + path + ")";
            }
        }
        case QcDbDriver::Oracle:
        default: {
            const std::string path = sqlStringLiteral(toNativeJsonPath(jsonSearchPath));
            switch (kind) {
                case QcSqlBase::_jsonAsNumber_:
                    // Oracle's JSON_VALUE defaults to VARCHAR2 -- RETURNING
                    // NUMBER asks for a real numeric type instead, same
                    // purpose as MSSQL's CAST(... AS FLOAT) above.
                    return "JSON_VALUE(" + expr + ", " + path + " RETURNING NUMBER)";
                case QcSqlBase::_jsonAsRaw_:
                    // Same JSON_VALUE-is-scalar-only/JSON_QUERY-for-
                    // fragments split as MSSQL above.
                    return "JSON_QUERY(" + expr + ", " + path + ")";
                case QcSqlBase::_jsonAsText_:
                default:
                    return "JSON_VALUE(" + expr + ", " + path + ")";
            }
        }
    }
}

} // namespace QcSqlDialect
