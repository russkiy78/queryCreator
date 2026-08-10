#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "query/qcsqldialect.h"
#include "query/qcsqlinsert.h"

// Shorthand for QcSqlDialect::quoteIdentifier() -- see the identical helper
// (and its rationale) in test_qcsqlquery.cpp.
namespace {
std::string q(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name);
}
} // namespace

// See test_qcsqlquery.cpp for why this is a friend fixture rather than
// public getters: toSql() (exercised separately below) is the public way to
// observe rendering, but set()/into()/returning() still need to be verified
// against real state, not just a chainable return value.
class QcSqlInsertWhiteBoxTest : public ::testing::Test
{
protected:
    static const std::string & table(const QcSqlInsert & q) { return q.m_table; }
    static std::size_t columnCount(const QcSqlInsert & q) { return q.m_columns.size(); }
    static const std::string & columnName(const QcSqlInsert & q, std::size_t i) { return q.m_columns[i].first; }
    static const QcSqlInsert::QcVariant & columnValue(const QcSqlInsert & q, std::size_t i) { return q.m_columns[i].second; }
    static const QcSqlInsert::QcStringList & returningColumns(const QcSqlInsert & q) { return q.m_returning; }
    static const std::string & autoIncrementColumn(const QcSqlInsert & q) { return q.m_autoIncrementColumn; }
};

TEST_F(QcSqlInsertWhiteBoxTest, IntoSetsTargetTableAndReturnsChainableReference)
{
    QcSqlInsert insert;

    QcSqlInsert & ref = insert.into("users");

    EXPECT_EQ(&ref, &insert);
    EXPECT_EQ(table(insert), "users");
}

TEST_F(QcSqlInsertWhiteBoxTest, SetAppendsColumnValuePairsInCallOrder)
{
    QcSqlInsert insert;

    insert.set("id", QcSqlInsert::QcVariant(1LL));
    insert.set("name", QcSqlInsert::QcVariant(std::string("Widget")));

    ASSERT_EQ(columnCount(insert), 2u);
    EXPECT_EQ(columnName(insert, 0), "id");
    EXPECT_EQ(columnName(insert, 1), "name");
    EXPECT_EQ(std::get<long long>(columnValue(insert, 0)), 1LL);
    EXPECT_EQ(std::get<std::string>(columnValue(insert, 1)), "Widget");
}

TEST_F(QcSqlInsertWhiteBoxTest, SetReturnsChainableReference)
{
    QcSqlInsert insert;

    QcSqlInsert & ref = insert.set("id", QcSqlInsert::QcVariant(1LL));

    EXPECT_EQ(&ref, &insert);
}

TEST_F(QcSqlInsertWhiteBoxTest, SetOverwritesEarlierValueForSameColumnKeepingItsPosition)
{
    QcSqlInsert insert;
    insert.set("id", QcSqlInsert::QcVariant(1LL));
    insert.set("name", QcSqlInsert::QcVariant(std::string("A")));

    insert.set("id", QcSqlInsert::QcVariant(2LL));

    ASSERT_EQ(columnCount(insert), 2u);
    EXPECT_EQ(columnName(insert, 0), "id");
    EXPECT_EQ(std::get<long long>(columnValue(insert, 0)), 2LL);
    EXPECT_EQ(columnName(insert, 1), "name");
}

TEST_F(QcSqlInsertWhiteBoxTest, SetAcceptsEveryQcVariantAlternative)
{
    const std::vector<std::byte> blob{std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}};
    QcSqlInsert insert;

    insert.set("i", QcSqlInsert::QcVariant(42LL));
    insert.set("d", QcSqlInsert::QcVariant(3.5));
    insert.set("s", QcSqlInsert::QcVariant(std::string("text")));
    insert.set("b", QcSqlInsert::QcVariant(blob));
    insert.set("n", QcSqlInsert::QcVariant{});

    ASSERT_EQ(columnCount(insert), 5u);
    EXPECT_EQ(std::get<long long>(columnValue(insert, 0)), 42LL);
    EXPECT_EQ(std::get<double>(columnValue(insert, 1)), 3.5);
    EXPECT_EQ(std::get<std::string>(columnValue(insert, 2)), "text");
    EXPECT_EQ(std::get<std::vector<std::byte>>(columnValue(insert, 3)), blob);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(columnValue(insert, 4)));
}

