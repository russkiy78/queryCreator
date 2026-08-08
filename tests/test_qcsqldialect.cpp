#include <gtest/gtest.h>

#include "query/qcsqldialect.h"
#include "query/qcsqlbase.h"

// Every dialect function now takes an explicit QcDbDriver -- these tests
// exercise all five drivers in a single run/binary (previously each was an
// #if/#elif chain, only one branch of which ever compiled/ran per build,
// since QC_DB_DRIVER selected exactly one driver at compile time).
namespace {
constexpr QcDbDriver kAllDrivers[] = {
    QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL,
};
} // namespace

TEST(QcSqlDialectPlaceholder, ProducesCorrectSyntaxPerDriver)
{
    EXPECT_EQ(QcSqlDialect::placeholder(1, QcDbDriver::PostgreSQL), "$1");
    EXPECT_EQ(QcSqlDialect::placeholder(2, QcDbDriver::PostgreSQL), "$2");
    EXPECT_EQ(QcSqlDialect::placeholder(10, QcDbDriver::PostgreSQL), "$10");

    EXPECT_EQ(QcSqlDialect::placeholder(1, QcDbDriver::Oracle), ":1");
    EXPECT_EQ(QcSqlDialect::placeholder(2, QcDbDriver::Oracle), ":2");

    for (QcDbDriver driver : {QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        EXPECT_EQ(QcSqlDialect::placeholder(1, driver), "?") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::placeholder(2, driver), "?") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectDataTypeName, ReturnsNonEmptyNameForEveryDataTypeOnEveryDriver)
{
    const int types[] = {
        QcSqlBase::_int_, QcSqlBase::_float_, QcSqlBase::_string_, QcSqlBase::_date_,
        QcSqlBase::_bool_, QcSqlBase::_decimal_, QcSqlBase::_datetime_, QcSqlBase::_text_,
        QcSqlBase::_blob_, QcSqlBase::_json_,
    };
    for (QcDbDriver driver : kAllDrivers) {
        for (int type : types) {
            EXPECT_FALSE(QcSqlDialect::dataTypeName(type, driver).empty()) << "driver=" << static_cast<int>(driver) << " dataType=" << type;
        }
    }
}

TEST(QcSqlDialectDataTypeName, MatchesEachDriverForIntAndString)
{
    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_int_, QcDbDriver::PostgreSQL), "INTEGER");
    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_string_, QcDbDriver::PostgreSQL), "VARCHAR");
    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_blob_, QcDbDriver::PostgreSQL), "BYTEA");

    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_int_, QcDbDriver::MySQL), "SIGNED");
    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_string_, QcDbDriver::MySQL), "CHAR");

    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_int_, QcDbDriver::SQLite), "INTEGER");
    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_string_, QcDbDriver::SQLite), "TEXT");

    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_int_, QcDbDriver::MSSQL), "INT");
    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_string_, QcDbDriver::MSSQL), "VARCHAR(MAX)");

    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_int_, QcDbDriver::Oracle), "NUMBER");
    EXPECT_EQ(QcSqlDialect::dataTypeName(QcSqlBase::_string_, QcDbDriver::Oracle), "VARCHAR2(4000)");
}

