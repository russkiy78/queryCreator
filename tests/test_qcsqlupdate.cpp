#include <gtest/gtest.h>

#include "query/qcsqldialect.h"
#include "query/qcsqlupdate.h"

// Shorthand for QcSqlDialect::quoteIdentifier() -- see the identical helper
// (and its rationale) in test_qcsqlquery.cpp.
namespace {
std::string q(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name);
}
} // namespace

// See test_qcsqlquery.cpp for why this is a friend fixture rather than
// public getters. Individual QcSqlQueryElement comparator rendering is
// already exhaustively covered in test_qcsqlqueryelement.cpp -- these tests
// only need to prove QcSqlUpdate wires where()/and_()/or_() through to it
// correctly (state here, final SQL shape in the QcSqlUpdateToSql tests
// below), not re-verify every comparator's own SQL shape.
class QcSqlUpdateWhiteBoxTest : public ::testing::Test
{
protected:
    static const std::string & table(const QcSqlUpdate & q) { return q.m_table; }
    static std::size_t columnCount(const QcSqlUpdate & q) { return q.m_columns.size(); }
    static const std::string & columnName(const QcSqlUpdate & q, std::size_t i) { return q.m_columns[i].first; }
    static const QcSqlUpdate::QcVariant & columnValue(const QcSqlUpdate & q, std::size_t i) { return q.m_columns[i].second; }

    static std::size_t whereCount(const QcSqlUpdate & q) { return q.m_whereElements.size(); }
    static int whereConnector(const QcSqlUpdate & q, std::size_t i) { return q.m_whereElements[i].first; }
    static const QcSqlQueryElement & whereElement(const QcSqlUpdate & q, std::size_t i) { return q.m_whereElements[i].second; }
    static int whereParenDepth(const QcSqlUpdate & q) { return q.m_whereParenDepth; }
    static const QcSqlUpdate::QcStringList & returningColumns(const QcSqlUpdate & q) { return q.m_returning; }
};

TEST_F(QcSqlUpdateWhiteBoxTest, TableSetsTargetTableAndReturnsChainableReference)
{
    QcSqlUpdate update;

    QcSqlUpdate & ref = update.table("users");

    EXPECT_EQ(&ref, &update);
    EXPECT_EQ(table(update), "users");
}

TEST_F(QcSqlUpdateWhiteBoxTest, SetAppendsColumnValuePairsAndOverwritesSameColumnInPlace)
{
    QcSqlUpdate update;
    update.set("name", QcSqlUpdate::QcVariant(std::string("A")));
    update.set("amount", QcSqlUpdate::QcVariant(1.5));

    update.set("name", QcSqlUpdate::QcVariant(std::string("B")));

    ASSERT_EQ(columnCount(update), 2u);
    EXPECT_EQ(columnName(update, 0), "name");
    EXPECT_EQ(std::get<std::string>(columnValue(update, 0)), "B");
    EXPECT_EQ(columnName(update, 1), "amount");
}

TEST_F(QcSqlUpdateWhiteBoxTest, WhereReturnsReferenceToTheJustInsertedElement)
{
    QcSqlUpdate update;

    QcSqlQueryElement & element = update.where("a");

    ASSERT_EQ(whereCount(update), 1u);
    EXPECT_EQ(&whereElement(update, 0), &element);
}

TEST_F(QcSqlUpdateWhiteBoxTest, AndOrOrAppendWithCorrectConnector)
{
    QcSqlUpdate update;

    update.where("a").isEqualTo(QcSqlUpdate::QcVariant(1LL));
    update.and_("b").isEqualTo(QcSqlUpdate::QcVariant(2LL));
    update.or_("c").isEqualTo(QcSqlUpdate::QcVariant(3LL));

    ASSERT_EQ(whereCount(update), 3u);
    EXPECT_NE(whereConnector(update, 1), whereConnector(update, 2));
}

TEST_F(QcSqlUpdateWhiteBoxTest, OpenParenthesisIncrementsDepthAndCloseDecrementsIt)
{
    QcSqlUpdate update;

    EXPECT_TRUE(update.openParenthesis());
    EXPECT_EQ(whereParenDepth(update), 1);
    EXPECT_TRUE(update.closeParenthesis());
    EXPECT_EQ(whereParenDepth(update), 0);
}

TEST_F(QcSqlUpdateWhiteBoxTest, CloseParenthesisWithoutMatchingOpenReturnsFalse)
{
    QcSqlUpdate update;

    EXPECT_FALSE(update.closeParenthesis());
    EXPECT_EQ(whereCount(update), 0u);
}

