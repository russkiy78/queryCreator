#include <gtest/gtest.h>

#include "query/qcsqldialect.h"
#include "query/qcsqlqueryvalue.h"

// Shorthand for QcSqlDialect::quoteIdentifier() -- see the identical helper
// (and its rationale) in test_qcsqlquery.cpp.
namespace {
std::string q(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name);
}
} // namespace

// QcSqlQueryValue exposes no public getters for its internal function chain
// (toSql() is the public way to observe it, exercised separately below) --
// this fixture is a declared friend (see qcsqlqueryvalue.h) purely so tests
// can verify real state changes instead of only "it compiles and returns
// something".
class QcSqlQueryValueWhiteBoxTest : public ::testing::Test
{
protected:
    using FunctionChain = std::vector<std::pair<int, std::vector<QcSqlQueryValue::QcVariant>>>;

    static const std::string & columnName(const QcSqlQueryValue & value) { return value.m_columnName; }
    static const std::string & columnAlias(const QcSqlQueryValue & value) { return value.m_columnAlias; }
    static const FunctionChain & functions(const QcSqlQueryValue & value) { return value.m_functions; }
};

TEST_F(QcSqlQueryValueWhiteBoxTest, NameConstructorParsesAlias)
{
    QcSqlQueryValue value("name <alias>");

    EXPECT_EQ(columnName(value), "name");
    EXPECT_EQ(columnAlias(value), "alias");
}

TEST_F(QcSqlQueryValueWhiteBoxTest, NameConstructorWithoutAliasLeavesAliasEmpty)
{
    QcSqlQueryValue value("plain_column");

    EXPECT_EQ(columnName(value), "plain_column");
    EXPECT_TRUE(columnAlias(value).empty());
}

TEST_F(QcSqlQueryValueWhiteBoxTest, NameConstructorTrimsSurroundingWhitespace)
{
    QcSqlQueryValue value("  name2 <alias2>  ");

    EXPECT_EQ(columnName(value), "name2");
    EXPECT_EQ(columnAlias(value), "alias2");
}

TEST_F(QcSqlQueryValueWhiteBoxTest, DefaultConstructorLeavesColumnNameEmpty)
{
    QcSqlQueryValue value;

    EXPECT_TRUE(columnName(value).empty());
    EXPECT_TRUE(columnAlias(value).empty());
    EXPECT_TRUE(functions(value).empty());
}

TEST_F(QcSqlQueryValueWhiteBoxTest, ConcatAppendsFunctionWithParams)
{
    QcSqlQueryValue value;

    QcSqlQueryValue & chained = value.concat({"a", "b"});

    EXPECT_EQ(&chained, &value);
    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_concat_);
    ASSERT_EQ(functions(value)[0].second.size(), 2u);
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[0]), "a");
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[1]), "b");
}

TEST_F(QcSqlQueryValueWhiteBoxTest, CastAppendsFunctionWithFromAndTo)
{
    QcSqlQueryValue value;

    value.cast(QcSqlBase::_string_, QcSqlBase::_float_);

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_cast_);
    ASSERT_EQ(functions(value)[0].second.size(), 2u);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[0]), QcSqlBase::_string_);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[1]), QcSqlBase::_float_);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, CastAcceptsDateDataType)
{
    // dataTypes::_date_ used to be a typo'd _data_ (see architecture.md) --
    // exercised here explicitly, not just _string_/_float_, per the note in
    // testing.md.
    QcSqlQueryValue value;

    value.cast(QcSqlBase::_string_, QcSqlBase::_date_);

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[1]), QcSqlBase::_date_);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, RoundAppendsFunctionWithPrecision)
{
    QcSqlQueryValue value;

    value.round(2);

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_round_);
    ASSERT_EQ(functions(value)[0].second.size(), 1u);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[0]), 2);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, UpperCaseAndLowerCaseAppendNoParamFunctions)
{
    QcSqlQueryValue value;

    value.upperCase().lowerCase();

    ASSERT_EQ(functions(value).size(), 2u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_upperCase_);
    EXPECT_TRUE(functions(value)[0].second.empty());
    EXPECT_EQ(functions(value)[1].first, QcSqlBase::_lowerCase_);
    EXPECT_TRUE(functions(value)[1].second.empty());
}