TEST_F(QcSqlInsertWhiteBoxTest, ReturningSetsColumnListAndReturnsChainableReference)
{
    QcSqlInsert insert;

    QcSqlInsert & ref = insert.returning({"id", "created_at"});

    EXPECT_EQ(&ref, &insert);
    ASSERT_EQ(returningColumns(insert).size(), 2u);
    EXPECT_EQ(returningColumns(insert)[0], "id");
    EXPECT_EQ(returningColumns(insert)[1], "created_at");
}

TEST_F(QcSqlInsertWhiteBoxTest, ReturningDefaultsToEmpty)
{
    QcSqlInsert insert;

    EXPECT_TRUE(returningColumns(insert).empty());
    EXPECT_TRUE(autoIncrementColumn(insert).empty());
}

TEST_F(QcSqlInsertWhiteBoxTest, ReturningWithAutoIncrementColumnSetsBothFields)
{
    QcSqlInsert insert;

    QcSqlInsert & ref = insert.returning({"id", "name"}, "id");

    EXPECT_EQ(&ref, &insert);
    ASSERT_EQ(returningColumns(insert).size(), 2u);
    EXPECT_EQ(returningColumns(insert)[0], "id");
    EXPECT_EQ(autoIncrementColumn(insert), "id");
}

TEST_F(QcSqlInsertWhiteBoxTest, PlainReturningAfterAutoIncrementOverloadClearsAutoIncrementColumn)
{
    // Reusing the same builder in a loop (like set() already needs to
    // support, see SetOverwritesEarlierValueForSameColumnKeepingItsPosition)
    // shouldn't leave a stale autoIncrementColumn from an earlier call
    // dangling around once the caller switches back to the plain overload.
    QcSqlInsert insert;
    insert.returning({"id"}, "id");

    insert.returning({"name"});

    EXPECT_TRUE(autoIncrementColumn(insert).empty());
    ASSERT_EQ(returningColumns(insert).size(), 1u);
    EXPECT_EQ(returningColumns(insert)[0], "name");
}

// =====================================================================
// toSql() -- structure, placeholder numbering
// =====================================================================

TEST(QcSqlInsertToSql, RendersColumnsAndPlaceholdersInInsertionOrder)
{
    QcSqlInsert insert;
    insert.into("users").set("id", QcSqlInsert::QcVariant(1LL)).set("name", QcSqlInsert::QcVariant(std::string("Widget")));

    const std::string expected = "INSERT INTO " + q("users") + " (" + q("id") + ", " + q("name") + ") VALUES ("
        + QcSqlDialect::placeholder(1) + ", " + QcSqlDialect::placeholder(2) + ")";
    QcSqlStatement statement = insert.toSql();
    EXPECT_EQ(statement.sql, expected);
    ASSERT_EQ(statement.params.size(), 2u);
    EXPECT_EQ(std::get<long long>(statement.params[0]), 1LL);
    EXPECT_EQ(std::get<std::string>(statement.params[1]), "Widget");
}

TEST(QcSqlInsertToSql, UseDriverSetsDialectForNoArgToSql)
{
    // Every other toSql() test in this file passes an explicit driver (see
    // docs/testing.md) -- this is the one test proving the "set once, up
    // front" useDriver()/no-arg-toSql() pairing itself actually works.
    QcSqlInsert insert;
    insert.useDriver(QcDbDriver::MySQL);
    insert.into("users").set("id", QcSqlInsert::QcVariant(1LL));

    const std::string expected = "INSERT INTO " + QcSqlDialect::quoteIdentifier("users", QcDbDriver::MySQL) + " ("
        + QcSqlDialect::quoteIdentifier("id", QcDbDriver::MySQL) + ") VALUES (" + QcSqlDialect::placeholder(1, QcDbDriver::MySQL) + ")";
    EXPECT_EQ(insert.toSql().sql, expected);
}