TEST_F(QcSqlUpdateWhiteBoxTest, ReturningSetsColumnListAndReturnsChainableReference)
{
    QcSqlUpdate update;

    QcSqlUpdate & ref = update.returning({"id"});

    EXPECT_EQ(&ref, &update);
    ASSERT_EQ(returningColumns(update).size(), 1u);
    EXPECT_EQ(returningColumns(update)[0], "id");
}

TEST(QcSqlUpdateToSql, RendersSetListWithoutWhereWhenNoConditionsAdded)
{
    QcSqlUpdate update;
    update.table("users").set("name", QcSqlUpdate::QcVariant(std::string("A")));

    EXPECT_EQ(update.toSql().sql, "UPDATE " + q("users") + " SET " + q("name") + " = " + QcSqlDialect::placeholder(1));
}

TEST(QcSqlUpdateToSql, RendersMultipleSetColumnsThenWhereClause)
{
    QcSqlUpdate update;
    update.table("users").set("name", QcSqlUpdate::QcVariant(std::string("B-updated")));
    update.where("id").isEqualTo(QcSqlUpdate::QcVariant(2LL));

    const std::string expected = "UPDATE " + q("users") + " SET " + q("name") + " = " + QcSqlDialect::placeholder(1)
        + " WHERE " + q("id") + " = " + QcSqlDialect::placeholder(2);
    QcSqlStatement statement = update.toSql();
    EXPECT_EQ(statement.sql, expected);
    ASSERT_EQ(statement.params.size(), 2u);
    EXPECT_EQ(std::get<std::string>(statement.params[0]), "B-updated");
    EXPECT_EQ(std::get<long long>(statement.params[1]), 2LL);
}

TEST(QcSqlUpdateToSql, WhereChainGluesAndOrCorrectly)
{
    QcSqlUpdate update;
    update.table("users").set("x", QcSqlUpdate::QcVariant(1LL));
    update.where("a").isEqualTo(QcSqlUpdate::QcVariant(1LL));
    update.and_("b").isEqualTo(QcSqlUpdate::QcVariant(2LL));
    update.or_("c").isEqualTo(QcSqlUpdate::QcVariant(3LL));

    const std::string expected = "UPDATE " + q("users") + " SET " + q("x") + " = " + QcSqlDialect::placeholder(1)
        + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(2)
        + " AND " + q("b") + " = " + QcSqlDialect::placeholder(3)
        + " OR " + q("c") + " = " + QcSqlDialect::placeholder(4);
    EXPECT_EQ(update.toSql().sql, expected);
}

TEST(QcSqlUpdateToSql, WhereChainWithParenthesesOmitsConnectorRightAfterOpenParen)
{
    QcSqlUpdate update;
    update.table("users").set("x", QcSqlUpdate::QcVariant(1LL));
    update.where("a").isEqualTo(QcSqlUpdate::QcVariant(1LL));
    update.and_OpenParenthesis("b").isEqualTo(QcSqlUpdate::QcVariant(2LL));
    update.or_("c").isEqualTo(QcSqlUpdate::QcVariant(3LL));
    update.closeParenthesis();

    const std::string expected = "UPDATE " + q("users") + " SET " + q("x") + " = " + QcSqlDialect::placeholder(1)
        + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(2)
        + " AND (" + q("b") + " = " + QcSqlDialect::placeholder(3) + " OR " + q("c") + " = " + QcSqlDialect::placeholder(4) + ")";
    EXPECT_EQ(update.toSql().sql, expected);
}

TEST(QcSqlUpdateToSql, WhereOpenParenthesisAsFirstConditionOpensLeadingGroup)
{
    // and_OpenParenthesis() is exercised above -- this covers the other two
    // *_OpenParenthesis() sugar methods, untested until now (see
    // test_qcsqlquery.cpp's OpenParenthesisSugarMethodsPushMarkerThenElement
    // for the equivalent QcSqlQuery coverage of all three).
    QcSqlUpdate update;
    update.table("users").set("x", QcSqlUpdate::QcVariant(1LL));
    update.where_OpenParenthesis("a").isEqualTo(QcSqlUpdate::QcVariant(1LL));
    update.or_("b").isEqualTo(QcSqlUpdate::QcVariant(2LL));
    update.closeParenthesis();

    const std::string expected = "UPDATE " + q("users") + " SET " + q("x") + " = " + QcSqlDialect::placeholder(1)
        + " WHERE (" + q("a") + " = " + QcSqlDialect::placeholder(2) + " OR " + q("b") + " = " + QcSqlDialect::placeholder(3) + ")";
    EXPECT_EQ(update.toSql().sql, expected);
}