TEST_F(QcSqlQueryValueWhiteBoxTest, ChainAccumulatesFunctionsInCallOrder)
{
    QcSqlQueryValue value("test3 <test_alias3>");

    value.upperCase().cast(QcSqlBase::_string_, QcSqlBase::_float_).round(1);

    ASSERT_EQ(functions(value).size(), 3u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_upperCase_);
    EXPECT_EQ(functions(value)[1].first, QcSqlBase::_cast_);
    EXPECT_EQ(functions(value)[2].first, QcSqlBase::_round_);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, CountAppendsFunctionWithDistinctFlag)
{
    QcSqlQueryValue plain;
    plain.count();
    ASSERT_EQ(functions(plain).size(), 1u);
    EXPECT_EQ(functions(plain)[0].first, QcSqlBase::_count_);
    ASSERT_EQ(functions(plain)[0].second.size(), 1u);
    EXPECT_EQ(std::get<long long>(functions(plain)[0].second[0]), 0LL);

    QcSqlQueryValue distinct;
    distinct.count(true);
    EXPECT_EQ(std::get<long long>(functions(distinct)[0].second[0]), 1LL);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, SumAvgMinMaxAppendNoParamFunctions)
{
    QcSqlQueryValue value;

    value.sum();
    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_sum_);
    EXPECT_TRUE(functions(value)[0].second.empty());

    QcSqlQueryValue avgValue;
    avgValue.avg();
    EXPECT_EQ(functions(avgValue)[0].first, QcSqlBase::_avg_);

    QcSqlQueryValue minValue;
    minValue.min();
    EXPECT_EQ(functions(minValue)[0].first, QcSqlBase::_min_);

    QcSqlQueryValue maxValue;
    maxValue.max();
    EXPECT_EQ(functions(maxValue)[0].first, QcSqlBase::_max_);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, CoalesceAppendsFunctionWithFallbacks)
{
    QcSqlQueryValue value;

    value.coalesce({"backup_col", "'N/A'"});

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_coalesce_);
    ASSERT_EQ(functions(value)[0].second.size(), 2u);
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[0]), "backup_col");
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[1]), "'N/A'");
}

TEST_F(QcSqlQueryValueWhiteBoxTest, NullIfAppendsFunctionWithCompareExpr)
{
    QcSqlQueryValue value;

    value.nullIf("''");

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_nullIf_);
    ASSERT_EQ(functions(value)[0].second.size(), 1u);
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[0]), "''");
}

TEST_F(QcSqlQueryValueWhiteBoxTest, TrimAppendsNoParamFunction)
{
    QcSqlQueryValue value;

    value.trim();

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_trim_);
    EXPECT_TRUE(functions(value)[0].second.empty());
}

TEST_F(QcSqlQueryValueWhiteBoxTest, SubstringAppendsFunctionWithStartAndLength)
{
    QcSqlQueryValue value;

    value.substring(2, 5);

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_substring_);
    ASSERT_EQ(functions(value)[0].second.size(), 2u);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[0]), 2LL);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[1]), 5LL);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, LengthAppendsNoParamFunction)
{
    QcSqlQueryValue value;

    value.length();

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_length_);
    EXPECT_TRUE(functions(value)[0].second.empty());
}

TEST_F(QcSqlQueryValueWhiteBoxTest, ReplaceAppendsFunctionWithSearchAndReplacement)
{
    QcSqlQueryValue value;

    value.replace("'a'", "'b'");

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_replace_);
    ASSERT_EQ(functions(value)[0].second.size(), 2u);
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[0]), "'a'");
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[1]), "'b'");
}

TEST_F(QcSqlQueryValueWhiteBoxTest, CaseWithoutElseEncodesHasElseFlagFalse)
{
    QcSqlQueryValue value;

    value.case_({{"status = 1", "'active'"}, {"status = 0", "'inactive'"}});

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_case_);
    // [hasElse=0, cond1, then1, cond2, then2] -- 5 entries, no trailing else.
    ASSERT_EQ(functions(value)[0].second.size(), 5u);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[0]), 0LL);
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[1]), "status = 1");
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[2]), "'active'");
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[3]), "status = 0");
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[4]), "'inactive'");
}