TEST(QcSqlDialectLimitOffset, EmptyWhenNeitherRowsNorOffsetRequested)
{
    for (QcDbDriver driver : kAllDrivers) {
        EXPECT_EQ(QcSqlDialect::limitOffsetClause(0, 0, false, driver), "") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectLimitOffset, MatchesEachDriverSyntax)
{
    EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 0, false, QcDbDriver::PostgreSQL), " LIMIT 10");
    EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 20, false, QcDbDriver::PostgreSQL), " LIMIT 10 OFFSET 20");
    EXPECT_EQ(QcSqlDialect::limitOffsetClause(0, 20, false, QcDbDriver::PostgreSQL), " OFFSET 20");

    EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 0, false, QcDbDriver::MySQL), " LIMIT 10");
    EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 20, false, QcDbDriver::MySQL), " LIMIT 10 OFFSET 20");
    // MySQL rejects a bare OFFSET without LIMIT -- its documented "no cap"
    // sentinel (2^64-1) stands in for the omitted row count.
    EXPECT_EQ(QcSqlDialect::limitOffsetClause(0, 20, false, QcDbDriver::MySQL), " LIMIT 18446744073709551615 OFFSET 20");

    EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 0, false, QcDbDriver::SQLite), " LIMIT 10");
    EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 20, false, QcDbDriver::SQLite), " LIMIT 10 OFFSET 20");
    // SQLite rejects a bare OFFSET without LIMIT (verified directly:
    // "SELECT ... OFFSET 1" -> "Error: in prepare, near '1': syntax error")
    // -- "LIMIT -1" is SQLite's documented "no cap" sentinel.
    EXPECT_EQ(QcSqlDialect::limitOffsetClause(0, 20, false, QcDbDriver::SQLite), " LIMIT -1 OFFSET 20");

    for (QcDbDriver driver : {QcDbDriver::MSSQL, QcDbDriver::Oracle}) {
        EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 0, false, driver), " OFFSET 0 ROWS FETCH NEXT 10 ROWS ONLY") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 20, false, driver), " OFFSET 20 ROWS FETCH NEXT 10 ROWS ONLY") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectLimitOffset, WithTiesAlwaysUsesAnsiFetchSyntaxOnEveryDriver)
{
    for (QcDbDriver driver : kAllDrivers) {
        EXPECT_EQ(QcSqlDialect::limitOffsetClause(10, 5, true, driver), " OFFSET 5 ROWS FETCH NEXT 10 ROWS WITH TIES") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectConcatExpr, EmptyForNoOperandsOnEveryDriver)
{
    for (QcDbDriver driver : kAllDrivers) {
        EXPECT_EQ(QcSqlDialect::concatExpr({}, driver), "") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectConcatExpr, SingleOperandMatchesEachDriverSyntax)
{
    // Not a pure pass-through on every driver: the "||" branch (SQLite/
    // Oracle) has nothing to join so it degrades to the bare operand, but
    // the CONCAT(...)-branch drivers still wrap a single operand.
    for (QcDbDriver driver : {QcDbDriver::SQLite, QcDbDriver::Oracle}) {
        EXPECT_EQ(QcSqlDialect::concatExpr({"a"}, driver), "a") << "driver=" << static_cast<int>(driver);
    }
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::MySQL, QcDbDriver::MSSQL}) {
        EXPECT_EQ(QcSqlDialect::concatExpr({"a"}, driver), "CONCAT(a)") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectConcatExpr, MatchesEachDriverSyntax)
{
    for (QcDbDriver driver : {QcDbDriver::SQLite, QcDbDriver::Oracle}) {
        EXPECT_EQ(QcSqlDialect::concatExpr({"a", "b", "c"}, driver), "a || b || c") << "driver=" << static_cast<int>(driver);
    }
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::MySQL, QcDbDriver::MSSQL}) {
        EXPECT_EQ(QcSqlDialect::concatExpr({"a", "b", "c"}, driver), "CONCAT(a, b, c)") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectReturningClause, EmptyWhenNoColumnsRequestedOnEveryDriver)
{
    for (QcDbDriver driver : kAllDrivers) {
        EXPECT_EQ(QcSqlDialect::returningClause({}, "inserted", 1, driver), "") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectReturningClause, MatchesEachDriverSyntax)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::SQLite}) {
        const std::string id = QcSqlDialect::quoteIdentifier("id", driver);
        const std::string name = QcSqlDialect::quoteIdentifier("name", driver);
        EXPECT_EQ(QcSqlDialect::returningClause({"id"}, "inserted", 1, driver), " RETURNING " + id) << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::returningClause({"id", "name"}, "inserted", 1, driver), " RETURNING " + id + ", " + name) << "driver=" << static_cast<int>(driver);
        // mssqlRowKeyword/oracleFirstPlaceholderIndex are ignored on every
        // driver that isn't MSSQL/Oracle respectively.
        EXPECT_EQ(QcSqlDialect::returningClause({"id"}, "deleted", 1, driver), " RETURNING " + id) << "driver=" << static_cast<int>(driver);
    }

    {
        const std::string id = QcSqlDialect::quoteIdentifier("id", QcDbDriver::MSSQL);
        const std::string name = QcSqlDialect::quoteIdentifier("name", QcDbDriver::MSSQL);
        EXPECT_EQ(QcSqlDialect::returningClause({"id"}, "inserted", 1, QcDbDriver::MSSQL), " OUTPUT inserted." + id);
        EXPECT_EQ(QcSqlDialect::returningClause({"id", "name"}, "inserted", 1, QcDbDriver::MSSQL), " OUTPUT inserted." + id + ", inserted." + name);
        EXPECT_EQ(QcSqlDialect::returningClause({"id"}, "deleted", 1, QcDbDriver::MSSQL), " OUTPUT deleted." + id);
    }

    {
        const std::string id = QcSqlDialect::quoteIdentifier("id", QcDbDriver::Oracle);
        const std::string name = QcSqlDialect::quoteIdentifier("name", QcDbDriver::Oracle);
        // RETURNING ... INTO's OUT-bind placeholders continue numbering from
        // wherever the caller's ordinary IN placeholders left off --
        // oracleFirstPlaceholderIndex=3 here simulates 2 IN params already
        // bound, so the first OUT bind is :3, matching placeholder(3).
        EXPECT_EQ(QcSqlDialect::returningClause({"id"}, "inserted", 3, QcDbDriver::Oracle), " RETURNING " + id + " INTO :3");
        EXPECT_EQ(QcSqlDialect::returningClause({"id", "name"}, "inserted", 3, QcDbDriver::Oracle),
                  " RETURNING " + id + ", " + name + " INTO :3, :4");
        // mssqlRowKeyword is ignored on Oracle.
        EXPECT_EQ(QcSqlDialect::returningClause({"id"}, "deleted", 3, QcDbDriver::Oracle), " RETURNING " + id + " INTO :3");
    }

    {
        // No single-statement equivalent at all -- see the doc comment in
        // qcsqldialect.h for the separate follow-up-SELECT mechanism INSERT
        // can use instead (QcSqlInsert::returning()'s autoIncrementColumn
        // overload).
        EXPECT_EQ(QcSqlDialect::returningClause({"id"}, "inserted", 1, QcDbDriver::MySQL), "");
        EXPECT_EQ(QcSqlDialect::returningClause({"id", "name"}, "inserted", 1, QcDbDriver::MySQL), "");
    }
}