TEST(QcSqlInsertToSql, RendersManyColumnsInInsertionOrder)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch")
        .set("id", QcSqlInsert::QcVariant(1LL))
        .set("name", QcSqlInsert::QcVariant(std::string("Widget")))
        .set("amount", QcSqlInsert::QcVariant(19.99))
        .set("notes", QcSqlInsert::QcVariant(std::string("some note")))
        .set("is_archived", QcSqlInsert::QcVariant(0LL));

    const std::string expectedColumns = q("id") + ", " + q("name") + ", " + q("amount") + ", " + q("notes") + ", " + q("is_archived");
    std::string expectedValues;
    for (int i = 1; i <= 5; ++i) {
        if (i > 1) expectedValues += ", ";
        expectedValues += QcSqlDialect::placeholder(i);
    }
    QcSqlStatement statement = insert.toSql();
    EXPECT_EQ(statement.sql, "INSERT INTO " + q("qc_bt_dml_scratch") + " (" + expectedColumns + ") VALUES (" + expectedValues + ")");
    ASSERT_EQ(statement.params.size(), 5u);
    EXPECT_EQ(std::get<long long>(statement.params[0]), 1LL);
    EXPECT_EQ(std::get<std::string>(statement.params[1]), "Widget");
    EXPECT_EQ(std::get<double>(statement.params[2]), 19.99);
    EXPECT_EQ(std::get<std::string>(statement.params[3]), "some note");
    EXPECT_EQ(std::get<long long>(statement.params[4]), 0LL);
}

TEST(QcSqlInsertToSql, EmptyColumnsRendersEmptyColumnAndValueLists)
{
    QcSqlInsert insert;
    insert.into("users");

    EXPECT_EQ(insert.toSql().sql, "INSERT INTO " + q("users") + " () VALUES ()");
}

// =====================================================================
// toSql() -- one test per QcVariant alternative
// =====================================================================

TEST(QcSqlInsertToSql, IntegerValueBindsLongLong)
{
    QcSqlInsert insert;
    insert.into("t").set("n", QcSqlInsert::QcVariant(-123LL));

    QcSqlStatement statement = insert.toSql();
    ASSERT_EQ(statement.params.size(), 1u);
    EXPECT_EQ(std::get<long long>(statement.params[0]), -123LL);
}

TEST(QcSqlInsertToSql, DoubleValueBindsDouble)
{
    QcSqlInsert insert;
    insert.into("t").set("n", QcSqlInsert::QcVariant(2.71828));

    QcSqlStatement statement = insert.toSql();
    ASSERT_EQ(statement.params.size(), 1u);
    EXPECT_DOUBLE_EQ(std::get<double>(statement.params[0]), 2.71828);
}

TEST(QcSqlInsertToSql, StringValueBindsString)
{
    QcSqlInsert insert;
    insert.into("t").set("n", QcSqlInsert::QcVariant(std::string("hello world")));

    QcSqlStatement statement = insert.toSql();
    ASSERT_EQ(statement.params.size(), 1u);
    EXPECT_EQ(std::get<std::string>(statement.params[0]), "hello world");
}

TEST(QcSqlInsertToSql, BlobValueBindsByteVector)
{
    const std::vector<std::byte> blob{std::byte{0x00}, std::byte{0xAB}, std::byte{0xFF}, std::byte{0x00}};
    QcSqlInsert insert;
    insert.into("t").set("payload", QcSqlInsert::QcVariant(blob));

    QcSqlStatement statement = insert.toSql();
    ASSERT_EQ(statement.params.size(), 1u);
    EXPECT_EQ(std::get<std::vector<std::byte>>(statement.params[0]), blob);
}