TEST(QcSqlUpdateToSql, OrOpenParenthesisStartsNewGroupWithOrConnector)
{
    // Distinguishes or_OpenParenthesis() from and_OpenParenthesis() (already
    // covered above) by the " OR (" it must render instead of " AND (".
    QcSqlUpdate update;
    update.table("users").set("x", QcSqlUpdate::QcVariant(1LL));
    update.where("a").isEqualTo(QcSqlUpdate::QcVariant(1LL));
    update.or_OpenParenthesis("b").isEqualTo(QcSqlUpdate::QcVariant(2LL));
    update.and_("c").isEqualTo(QcSqlUpdate::QcVariant(3LL));
    update.closeParenthesis();

    const std::string expected = "UPDATE " + q("users") + " SET " + q("x") + " = " + QcSqlDialect::placeholder(1)
        + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(2)
        + " OR (" + q("b") + " = " + QcSqlDialect::placeholder(3) + " AND " + q("c") + " = " + QcSqlDialect::placeholder(4) + ")";
    EXPECT_EQ(update.toSql().sql, expected);
}

TEST(QcSqlUpdateToSql, UseDriverSetsDialectForNoArgToSql)
{
    // Every other toSql() test in this file passes an explicit driver (see
    // docs/testing.md) -- this is the one test proving the "set once, up
    // front" useDriver()/no-arg-toSql() pairing itself actually works.
    QcSqlUpdate update;
    update.useDriver(QcDbDriver::MySQL);
    update.table("users").set("x", QcSqlUpdate::QcVariant(1LL));

    const std::string expected = "UPDATE " + QcSqlDialect::quoteIdentifier("users", QcDbDriver::MySQL) + " SET "
        + QcSqlDialect::quoteIdentifier("x", QcDbDriver::MySQL) + " = " + QcSqlDialect::placeholder(1, QcDbDriver::MySQL);
    EXPECT_EQ(update.toSql().sql, expected);
}

TEST(QcSqlUpdateToSql, ComparatorVocabularyIsAvailableThroughWhere)
{
    // Spot-check a comparator with a non-trivial rendering branch (isIn) to
    // prove where() really hands back a live QcSqlQueryElement& -- exhaustive
    // per-comparator coverage lives in test_qcsqlqueryelement.cpp.
    QcSqlUpdate update;
    update.table("t").set("x", QcSqlUpdate::QcVariant(1LL));
    update.where("id").isIn({QcSqlUpdate::QcVariant(1LL), QcSqlUpdate::QcVariant(2LL)});

    const std::string expected = "UPDATE " + q("t") + " SET " + q("x") + " = " + QcSqlDialect::placeholder(1)
        + " WHERE " + q("id") + " IN (" + QcSqlDialect::placeholder(2) + ", " + QcSqlDialect::placeholder(3) + ")";
    EXPECT_EQ(update.toSql().sql, expected);
}