TEST(QcSqlDialectQuoteIdentifier, MatchesEachDriverEscapingRule)
{
    // PostgreSQL/SQLite/Oracle: ANSI double quotes, doubled to escape.
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::SQLite, QcDbDriver::Oracle}) {
        EXPECT_EQ(QcSqlDialect::quoteIdentifier("name", driver), "\"name\"") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::quoteIdentifier("a\"b", driver), "\"a\"\"b\"") << "driver=" << static_cast<int>(driver);
    }
    // MySQL: backticks, doubled to escape.
    EXPECT_EQ(QcSqlDialect::quoteIdentifier("name", QcDbDriver::MySQL), "`name`");
    EXPECT_EQ(QcSqlDialect::quoteIdentifier("a`b", QcDbDriver::MySQL), "`a``b`");
    // MSSQL: brackets, doubled to escape.
    EXPECT_EQ(QcSqlDialect::quoteIdentifier("name", QcDbDriver::MSSQL), "[name]");
    EXPECT_EQ(QcSqlDialect::quoteIdentifier("a]b", QcDbDriver::MSSQL), "[a]]b]");
    // "*" and empty are never quoted, on any driver.
    for (QcDbDriver driver : kAllDrivers) {
        EXPECT_EQ(QcSqlDialect::quoteIdentifier("*", driver), "*") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::quoteIdentifier("", driver), "") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectLengthExpr, MatchesEachDriverFunctionName)
{
    EXPECT_EQ(QcSqlDialect::lengthExpr("x", QcDbDriver::MSSQL), "LEN(x)");
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite}) {
        EXPECT_EQ(QcSqlDialect::lengthExpr("x", driver), "LENGTH(x)") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectSubstringExpr, MatchesEachDriverFunctionName)
{
    EXPECT_EQ(QcSqlDialect::substringExpr("x", 2, 5, QcDbDriver::MSSQL), "SUBSTRING(x, 2, 5)");
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite}) {
        EXPECT_EQ(QcSqlDialect::substringExpr("x", 2, 5, driver), "SUBSTR(x, 2, 5)") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectOrderByEntry, NullsDefaultOmitsNullsClauseOnEveryDriver)
{
    for (QcDbDriver driver : kAllDrivers) {
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", false, QcSqlBase::_nullsDefault_, driver), "x ASC") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", true, QcSqlBase::_nullsDefault_, driver), "x DESC") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectOrderByEntry, NullsFirstLastUseNativeSyntaxOnPostgreSqlSqliteOracle)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::SQLite, QcDbDriver::Oracle}) {
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", false, QcSqlBase::_nullsFirst_, driver), "x ASC NULLS FIRST") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", false, QcSqlBase::_nullsLast_, driver), "x ASC NULLS LAST") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", true, QcSqlBase::_nullsFirst_, driver), "x DESC NULLS FIRST") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", true, QcSqlBase::_nullsLast_, driver), "x DESC NULLS LAST") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectOrderByEntry, NullsFirstLastAreEmulatedWithCaseTiebreakerOnMySqlAndMssql)
{
    for (QcDbDriver driver : {QcDbDriver::MySQL, QcDbDriver::MSSQL}) {
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", false, QcSqlBase::_nullsFirst_, driver), "CASE WHEN x IS NULL THEN 0 ELSE 1 END, x ASC") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", true, QcSqlBase::_nullsFirst_, driver), "CASE WHEN x IS NULL THEN 0 ELSE 1 END, x DESC") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", false, QcSqlBase::_nullsLast_, driver), "CASE WHEN x IS NULL THEN 1 ELSE 0 END, x ASC") << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(QcSqlDialect::orderByEntry("x", true, QcSqlBase::_nullsLast_, driver), "CASE WHEN x IS NULL THEN 1 ELSE 0 END, x DESC") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlDialectJsonExtractExpr, TextKindMatchesEachDriverSyntax)
{
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "a.b", QcSqlBase::_jsonAsText_, QcDbDriver::PostgreSQL),
              "jsonb_extract_path_text(m::jsonb, 'a', 'b')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "a.b", QcSqlBase::_jsonAsText_, QcDbDriver::SQLite),
              "json_extract(m, '$.a.b')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "a.b", QcSqlBase::_jsonAsText_, QcDbDriver::MySQL),
              "JSON_UNQUOTE(JSON_EXTRACT(m, '$.a.b'))");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "a.b", QcSqlBase::_jsonAsText_, QcDbDriver::MSSQL),
              "JSON_VALUE(m, '$.a.b')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "a.b", QcSqlBase::_jsonAsText_, QcDbDriver::Oracle),
              "JSON_VALUE(m, '$.a.b')");
}