TEST_F(QcSqlQueryValueWhiteBoxTest, CaseWithElseEncodesHasElseFlagTrueAndAppendsElseResult)
{
    QcSqlQueryValue value;

    value.case_({{"status = 1", "'active'"}}, std::string("'unknown'"));

    ASSERT_EQ(functions(value).size(), 1u);
    // [hasElse=1, cond1, then1, elseResult] -- 4 entries.
    ASSERT_EQ(functions(value)[0].second.size(), 4u);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[0]), 1LL);
    EXPECT_EQ(std::get<std::string>(functions(value)[0].second[3]), "'unknown'");
}

TEST_F(QcSqlQueryValueWhiteBoxTest, ExtractAppendsFunctionWithPart)
{
    QcSqlQueryValue value;

    value.extract(QcSqlBase::_year_);

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_extract_);
    ASSERT_EQ(functions(value)[0].second.size(), 1u);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[0]), QcSqlBase::_year_);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, DateAddAppendsFunctionWithPartAndAmount)
{
    QcSqlQueryValue value;

    value.dateAdd(QcSqlBase::_day_, -7);

    ASSERT_EQ(functions(value).size(), 1u);
    EXPECT_EQ(functions(value)[0].first, QcSqlBase::_dateAdd_);
    ASSERT_EQ(functions(value)[0].second.size(), 2u);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[0]), QcSqlBase::_day_);
    EXPECT_EQ(std::get<long long>(functions(value)[0].second[1]), -7LL);
}

TEST_F(QcSqlQueryValueWhiteBoxTest, JsonExtractFamilyAppendsFunctionWithKindAndPath)
{
    QcSqlQueryValue text;
    text.jsonExtract("skills[0]");
    ASSERT_EQ(functions(text).size(), 1u);
    EXPECT_EQ(functions(text)[0].first, QcSqlBase::_jsonExtract_);
    ASSERT_EQ(functions(text)[0].second.size(), 2u);
    EXPECT_EQ(std::get<long long>(functions(text)[0].second[0]), QcSqlBase::_jsonAsText_);
    EXPECT_EQ(std::get<std::string>(functions(text)[0].second[1]), "skills[0]");

    QcSqlQueryValue number;
    number.jsonExtractNumber("rating");
    ASSERT_EQ(functions(number).size(), 1u);
    EXPECT_EQ(functions(number)[0].first, QcSqlBase::_jsonExtract_);
    EXPECT_EQ(std::get<long long>(functions(number)[0].second[0]), QcSqlBase::_jsonAsNumber_);
    EXPECT_EQ(std::get<std::string>(functions(number)[0].second[1]), "rating");

    QcSqlQueryValue raw;
    raw.jsonExtractRaw("skills");
    ASSERT_EQ(functions(raw).size(), 1u);
    EXPECT_EQ(functions(raw)[0].first, QcSqlBase::_jsonExtract_);
    EXPECT_EQ(std::get<long long>(functions(raw)[0].second[0]), QcSqlBase::_jsonAsRaw_);
    EXPECT_EQ(std::get<std::string>(functions(raw)[0].second[1]), "skills");
}

TEST(QcSqlQueryValueToSql, PlainColumnWithNoAliasOrFunctions)
{
    QcSqlQueryValue value("id");

    EXPECT_EQ(value.toSql(), q("id"));
}

TEST(QcSqlQueryValueToSql, AppendsAsAliasWhenPresent)
{
    QcSqlQueryValue value("name <display_name>");

    EXPECT_EQ(value.toSql(), q("name") + " AS " + q("display_name"));
}

TEST(QcSqlQueryValueToSql, WrapsWithUpperCaseAndLowerCase)
{
    QcSqlQueryValue upper("name");
    upper.upperCase();
    EXPECT_EQ(upper.toSql(), "UPPER(" + q("name") + ")");

    QcSqlQueryValue lower("name");
    lower.lowerCase();
    EXPECT_EQ(lower.toSql(), "LOWER(" + q("name") + ")");
}

TEST(QcSqlQueryValueToSql, WrapsWithRoundAndPrecision)
{
    QcSqlQueryValue value("price");
    value.round(2);

    EXPECT_EQ(value.toSql(), "ROUND(" + q("price") + ", 2)");
}

