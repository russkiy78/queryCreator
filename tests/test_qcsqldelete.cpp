#include <gtest/gtest.h>

#include "query/qcsqldialect.h"
#include "query/qcsqldelete.h"

// Shorthand for QcSqlDialect::quoteIdentifier() -- see the identical helper
// (and its rationale) in test_qcsqlquery.cpp.
namespace {
std::string q(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name);
}
} // namespace

// See test_qcsqlupdate.cpp for why this is a friend fixture and why
// per-comparator SQL shape isn't re-verified here (that lives in
// test_qcsqlqueryelement.cpp).
class QcSqlDeleteWhiteBoxTest : public ::testing::Test
{
protected:
    static const std::string & table(const QcSqlDelete & q) { return q.m_table; }
    static std::size_t whereCount(const QcSqlDelete & q) { return q.m_whereElements.size(); }
    static int whereConnector(const QcSqlDelete & q, std::size_t i) { return q.m_whereElements[i].first; }
    static const QcSqlQueryElement & whereElement(const QcSqlDelete & q, std::size_t i) { return q.m_whereElements[i].second; }
    static int whereParenDepth(const QcSqlDelete & q) { return q.m_whereParenDepth; }
    static const QcSqlDelete::QcStringList & returningColumns(const QcSqlDelete & q) { return q.m_returning; }
};

TEST_F(QcSqlDeleteWhiteBoxTest, FromSetsTargetTableAndReturnsChainableReference)
{
    QcSqlDelete del;

    QcSqlDelete & ref = del.from("users");

    EXPECT_EQ(&ref, &del);
    EXPECT_EQ(table(del), "users");
}

TEST_F(QcSqlDeleteWhiteBoxTest, WhereReturnsReferenceToTheJustInsertedElement)
{
    QcSqlDelete del;

    QcSqlQueryElement & element = del.where("a");

    ASSERT_EQ(whereCount(del), 1u);
    EXPECT_EQ(&whereElement(del, 0), &element);
}

TEST_F(QcSqlDeleteWhiteBoxTest, AndOrOrAppendWithCorrectConnector)
{
    QcSqlDelete del;

    del.where("a").isEqualTo(QcSqlDelete::QcVariant(1LL));
    del.and_("b").isEqualTo(QcSqlDelete::QcVariant(2LL));
    del.or_("c").isEqualTo(QcSqlDelete::QcVariant(3LL));

    ASSERT_EQ(whereCount(del), 3u);
    EXPECT_NE(whereConnector(del, 1), whereConnector(del, 2));
}

TEST_F(QcSqlDeleteWhiteBoxTest, OpenParenthesisIncrementsDepthAndCloseDecrementsIt)
{
    QcSqlDelete del;

    EXPECT_TRUE(del.openParenthesis());
    EXPECT_EQ(whereParenDepth(del), 1);
    EXPECT_TRUE(del.closeParenthesis());
    EXPECT_EQ(whereParenDepth(del), 0);
}

TEST_F(QcSqlDeleteWhiteBoxTest, CloseParenthesisWithoutMatchingOpenReturnsFalse)
{
    QcSqlDelete del;

    EXPECT_FALSE(del.closeParenthesis());
    EXPECT_EQ(whereCount(del), 0u);
}

TEST_F(QcSqlDeleteWhiteBoxTest, ReturningSetsColumnListAndReturnsChainableReference)
{
    QcSqlDelete del;

    QcSqlDelete & ref = del.returning({"id"});

    EXPECT_EQ(&ref, &del);
    ASSERT_EQ(returningColumns(del).size(), 1u);
    EXPECT_EQ(returningColumns(del)[0], "id");
}

TEST(QcSqlDeleteToSql, RendersBareDeleteWhenNoWhereAdded)
{
    QcSqlDelete del;
    del.from("users");

    EXPECT_EQ(del.toSql().sql, "DELETE FROM " + q("users"));
}

TEST(QcSqlDeleteToSql, RendersWhereClauseWithBoundValue)
{
    QcSqlDelete del;
    del.from("users");
    del.where("id").isEqualTo(QcSqlDelete::QcVariant(1LL));

    const std::string expected = "DELETE FROM " + q("users") + " WHERE " + q("id") + " = " + QcSqlDialect::placeholder(1);
    QcSqlStatement statement = del.toSql();
    EXPECT_EQ(statement.sql, expected);
    ASSERT_EQ(statement.params.size(), 1u);
    EXPECT_EQ(std::get<long long>(statement.params[0]), 1LL);
}