TEST(QcSqlInsertToSql, NullValueBindsMonostate)
{
    QcSqlInsert insert;
    insert.into("users").set("notes", QcSqlInsert::QcVariant{});

    QcSqlStatement statement = insert.toSql();
    ASSERT_EQ(statement.params.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(statement.params[0]));
}

TEST(QcSqlInsertToSql, MixedTypesInOneRowBindEachAtItsOwnType)
{
    const std::vector<std::byte> blob{std::byte{0x10}, std::byte{0x20}};
    QcSqlInsert insert;
    insert.into("t")
        .set("i", QcSqlInsert::QcVariant(7LL))
        .set("d", QcSqlInsert::QcVariant(1.5))
        .set("s", QcSqlInsert::QcVariant(std::string("mixed")))
        .set("b", QcSqlInsert::QcVariant(blob))
        .set("n", QcSqlInsert::QcVariant{});

    QcSqlStatement statement = insert.toSql();
    ASSERT_EQ(statement.params.size(), 5u);
    EXPECT_EQ(std::get<long long>(statement.params[0]), 7LL);
    EXPECT_EQ(std::get<double>(statement.params[1]), 1.5);
    EXPECT_EQ(std::get<std::string>(statement.params[2]), "mixed");
    EXPECT_EQ(std::get<std::vector<std::byte>>(statement.params[3]), blob);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(statement.params[4]));
}

// =====================================================================
// toSql() -- large payloads (unit-level round trip through params; the
// real DB round trip for equivalent sizes is covered by
// test_integration_dml.cpp's HugeText*/HugeBinary* tests)
// =====================================================================

TEST(QcSqlInsertToSql, LargeTextValueRoundTripsExactlyThroughParams)
{
    std::string hugeText;
    hugeText.reserve(512 * 1024);
    for (int i = 0; i < 512 * 1024; ++i) {
        hugeText += static_cast<char>('a' + (i % 26));
    }

    QcSqlInsert insert;
    insert.into("t").set("notes", QcSqlInsert::QcVariant(hugeText));

    QcSqlStatement statement = insert.toSql();
    ASSERT_EQ(statement.params.size(), 1u);
    const std::string & readBack = std::get<std::string>(statement.params[0]);
    ASSERT_EQ(readBack.size(), hugeText.size());
    EXPECT_EQ(readBack, hugeText);
    // The generated SQL text itself stays tiny -- large values are bound as
    // parameters, never concatenated into the statement text.
    EXPECT_EQ(statement.sql, "INSERT INTO " + q("t") + " (" + q("notes") + ") VALUES (" + QcSqlDialect::placeholder(1) + ")");
}

TEST(QcSqlInsertToSql, LargeBinaryValueRoundTripsExactlyThroughParams)
{
    std::vector<std::byte> hugeBlob(512 * 1024);
    for (std::size_t i = 0; i < hugeBlob.size(); ++i) {
        hugeBlob[i] = static_cast<std::byte>(i % 256);
    }

    QcSqlInsert insert;
    insert.into("t").set("payload", QcSqlInsert::QcVariant(hugeBlob));

    QcSqlStatement statement = insert.toSql();
    ASSERT_EQ(statement.params.size(), 1u);
    const std::vector<std::byte> & readBack = std::get<std::vector<std::byte>>(statement.params[0]);
    ASSERT_EQ(readBack.size(), hugeBlob.size());
    EXPECT_EQ(readBack, hugeBlob);
}

// =====================================================================
// toSql() -- returning()
// =====================================================================