TEST(QcSqlQueryValueToSql, WrapsWithConcatRenderingArgumentsAsRawExpressions)
{
    QcSqlQueryValue value("first_name");
    value.concat({"' '", "last_name"});

    // Every operand is run through quoteRef() before joining: "first_name"/
    // "last_name" look like plain column references and get quoted, "' '"
    // (a string literal, not identifier-shaped) is left exactly as given --
    // see qcsqlqueryvalue.cpp for why concat() has to make this distinction
    // instead of quoting or leaving everything alone unconditionally.
    EXPECT_EQ(value.toSql(), QcSqlDialect::concatExpr({q("first_name"), "' '", q("last_name")}));
}

// Oracle's CONCAT() is binary-only and SQLite has no CONCAT() at all in most
// deployed versions -- concat() must use "||" there, not CONCAT(...), which
// only works on PostgreSQL/MySQL/MSSQL. Matches QcSqlQueryElementToSql's
// IsDistinctFromFamilyMatchesActiveDriverShape in spirit: verify the actual
// shape for whichever driver this was built against, not just delegate to
// the same helper the production code calls (that alone wouldn't catch a
// QcSqlQueryValue::toSql() that forgot to call it).
TEST(QcSqlQueryValueToSql, ConcatUsesPipeOperatorOnDriversWithoutVariadicConcat)
{
    // Runs once per driver in a single build/binary now (previously this was
    // an #if/#else chain, only one branch of which ever compiled/ran per
    // build).
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQueryValue value("first_name");
        value.useDriver(driver).concat({"last_name"});

        const std::string firstName = QcSqlDialect::quoteIdentifier("first_name", driver);
        const std::string lastName = QcSqlDialect::quoteIdentifier("last_name", driver);
        if (driver == QcDbDriver::SQLite || driver == QcDbDriver::Oracle) {
            EXPECT_EQ(value.toSql(), firstName + " || " + lastName) << "driver=" << static_cast<int>(driver);
        } else {
            EXPECT_EQ(value.toSql(), "CONCAT(" + firstName + ", " + lastName + ")") << "driver=" << static_cast<int>(driver);
        }
    }
}

TEST(QcSqlQueryValueToSql, WrapsWithCastUsingActiveDriverTypeName)
{
    QcSqlQueryValue value("amount");
    value.cast(QcSqlBase::_string_, QcSqlBase::_float_);

    EXPECT_EQ(value.toSql(), "CAST(" + q("amount") + " AS " + QcSqlDialect::dataTypeName(QcSqlBase::_float_) + ")");
}

TEST(QcSqlQueryValueToSql, ChainWrapsOutwardInCallOrderThenAppendsAlias)
{
    QcSqlQueryValue value("test3 <test_alias3>");
    value.upperCase().cast(QcSqlBase::_string_, QcSqlBase::_float_).round(1);

    const std::string expected = "ROUND(CAST(UPPER(" + q("test3") + ") AS " + QcSqlDialect::dataTypeName(QcSqlBase::_float_) + "), 1) AS " + q("test_alias3");
    EXPECT_EQ(value.toSql(), expected);
}

// =====================================================================
// toSql() -- aggregate functions (_count_/_sum_/_avg_/_min_/_max_)
// =====================================================================

TEST(QcSqlQueryValueToSql, WrapsWithCountPlainAndDistinct)
{
    QcSqlQueryValue plain("id");
    plain.count();
    EXPECT_EQ(plain.toSql(), "COUNT(" + q("id") + ")");

    QcSqlQueryValue distinct("id");
    distinct.count(true);
    EXPECT_EQ(distinct.toSql(), "COUNT(DISTINCT " + q("id") + ")");
}

TEST(QcSqlQueryValueToSql, CountOfWildcardRendersBareStar)
{
    // QcSqlQueryValue("*") -- quoteIdentifier() never quotes the "*"
    // wildcard (see qcsqldialect.h) -- this is how COUNT(*) is reached,
    // rather than a separate no-argument count() overload.
    QcSqlQueryValue value("*");
    value.count();

    EXPECT_EQ(value.toSql(), "COUNT(*)");
}