TEST(QcSqlDeleteToSql, WhereChainGluesAndOrCorrectly)
{
    QcSqlDelete del;
    del.from("users");
    del.where("a").isEqualTo(QcSqlDelete::QcVariant(1LL));
    del.and_("b").isEqualTo(QcSqlDelete::QcVariant(2LL));
    del.or_("c").isEqualTo(QcSqlDelete::QcVariant(3LL));

    const std::string expected = "DELETE FROM " + q("users") + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(1)
        + " AND " + q("b") + " = " + QcSqlDialect::placeholder(2) + " OR " + q("c") + " = " + QcSqlDialect::placeholder(3);
    EXPECT_EQ(del.toSql().sql, expected);
}

TEST(QcSqlDeleteToSql, WhereChainWithParenthesesOmitsConnectorRightAfterOpenParen)
{
    QcSqlDelete del;
    del.from("users");
    del.where("a").isEqualTo(QcSqlDelete::QcVariant(1LL));
    del.and_OpenParenthesis("b").isEqualTo(QcSqlDelete::QcVariant(2LL));
    del.or_("c").isEqualTo(QcSqlDelete::QcVariant(3LL));
    del.closeParenthesis();

    const std::string expected = "DELETE FROM " + q("users") + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(1)
        + " AND (" + q("b") + " = " + QcSqlDialect::placeholder(2) + " OR " + q("c") + " = " + QcSqlDialect::placeholder(3) + ")";
    EXPECT_EQ(del.toSql().sql, expected);
}

TEST(QcSqlDeleteToSql, WhereOpenParenthesisAsFirstConditionOpensLeadingGroup)
{
    // and_OpenParenthesis() is exercised above -- this covers the other two
    // *_OpenParenthesis() sugar methods, untested until now (see
    // test_qcsqlquery.cpp's OpenParenthesisSugarMethodsPushMarkerThenElement
    // for the equivalent QcSqlQuery coverage of all three).
    QcSqlDelete del;
    del.from("users");
    del.where_OpenParenthesis("a").isEqualTo(QcSqlDelete::QcVariant(1LL));
    del.or_("b").isEqualTo(QcSqlDelete::QcVariant(2LL));
    del.closeParenthesis();

    const std::string expected = "DELETE FROM " + q("users") + " WHERE (" + q("a") + " = " + QcSqlDialect::placeholder(1)
        + " OR " + q("b") + " = " + QcSqlDialect::placeholder(2) + ")";
    EXPECT_EQ(del.toSql().sql, expected);
}

TEST(QcSqlDeleteToSql, OrOpenParenthesisStartsNewGroupWithOrConnector)
{
    // Distinguishes or_OpenParenthesis() from and_OpenParenthesis() (already
    // covered above) by the " OR (" it must render instead of " AND (".
    QcSqlDelete del;
    del.from("users");
    del.where("a").isEqualTo(QcSqlDelete::QcVariant(1LL));
    del.or_OpenParenthesis("b").isEqualTo(QcSqlDelete::QcVariant(2LL));
    del.and_("c").isEqualTo(QcSqlDelete::QcVariant(3LL));
    del.closeParenthesis();

    const std::string expected = "DELETE FROM " + q("users") + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(1)
        + " OR (" + q("b") + " = " + QcSqlDialect::placeholder(2) + " AND " + q("c") + " = " + QcSqlDialect::placeholder(3) + ")";
    EXPECT_EQ(del.toSql().sql, expected);
}

TEST(QcSqlDeleteToSql, UseDriverSetsDialectForNoArgToSql)
{
    // Every other toSql() test in this file passes an explicit driver (see
    // docs/testing.md) -- this is the one test proving the "set once, up
    // front" useDriver()/no-arg-toSql() pairing itself actually works.
    QcSqlDelete del;
    del.useDriver(QcDbDriver::MySQL);
    del.from("users");

    const std::string expected = "DELETE FROM " + QcSqlDialect::quoteIdentifier("users", QcDbDriver::MySQL);
    EXPECT_EQ(del.toSql().sql, expected);
}