TEST(QcSqlInsertToSql, ReturningMatchesActiveDriverShapeAndPosition)
{
    // Runs once per driver in a single build/binary now (previously this was
    // an #if/#elif chain, only one branch of which ever compiled/ran per
    // build).
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlInsert insert;
        insert.into("users").set("name", QcSqlInsert::QcVariant(std::string("Widget"))).returning({"id", "created_at"});

        const QcSqlStatement statement = insert.toSql(driver);
        const std::string & sql = statement.sql;
        const std::string name = QcSqlDialect::quoteIdentifier("name", driver);
        const std::string id = QcSqlDialect::quoteIdentifier("id", driver);
        const std::string createdAt = QcSqlDialect::quoteIdentifier("created_at", driver);
        const std::string table = QcSqlDialect::quoteIdentifier("users", driver);
        // returningColumnNames is a plain copy of returning()'s argument,
        // unconditional across every driver -- see the field's doc comment in
        // qcsqlbase.h. Checked once here regardless of which branch below
        // runs, since it doesn't vary by driver the way sql/returningColumnCount do.
        EXPECT_EQ(statement.returningColumnNames, (QcSqlInsert::QcStringList{"id", "created_at"})) << "driver=" << static_cast<int>(driver);

        switch (driver) {
            case QcDbDriver::PostgreSQL:
            case QcDbDriver::SQLite:
                EXPECT_EQ(sql, "INSERT INTO " + table + " (" + name + ") VALUES (" + QcSqlDialect::placeholder(1, driver) + ") RETURNING " + id + ", " + createdAt);
                EXPECT_EQ(statement.returningColumnCount, 0u);
                break;
            case QcDbDriver::MSSQL:
                // OUTPUT sits between the column list and VALUES, not at the end.
                // "inserted" itself is an internal T-SQL keyword supplied by the
                // production code, not caller data -- only the column names are quoted.
                EXPECT_EQ(sql, "INSERT INTO " + table + " (" + name + ") OUTPUT inserted." + id + ", inserted." + createdAt + " VALUES (" + QcSqlDialect::placeholder(1, driver) + ")");
                EXPECT_EQ(statement.returningColumnCount, 0u);
                break;
            case QcDbDriver::Oracle:
                // RETURNING ... INTO's OUT-bind placeholders continue from the one IN
                // placeholder (the "name" value) already used -- :2, :3, matching
                // QcNativeConnection::executeReturning()'s expectations, communicated to
                // it via returningColumnCount.
                EXPECT_EQ(sql, "INSERT INTO " + table + " (" + name + ") VALUES (" + QcSqlDialect::placeholder(1, driver)
                    + ") RETURNING " + id + ", " + createdAt + " INTO " + QcSqlDialect::placeholder(2, driver) + ", " + QcSqlDialect::placeholder(3, driver));
                EXPECT_EQ(statement.returningColumnCount, 2u);
                break;
            case QcDbDriver::MySQL:
                // MySQL can't express this in one statement -- this overload of
                // returning() (no autoIncrementColumn) silently has no effect on the
                // rendered SQL, and mysqlReturningSelectSql stays empty (see the
                // autoIncrementColumn-specific tests below for the emulated equivalent).
                EXPECT_EQ(sql, "INSERT INTO " + table + " (" + name + ") VALUES (" + QcSqlDialect::placeholder(1, driver) + ")");
                EXPECT_TRUE(statement.mysqlReturningSelectSql.empty());
                break;
        }
    }
}

TEST(QcSqlInsertToSql, WithoutReturningReturningColumnCountAndMysqlPlanStayAtDefaults)
{
    QcSqlInsert insert;
    insert.into("users").set("id", QcSqlInsert::QcVariant(1LL));

    const QcSqlStatement statement = insert.toSql();
    EXPECT_EQ(statement.returningColumnCount, 0u);
    EXPECT_TRUE(statement.returningColumnNames.empty());
    EXPECT_TRUE(statement.mysqlReturningSelectSql.empty());
}