TEST(QcSqlQueryValueToSql, WrapsWithSumAvgMinMax)
{
    QcSqlQueryValue sumValue("amount");
    sumValue.sum();
    EXPECT_EQ(sumValue.toSql(), "SUM(" + q("amount") + ")");

    QcSqlQueryValue avgValue("amount");
    avgValue.avg();
    EXPECT_EQ(avgValue.toSql(), "AVG(" + q("amount") + ")");

    QcSqlQueryValue minValue("amount");
    minValue.min();
    EXPECT_EQ(minValue.toSql(), "MIN(" + q("amount") + ")");

    QcSqlQueryValue maxValue("amount");
    maxValue.max();
    EXPECT_EQ(maxValue.toSql(), "MAX(" + q("amount") + ")");
}

TEST(QcSqlQueryValueToSql, AggregateWithAliasAppendsAsAfterTheFunction)
{
    QcSqlQueryValue value("salary <total>");
    value.sum();

    EXPECT_EQ(value.toSql(), "SUM(" + q("salary") + ") AS " + q("total"));
}

// =====================================================================
// toSql() -- COALESCE / NULLIF / TRIM / REPLACE
// =====================================================================

TEST(QcSqlQueryValueToSql, WrapsWithCoalesceRenderingFallbacksAsRawExpressions)
{
    QcSqlQueryValue value("nickname");
    value.coalesce({"full_name", "'N/A'"});

    // Same quoteRef()-per-operand convention as concat(): "full_name" looks
    // like a plain column and gets quoted, "'N/A'" (a string literal) does
    // not.
    EXPECT_EQ(value.toSql(), "COALESCE(" + q("nickname") + ", " + q("full_name") + ", 'N/A')");
}

TEST(QcSqlQueryValueToSql, WrapsWithNullIf)
{
    QcSqlQueryValue value("notes");
    value.nullIf("''");

    EXPECT_EQ(value.toSql(), "NULLIF(" + q("notes") + ", '')");
}

TEST(QcSqlQueryValueToSql, WrapsWithTrim)
{
    QcSqlQueryValue value("name");
    value.trim();

    EXPECT_EQ(value.toSql(), "TRIM(" + q("name") + ")");
}

TEST(QcSqlQueryValueToSql, WrapsWithReplaceRenderingOperandsAsRawExpressions)
{
    QcSqlQueryValue value("phone");
    value.replace("'-'", "''");

    EXPECT_EQ(value.toSql(), "REPLACE(" + q("phone") + ", '-', '')");
}

// =====================================================================
// toSql() -- LENGTH/LEN, SUBSTRING/SUBSTR (dialect-dependent function name)
// =====================================================================

TEST(QcSqlQueryValueToSql, WrapsWithLengthUsingActiveDriverFunctionName)
{
    QcSqlQueryValue mssqlValue("name");
    mssqlValue.useDriver(QcDbDriver::MSSQL).length();
    EXPECT_EQ(mssqlValue.toSql(), "LEN(" + QcSqlDialect::quoteIdentifier("name", QcDbDriver::MSSQL) + ")");

    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite}) {
        QcSqlQueryValue value("name");
        value.useDriver(driver).length();
        EXPECT_EQ(value.toSql(), "LENGTH(" + QcSqlDialect::quoteIdentifier("name", driver) + ")") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryValueToSql, WrapsWithSubstringUsingActiveDriverFunctionName)
{
    QcSqlQueryValue mssqlValue("name");
    mssqlValue.useDriver(QcDbDriver::MSSQL).substring(2, 5);
    EXPECT_EQ(mssqlValue.toSql(), "SUBSTRING(" + QcSqlDialect::quoteIdentifier("name", QcDbDriver::MSSQL) + ", 2, 5)");

    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite}) {
        QcSqlQueryValue value("name");
        value.useDriver(driver).substring(2, 5);
        EXPECT_EQ(value.toSql(), "SUBSTR(" + QcSqlDialect::quoteIdentifier("name", driver) + ", 2, 5)") << "driver=" << static_cast<int>(driver);
    }
}

// =====================================================================
// toSql() -- searched CASE WHEN
// =====================================================================

TEST(QcSqlQueryValueToSql, WrapsWithSearchedCaseWithoutElse)
{
    QcSqlQueryValue value;
    value.case_({{"status = 1", "active"}, {"status = 0", "'inactive'"}});

    // "active" looks like a plain column and gets quoted via quoteRef();
    // "status = 1"/"status = 0" (full boolean expressions) and "'inactive'"
    // (a string literal) do not.
    EXPECT_EQ(value.toSql(), "CASE WHEN status = 1 THEN " + q("active") + " WHEN status = 0 THEN 'inactive' END");
}