TEST(QcSqlDialectJsonExtractExpr, NumberKindMatchesEachDriverSyntax)
{
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "rating", QcSqlBase::_jsonAsNumber_, QcDbDriver::PostgreSQL),
              "(jsonb_extract_path_text(m::jsonb, 'rating'))::numeric");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "rating", QcSqlBase::_jsonAsNumber_, QcDbDriver::SQLite),
              "json_extract(m, '$.rating')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "rating", QcSqlBase::_jsonAsNumber_, QcDbDriver::MySQL),
              "(JSON_EXTRACT(m, '$.rating') + 0)");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "rating", QcSqlBase::_jsonAsNumber_, QcDbDriver::MSSQL),
              "CAST(JSON_VALUE(m, '$.rating') AS FLOAT)");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "rating", QcSqlBase::_jsonAsNumber_, QcDbDriver::Oracle),
              "JSON_VALUE(m, '$.rating' RETURNING NUMBER)");
}

TEST(QcSqlDialectJsonExtractExpr, RawKindMatchesEachDriverSyntax)
{
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills", QcSqlBase::_jsonAsRaw_, QcDbDriver::PostgreSQL),
              "(jsonb_extract_path(m::jsonb, 'skills'))::text");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills", QcSqlBase::_jsonAsRaw_, QcDbDriver::SQLite),
              "json_extract(m, '$.skills')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills", QcSqlBase::_jsonAsRaw_, QcDbDriver::MySQL),
              "JSON_EXTRACT(m, '$.skills')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills", QcSqlBase::_jsonAsRaw_, QcDbDriver::MSSQL),
              "JSON_QUERY(m, '$.skills')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills", QcSqlBase::_jsonAsRaw_, QcDbDriver::Oracle),
              "JSON_QUERY(m, '$.skills')");
}