TEST(QcSqlDeleteToSql, ReturningMatchesActiveDriverShapeAndPosition)
{
    // Runs once per driver in a single build/binary now (previously this was
    // an #if/#elif chain, only one branch of which ever compiled/ran per
    // build) -- see the identical restructuring (and its rationale) in
    // test_qcsqlinsert.cpp's ReturningMatchesActiveDriverShapeAndPosition.
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlDelete del;
        del.from("users");
        del.where("id").isEqualTo(QcSqlDelete::QcVariant(1LL));
        del.returning({"id", "name"});

        const QcSqlStatement statement = del.toSql(driver);
        const std::string & sql = statement.sql;
        const std::string id = QcSqlDialect::quoteIdentifier("id", driver);
        const std::string name = QcSqlDialect::quoteIdentifier("name", driver);
        const std::string table = QcSqlDialect::quoteIdentifier("users", driver);
        // Unconditional across every driver -- see the identical assertion (and
        // its rationale) in test_qcsqlinsert.cpp's ReturningMatchesActiveDriverShapeAndPosition.
        EXPECT_EQ(statement.returningColumnNames, (QcSqlDelete::QcStringList{"id", "name"})) << "driver=" << static_cast<int>(driver);

        switch (driver) {
            case QcDbDriver::PostgreSQL:
            case QcDbDriver::SQLite:
                EXPECT_EQ(sql, "DELETE FROM " + table + " WHERE " + id + " = " + QcSqlDialect::placeholder(1, driver) + " RETURNING " + id + ", " + name);
                EXPECT_EQ(statement.returningColumnCount, 0u);
                break;
            case QcDbDriver::MSSQL:
                // OUTPUT sits between the table name and WHERE, and uses "deleted."
                // (not "inserted.") since a DELETE only ever has pre-deletion values.
                EXPECT_EQ(sql, "DELETE FROM " + table + " OUTPUT deleted." + id + ", deleted." + name + " WHERE " + id + " = " + QcSqlDialect::placeholder(1, driver));
                EXPECT_EQ(statement.returningColumnCount, 0u);
                break;
            case QcDbDriver::Oracle:
                // RETURNING ... INTO's OUT-bind placeholders continue from the 1 IN
                // placeholder already used (the WHERE value) -- :2, :3.
                EXPECT_EQ(sql, "DELETE FROM " + table + " WHERE " + id + " = " + QcSqlDialect::placeholder(1, driver)
                    + " RETURNING " + id + ", " + name + " INTO " + QcSqlDialect::placeholder(2, driver) + ", " + QcSqlDialect::placeholder(3, driver));
                EXPECT_EQ(statement.returningColumnCount, 2u);
                break;
            case QcDbDriver::MySQL:
                EXPECT_EQ(sql, "DELETE FROM " + table + " WHERE " + id + " = " + QcSqlDialect::placeholder(1, driver));
                EXPECT_EQ(statement.returningColumnCount, 0u);
                break;
        }
    }
}

TEST(QcSqlDeleteToSql, NoArgOverloadMatchesThreadingOverload)
{
    QcSqlDelete del;
    del.from("users");
    del.where("id").isEqualTo(QcSqlDelete::QcVariant(5LL));

    QcSqlDelete::QcVariantList params;
    const std::string sqlFromThreadingOverload = del.toSql(params);
    QcSqlStatement statement = del.toSql();

    EXPECT_EQ(statement.sql, sqlFromThreadingOverload);
    ASSERT_EQ(statement.params.size(), params.size());
}

// =====================================================================
// validate() / toSql() structural-completeness checks
// =====================================================================

TEST(QcSqlDeleteValidate, NoTableIsReported)
{
    QcSqlDelete del;

    const QcSqlDelete::QcStringList problems = del.validate();
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("table"), std::string::npos);
}

TEST(QcSqlDeleteValidate, UnclosedParenthesisIsReported)
{
    QcSqlDelete del;
    del.from("users");
    del.openParenthesis();

    const QcSqlDelete::QcStringList problems = del.validate();
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("parenthesis"), std::string::npos);
}

TEST(QcSqlDeleteValidate, IncompleteWhereConditionIsReported)
{
    QcSqlDelete del;
    del.from("users");
    del.where("id");

    const QcSqlDelete::QcStringList problems = del.validate();
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("WHERE"), std::string::npos);
}

TEST(QcSqlDeleteValidate, NoWhereClauseIsNotAProblem)
{
    // Deleting every row on purpose (no WHERE at all) is legitimate SQL,
    // not a gap -- same reasoning as QcSqlUpdateValidate.NoWhereClauseIsNotAProblem.
    QcSqlDelete del;
    del.from("users");

    EXPECT_TRUE(del.validate().empty());
}

TEST(QcSqlDeleteToSql, ThrowsQcQueryBuildErrorWhenNoTable)
{
    QcSqlDelete del;

    EXPECT_THROW(del.toSql(), QcQueryBuildError);
}

TEST(QcSqlDeleteToSql, ThrowsQcQueryBuildErrorForIncompleteWhereCondition)
{
    QcSqlDelete del;
    del.from("users");
    del.where("id");

    EXPECT_THROW(del.toSql(), QcQueryBuildError);
}