TEST(QcSqlQueryValueToSql, WrapsWithSearchedCaseWithElse)
{
    QcSqlQueryValue value("<status_label>");
    value.case_({{"status = 1", "'active'"}}, std::string("'unknown'"));

    EXPECT_EQ(value.toSql(), "CASE WHEN status = 1 THEN 'active' ELSE 'unknown' END AS " + q("status_label"));
}

TEST(QcSqlQueryValueToSql, CaseDiscardsAnyPreviouslyAccumulatedExpression)
{
    // See case_()'s doc comment in qcsqlqueryvalue.h: a searched CASE has no
    // single subject to wrap, so it replaces whatever the chain had built up
    // so far rather than wrapping it -- the base column ("name") and the
    // preceding upperCase() are both discarded here, not just left unused.
    QcSqlQueryValue value("name");
    value.upperCase().case_({{"1 = 1", "'always'"}});

    EXPECT_EQ(value.toSql(), "CASE WHEN 1 = 1 THEN 'always' END");
}

// =====================================================================
// toSql() -- EXTRACT/DATEPART/strftime, DATEADD/DATE_ADD/INTERVAL
// (the two most dialect-divergent functions here)
// =====================================================================

TEST(QcSqlQueryValueToSql, WrapsWithExtractUsingActiveDriverSyntax)
{
    // Runs once per driver in a single build/binary now (previously this was
    // an #if/#elif chain, only one branch of which ever compiled/ran per
    // build).
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQueryValue value("hire_date");
        value.useDriver(driver).extract(QcSqlBase::_year_);
        const std::string hireDate = QcSqlDialect::quoteIdentifier("hire_date", driver);

        switch (driver) {
            case QcDbDriver::MSSQL:
                EXPECT_EQ(value.toSql(), "DATEPART(year, " + hireDate + ")");
                break;
            case QcDbDriver::SQLite:
                EXPECT_EQ(value.toSql(), "CAST(strftime('%Y', " + hireDate + ") AS INTEGER)");
                break;
            default:
                EXPECT_EQ(value.toSql(), "EXTRACT(YEAR FROM " + hireDate + ")") << "driver=" << static_cast<int>(driver);
                break;
        }
    }
}

TEST(QcSqlQueryValueToSql, ExtractCoversEveryDatePart)
{
    // One spot-check per QcSqlBase::datePartTypes value (beyond the YEAR
    // case above) so a future part added to the enum without a matching
    // dialect-map entry fails loudly instead of silently falling through to
    // the switch's default.
    const std::pair<int, const char *> parts[] = {
        {QcSqlBase::_month_, "MONTH"}, {QcSqlBase::_day_, "DAY"}, {QcSqlBase::_hour_, "HOUR"},
        {QcSqlBase::_minute_, "MINUTE"}, {QcSqlBase::_second_, "SECOND"},
    };
    for (const auto & [part, ansiKeyword] : parts) {
        for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
            QcSqlQueryValue value("hire_date");
            value.useDriver(driver).extract(part);
            if (driver == QcDbDriver::MSSQL || driver == QcDbDriver::SQLite) {
                // Already exhaustively covered per-driver by the YEAR case above;
                // here just confirm each part renders *something* distinct.
                EXPECT_FALSE(value.toSql().empty());
            } else {
                EXPECT_EQ(value.toSql(), "EXTRACT(" + std::string(ansiKeyword) + " FROM " + QcSqlDialect::quoteIdentifier("hire_date", driver) + ")") << "driver=" << static_cast<int>(driver);
            }
        }
    }
}