TEST(QcSqlDialectJsonExtractExpr, ArrayIndexSegmentMatchesEachDriverSyntax)
{
    // "skills[0]" -- an object key followed by an array index -- exercises
    // the bracket-tokenizing branch of the path parser (PostgreSQL) and the
    // "$."-prefixing branch (the other four), not just plain dotted keys.
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills[0]", QcSqlBase::_jsonAsText_, QcDbDriver::PostgreSQL),
              "jsonb_extract_path_text(m::jsonb, 'skills', '0')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills[0]", QcSqlBase::_jsonAsText_, QcDbDriver::SQLite),
              "json_extract(m, '$.skills[0]')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills[0]", QcSqlBase::_jsonAsText_, QcDbDriver::MySQL),
              "JSON_UNQUOTE(JSON_EXTRACT(m, '$.skills[0]'))");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills[0]", QcSqlBase::_jsonAsText_, QcDbDriver::MSSQL),
              "JSON_VALUE(m, '$.skills[0]')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "skills[0]", QcSqlBase::_jsonAsText_, QcDbDriver::Oracle),
              "JSON_VALUE(m, '$.skills[0]')");
}

TEST(QcSqlDialectJsonExtractExpr, LeadingArrayIndexPathHasNoDotAfterRootMarker)
{
    // A path that starts with a bracketed index (root-level array) needs
    // "$[0]", not "$.[0]" (invalid JSONPath) -- see toNativeJsonPath() in
    // qcsqldialect.cpp.
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "[0]", QcSqlBase::_jsonAsText_, QcDbDriver::SQLite), "json_extract(m, '$[0]')");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "[0]", QcSqlBase::_jsonAsText_, QcDbDriver::MySQL),
              "JSON_UNQUOTE(JSON_EXTRACT(m, '$[0]'))");
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "[0]", QcSqlBase::_jsonAsText_, QcDbDriver::PostgreSQL),
              "jsonb_extract_path_text(m::jsonb, '0')");
}

TEST(QcSqlDialectJsonExtractExpr, SingleSegmentPathOmitsTrailingComma)
{
    EXPECT_EQ(QcSqlDialect::jsonExtractExpr("m", "level", QcSqlBase::_jsonAsText_, QcDbDriver::PostgreSQL),
              "jsonb_extract_path_text(m::jsonb, 'level')");
}