TEST(QcSqlUpdateToSql, ReturningMatchesActiveDriverShapeAndPosition)
{
    // Runs once per driver in a single build/binary now (previously this was
    // an #if/#elif chain, only one branch of which ever compiled/ran per
    // build) -- see the identical restructuring (and its rationale) in
    // test_qcsqlinsert.cpp's ReturningMatchesActiveDriverShapeAndPosition.
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlUpdate update;
        update.table("users").set("name", QcSqlUpdate::QcVariant(std::string("B")));
        update.where("id").isEqualTo(QcSqlUpdate::QcVariant(2LL));
        update.returning({"id", "name"});

        const QcSqlStatement statement = update.toSql(driver);
        const std::string & sql = statement.sql;
        const std::string name = QcSqlDialect::quoteIdentifier("name", driver);
        const std::string id = QcSqlDialect::quoteIdentifier("id", driver);
        const std::string table = QcSqlDialect::quoteIdentifier("users", driver);
        // Unconditional across every driver -- see the identical assertion (and
        // its rationale) in test_qcsqlinsert.cpp's ReturningMatchesActiveDriverShapeAndPosition.
        EXPECT_EQ(statement.returningColumnNames, (QcSqlUpdate::QcStringList{"id", "name"})) << "driver=" << static_cast<int>(driver);

        switch (driver) {
            case QcDbDriver::PostgreSQL:
            case QcDbDriver::SQLite:
                EXPECT_EQ(sql, "UPDATE " + table + " SET " + name + " = " + QcSqlDialect::placeholder(1, driver)
                    + " WHERE " + id + " = " + QcSqlDialect::placeholder(2, driver) + " RETURNING " + id + ", " + name);
                EXPECT_EQ(statement.returningColumnCount, 0u);
                break;
            case QcDbDriver::MSSQL:
                // OUTPUT sits between the SET list and WHERE, not at the end.
                EXPECT_EQ(sql, "UPDATE " + table + " SET " + name + " = " + QcSqlDialect::placeholder(1, driver)
                    + " OUTPUT inserted." + id + ", inserted." + name + " WHERE " + id + " = " + QcSqlDialect::placeholder(2, driver));
                EXPECT_EQ(statement.returningColumnCount, 0u);
                break;
            case QcDbDriver::Oracle:
                // RETURNING ... INTO's OUT-bind placeholders continue from the 2 IN
                // placeholders already used (the SET value, then the WHERE value) -- :3, :4.
                EXPECT_EQ(sql, "UPDATE " + table + " SET " + name + " = " + QcSqlDialect::placeholder(1, driver)
                    + " WHERE " + id + " = " + QcSqlDialect::placeholder(2, driver)
                    + " RETURNING " + id + ", " + name + " INTO " + QcSqlDialect::placeholder(3, driver) + ", " + QcSqlDialect::placeholder(4, driver));
                EXPECT_EQ(statement.returningColumnCount, 2u);
                break;
            case QcDbDriver::MySQL:
                EXPECT_EQ(sql, "UPDATE " + table + " SET " + name + " = " + QcSqlDialect::placeholder(1, driver) + " WHERE " + id + " = " + QcSqlDialect::placeholder(2, driver));
                EXPECT_EQ(statement.returningColumnCount, 0u);
                break;
        }
    }
}

TEST(QcSqlUpdateToSql, NoArgOverloadMatchesThreadingOverload)
{
    QcSqlUpdate update;
    update.table("users").set("x", QcSqlUpdate::QcVariant(1LL));
    update.where("id").isEqualTo(QcSqlUpdate::QcVariant(5LL));

    QcSqlUpdate::QcVariantList params;
    const std::string sqlFromThreadingOverload = update.toSql(params);
    QcSqlStatement statement = update.toSql();

    EXPECT_EQ(statement.sql, sqlFromThreadingOverload);
    ASSERT_EQ(statement.params.size(), params.size());
}

// =====================================================================
// validate() / toSql() structural-completeness checks
// =====================================================================

TEST(QcSqlUpdateValidate, NoTableIsReported)
{
    QcSqlUpdate update;
    update.set("name", QcSqlUpdate::QcVariant(std::string("A")));

    const QcSqlUpdate::QcStringList problems = update.validate();
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("table"), std::string::npos);
}

TEST(QcSqlUpdateValidate, UnclosedParenthesisIsReported)
{
    QcSqlUpdate update;
    update.table("users").set("name", QcSqlUpdate::QcVariant(std::string("A")));
    update.openParenthesis();

    const QcSqlUpdate::QcStringList problems = update.validate();
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("parenthesis"), std::string::npos);
}

TEST(QcSqlUpdateValidate, IncompleteWhereConditionIsReported)
{
    QcSqlUpdate update;
    update.table("users").set("name", QcSqlUpdate::QcVariant(std::string("A")));
    update.where("id");

    const QcSqlUpdate::QcStringList problems = update.validate();
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("WHERE"), std::string::npos);
}

TEST(QcSqlUpdateValidate, NoWhereClauseIsNotAProblem)
{
    // Unlike a dangling where()/and_()/or_() call, updating every row on
    // purpose (no WHERE at all) is legitimate SQL, not a gap.
    QcSqlUpdate update;
    update.table("users").set("name", QcSqlUpdate::QcVariant(std::string("A")));

    EXPECT_TRUE(update.validate().empty());
}

TEST(QcSqlUpdateToSql, ThrowsQcQueryBuildErrorWhenNoTable)
{
    QcSqlUpdate update;
    update.set("name", QcSqlUpdate::QcVariant(std::string("A")));

    EXPECT_THROW(update.toSql(), QcQueryBuildError);
}

TEST(QcSqlUpdateToSql, ThrowsQcQueryBuildErrorForIncompleteWhereCondition)
{
    QcSqlUpdate update;
    update.table("users").set("name", QcSqlUpdate::QcVariant(std::string("A")));
    update.where("id");

    EXPECT_THROW(update.toSql(), QcQueryBuildError);
}