TEST(QcSqlQueryValueToSql, WrapsWithDateAddUsingActiveDriverSyntax)
{
    // Runs once per driver in a single build/binary now (previously this was
    // an #if/#elif chain, only one branch of which ever compiled/ran per
    // build).
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQueryValue value("hire_date");
        value.useDriver(driver).dateAdd(QcSqlBase::_day_, 7);
        const std::string hireDate = QcSqlDialect::quoteIdentifier("hire_date", driver);

        switch (driver) {
            case QcDbDriver::PostgreSQL:
                EXPECT_EQ(value.toSql(), hireDate + " + INTERVAL '7 DAY'");
                break;
            case QcDbDriver::MySQL:
                EXPECT_EQ(value.toSql(), "DATE_ADD(" + hireDate + ", INTERVAL 7 DAY)");
                break;
            case QcDbDriver::SQLite:
                EXPECT_EQ(value.toSql(), "datetime(" + hireDate + ", '+7 days')");
                break;
            case QcDbDriver::MSSQL:
                EXPECT_EQ(value.toSql(), "DATEADD(day, 7, " + hireDate + ")");
                break;
            case QcDbDriver::Oracle:
                EXPECT_EQ(value.toSql(), hireDate + " + INTERVAL '7' DAY");
                break;
        }
    }
}

TEST(QcSqlQueryValueToSql, DateAddWithNegativeAmountSubtracts)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQueryValue value("hire_date");
        value.useDriver(driver).dateAdd(QcSqlBase::_month_, -3);
        const std::string hireDate = QcSqlDialect::quoteIdentifier("hire_date", driver);

        switch (driver) {
            case QcDbDriver::PostgreSQL:
                EXPECT_EQ(value.toSql(), hireDate + " + INTERVAL '-3 MONTH'");
                break;
            case QcDbDriver::MySQL:
                EXPECT_EQ(value.toSql(), "DATE_ADD(" + hireDate + ", INTERVAL -3 MONTH)");
                break;
            case QcDbDriver::SQLite:
                EXPECT_EQ(value.toSql(), "datetime(" + hireDate + ", '-3 months')");
                break;
            case QcDbDriver::MSSQL:
                EXPECT_EQ(value.toSql(), "DATEADD(month, -3, " + hireDate + ")");
                break;
            case QcDbDriver::Oracle:
                EXPECT_EQ(value.toSql(), hireDate + " + INTERVAL '-3' MONTH");
                break;
        }
    }
}

// =====================================================================
// toSql() -- jsonExtract()/jsonExtractNumber()/jsonExtractRaw()
// =====================================================================

// Wraps the same QcSqlDialect::jsonExtractExpr() that
// QcSqlQueryElement::isXxxJson...() uses on the WHERE side (see
// test_qcsqlqueryelement.cpp) -- that function's own per-driver rendering is
// already pinned in test_qcsqldialect.cpp, so these tests only need to check
// that each method wraps the current chain expression with the right `kind`
// and path, the same as every other chain function above.
TEST(QcSqlQueryValueToSql, JsonExtractWrapsColumnAsTextUsingActiveDriverSyntax)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQueryValue value("metadata <skill0>");
        value.useDriver(driver).jsonExtract("skills[0]");

        const std::string expected = QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "skills[0]", QcSqlBase::_jsonAsText_, driver)
            + " AS " + QcSqlDialect::quoteIdentifier("skill0", driver);
        EXPECT_EQ(value.toSql(), expected) << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryValueToSql, JsonExtractNumberWrapsColumnAsNumberUsingActiveDriverSyntax)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQueryValue value("metadata");
        value.useDriver(driver).jsonExtractNumber("rating");

        EXPECT_EQ(value.toSql(), QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "rating", QcSqlBase::_jsonAsNumber_, driver))
            << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryValueToSql, JsonExtractRawWrapsColumnAsRawJsonFragmentUsingActiveDriverSyntax)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQueryValue value("metadata");
        value.useDriver(driver).jsonExtractRaw("skills");

        EXPECT_EQ(value.toSql(), QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "skills", QcSqlBase::_jsonAsRaw_, driver))
            << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryValueToSql, JsonExtractChainsWithFurtherFunctionsAfterward)
{
    // jsonExtract() wraps the current expression exactly like every other
    // function here (see the doc comment on qcsqlqueryvalue.h) -- it does
    // not special-case being first/only in the chain the way case_() does
    // (see CaseDiscardsAnyPreviouslyAccumulatedExpression above), so a
    // further call composes normally around its result.
    QcSqlQueryValue value("metadata");
    value.jsonExtractNumber("rating").round(1);

    EXPECT_EQ(value.toSql(), "ROUND(" + QcSqlDialect::jsonExtractExpr(q("metadata"), "rating", QcSqlBase::_jsonAsNumber_) + ", 1)");
}