TEST(QcSqlInsertToSql, AutoIncrementColumnBuildsMysqlFollowUpSelectAndLeavesInsertUnchanged)
{
    QcSqlInsert insert;
    insert.into("users").set("name", QcSqlInsert::QcVariant(std::string("Widget"))).returning({"id", "name"}, "id");

    const QcSqlStatement statement = insert.toSql(QcDbDriver::MySQL);
    // MySQL quotes with backticks, not q()'s default (PostgreSQL) double
    // quotes -- quote explicitly for this driver.
    auto mq = [](const std::string & name) { return QcSqlDialect::quoteIdentifier(name, QcDbDriver::MySQL); };
    // The INSERT itself is untouched -- MySQL has no RETURNING syntax to
    // splice in, regardless of which returning() overload was used.
    EXPECT_EQ(statement.sql, "INSERT INTO " + mq("users") + " (" + mq("name") + ") VALUES (" + QcSqlDialect::placeholder(1, QcDbDriver::MySQL) + ")");
    EXPECT_EQ(statement.mysqlReturningSelectSql,
              "SELECT " + mq("id") + ", " + mq("name") + " FROM " + mq("users") + " WHERE " + mq("id") + " = ?");
}

TEST(QcSqlInsertToSql, ReturningWithoutAutoIncrementColumnLeavesMysqlPlanEmpty)
{
    QcSqlInsert insert;
    insert.into("users").set("name", QcSqlInsert::QcVariant(std::string("Widget"))).returning({"id"});

    EXPECT_TRUE(insert.toSql(QcDbDriver::MySQL).mysqlReturningSelectSql.empty());
}

TEST(QcSqlInsertToSql, WithoutReturningRendersNoExtraClause)
{
    QcSqlInsert insert;
    insert.into("users").set("id", QcSqlInsert::QcVariant(1LL));

    EXPECT_EQ(insert.toSql().sql, "INSERT INTO " + q("users") + " (" + q("id") + ") VALUES (" + QcSqlDialect::placeholder(1) + ")");
}

TEST(QcSqlInsertToSql, NoArgOverloadMatchesThreadingOverload)
{
    QcSqlInsert insert;
    insert.into("users").set("id", QcSqlInsert::QcVariant(1LL));

    QcSqlInsert::QcVariantList params;
    const std::string sqlFromThreadingOverload = insert.toSql(params);
    QcSqlStatement statement = insert.toSql();

    EXPECT_EQ(statement.sql, sqlFromThreadingOverload);
    ASSERT_EQ(statement.params.size(), params.size());
    EXPECT_EQ(std::get<long long>(statement.params[0]), std::get<long long>(params[0]));
}

// =====================================================================
// validate() / toSql() structural-completeness checks
// =====================================================================

TEST(QcSqlInsertValidate, NoTableIsReported)
{
    QcSqlInsert insert;
    insert.set("id", QcSqlInsert::QcVariant(1LL));

    const QcSqlInsert::QcStringList problems = insert.validate();
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("table"), std::string::npos);
}

TEST(QcSqlInsertValidate, WellFormedInsertHasNoProblems)
{
    QcSqlInsert insert;
    insert.into("users").set("id", QcSqlInsert::QcVariant(1LL));

    EXPECT_TRUE(insert.validate().empty());
}

TEST(QcSqlInsertToSql, ThrowsQcQueryBuildErrorWhenNoTable)
{
    QcSqlInsert insert;
    insert.set("id", QcSqlInsert::QcVariant(1LL));

    EXPECT_THROW(insert.toSql(), QcQueryBuildError);
}

TEST(QcSqlInsertToSql, ThrowsQcQueryBuildErrorEvenWhenReturningWasRequested)
{
    // The user-facing motivating case: RETURNING was set up but into() never
    // was -- toSql() must not silently emit "INSERT INTO  (...) ... RETURNING ...".
    QcSqlInsert insert;
    insert.set("id", QcSqlInsert::QcVariant(1LL)).returning({"id"});

    EXPECT_THROW(insert.toSql(), QcQueryBuildError);
}
