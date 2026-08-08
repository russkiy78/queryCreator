// Tests for QueryCreator -- the facade tying QcSqlQuery/QcSqlInsert/
// QcSqlUpdate/QcSqlDelete's toSql() to QcConnectionPool::execute() in one
// call. Deliberately not re-testing what's already covered elsewhere
// (QcSqlQuery/QcSqlInsert/QcSqlUpdate/QcSqlDelete's own toSql() correctness
// is exhaustively covered in their own test files; QcConnectionPool's own
// pooling/threading/reconnect behavior in test_qcconnectionpool.cpp) --
// only that QueryCreator actually wires them together against a real
// database: builder in, real rows out.
//
// The whole suite is skipped (GTEST_SKIP()) if no database is reachable,
// same convention as every other integration suite here.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "query/qcnativeconnection.h"
#include "query/qcsqlbase.h"
#include "query/qcsqldelete.h"
#include "query/qcsqldialect.h"
#include "query/qcsqlinsert.h"
#include "query/qcsqlquery.h"
#include "query/qcsqlupdate.h"
#include "query/querycreator.h"
#include "testdbconfig.h"
#include "testintegrationsupport.h"

namespace {

// Shorthand for QcSqlDialect::quoteIdentifier() -- used below to build the
// hand-written SQL text (DDL, raw verification SELECTs) that never goes
// through the query builder and so is never quoted automatically. Quotes
// for primaryTestDriver() specifically (not quoteIdentifier()'s PostgreSQL
// default) -- s_conn was opened against that driver, so its quoting rules
// are the only ones that produce valid identifiers here. See the identical
// helper (and its rationale) in test_integration_select.cpp.
std::string qi(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name, primaryTestDriver());
}

} // namespace

class QueryCreatorTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite();
    static void TearDownTestSuite();
    void SetUp() override;
    void TearDown() override;

    // "DROP TABLE IF EXISTS" itself is not standard DDL every driver here
    // accepts -- Oracle rejects the IF EXISTS clause outright (ORA-00933),
    // same as every other integration suite in this project (see
    // test_integration_select.cpp/test_integration_dml.cpp's
    // identically-named helper).
    static void dropTableIfExists(const std::string & table);

    static std::unique_ptr<QcNativeConnection> s_conn;
};

std::unique_ptr<QcNativeConnection> QueryCreatorTest::s_conn;

void QueryCreatorTest::SetUpTestSuite()
{
    try {
        s_conn = std::make_unique<QcNativeConnection>(testDbConfigOrDefault());
    } catch (const std::exception & e) {
        GTEST_SKIP() << "No local database reachable via testDbConfigOrDefault(): " << e.what();
    }
}

void QueryCreatorTest::TearDownTestSuite()
{
    s_conn.reset();
}

void QueryCreatorTest::dropTableIfExists(const std::string & table)
{
    if (primaryTestDriver() == QcDbDriver::Oracle) {
        // Quoted, case-matching SetUp()'s CREATE TABLE below -- see the doc
        // comment on QcSqlDialect::quoteIdentifier() in qcsqldialect.h for why
        // an unquoted DROP TABLE here would miss a quoted-lowercase table.
        s_conn->execute("BEGIN EXECUTE IMMEDIATE 'DROP TABLE \"" + table + "\"'; EXCEPTION WHEN OTHERS THEN NULL; END;");
    } else {
        s_conn->execute("DROP TABLE IF EXISTS " + table);
    }
}

void QueryCreatorTest::SetUp()
{
    dropTableIfExists("qc_bt_facade_scratch");
    // INTEGER is a portable choice across all five drivers without
    // per-driver branching (SQLite/PostgreSQL/MySQL/MSSQL accept it
    // directly; Oracle accepts it as an ANSI synonym for NUMBER(38)) --
    // avoids the VARCHAR/VARCHAR2/NVARCHAR naming differences that would
    // otherwise need branching, since this suite only needs to prove the
    // facade wires builder->pool correctly, not exercise per-driver typing
    // (already covered by test_integration_select.cpp/test_integration_dml.cpp).
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE " + qi("qc_bt_facade_scratch") + " (" + qi("id") + " INTEGER PRIMARY KEY, " + qi("value") + " INTEGER)").has_value());
}

void QueryCreatorTest::TearDown()
{
    dropTableIfExists("qc_bt_facade_scratch");
}

TEST_F(QueryCreatorTest, ConstructorThrowsWhenDatabaseIsUnreachable)
{
    // Permanent mode (QueryCreator's default) opens every pooled connection
    // eagerly in the constructor -- a bad database name/path should surface
    // as a thrown std::runtime_error immediately, the same as
    // QcNativeConnection's own constructor (see QcConnectionPoolTest's
    // identically-purposed "...DoesNotExist" tests per driver).
    QcConnectionParams params = testDbConfigOrDefault();
    if (primaryTestDriver() == QcDbDriver::SQLite) {
        // SQLite has no network to be unreachable over -- a parent directory
        // that doesn't exist is the closest equivalent "can't connect" failure.
        params.database = "/qc_this_directory_does_not_exist_xyz/db.sqlite3";
    } else {
        params.database = "qc_this_database_does_not_exist";
    }

    // Brace-init, not QueryCreator(params) -- the latter, as a bare
    // expression-statement inside EXPECT_THROW's expansion, most-vexing-parses
    // as a declaration of a new local named `params` of type QueryCreator
    // (default-constructed), not a call to QueryCreator's constructor.
    EXPECT_THROW(QueryCreator{params}, std::runtime_error);
}

TEST_F(QueryCreatorTest, ExecuteInsertThenSelectRoundTripsThroughThePool)
{
    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_scratch").set("id", 1LL).set("value", 42LL);
    ASSERT_TRUE(qc.execute(insert).has_value());

    QcSqlQuery query;
    query.fromTable("qc_bt_facade_scratch");
    query.addReturnValues({"value"});
    query.where("id").isEqualTo(1LL);

    auto result = qc.execute(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    // SQLite/MSSQL/MySQL hand back a typed long long here, PostgreSQL/Oracle
    // hand back text -- normalize the same way every other integration
    // suite in this project does (see testintegrationsupport.cpp's
    // asInt64()), just inlined since this is the only place in this file
    // that needs it.
    const QcSqlBase::QcVariant & cell = (*result)[0][0];
    const long long value = std::holds_alternative<std::string>(cell)
        ? std::stoll(std::get<std::string>(cell))
        : std::get<long long>(cell);
    EXPECT_EQ(value, 42);
}

TEST_F(QueryCreatorTest, ExecuteUpdateModifiesTheRow)
{
    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_scratch").set("id", 1LL).set("value", 1LL);
    ASSERT_TRUE(qc.execute(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_facade_scratch").set("value", 99LL);
    update.where("id").isEqualTo(1LL);
    ASSERT_TRUE(qc.execute(update).has_value());

    auto result = qc.execute("SELECT " + qi("value") + " FROM " + qi("qc_bt_facade_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
}

TEST_F(QueryCreatorTest, ExecuteDeleteRemovesTheRow)
{
    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_scratch").set("id", 1LL).set("value", 1LL);
    ASSERT_TRUE(qc.execute(insert).has_value());

    QcSqlDelete del;
    del.from("qc_bt_facade_scratch");
    del.where("id").isEqualTo(1LL);
    ASSERT_TRUE(qc.execute(del).has_value());

    auto result = qc.execute("SELECT " + qi("id") + " FROM " + qi("qc_bt_facade_scratch"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(QueryCreatorTest, ExecuteRawSqlPassesThroughToTheConnection)
{
    QueryCreator qc(testDbConfigOrDefault());

    const std::string sql = "INSERT INTO " + qi("qc_bt_facade_scratch") + " (" + qi("id") + ", " + qi("value") + ") VALUES ("
        + QcSqlDialect::placeholder(1, primaryTestDriver()) + ", " + QcSqlDialect::placeholder(2, primaryTestDriver()) + ")";
    ASSERT_TRUE(qc.execute(sql, {1LL, 7LL}).has_value());

    auto result = qc.execute("SELECT COUNT(*) FROM " + qi("qc_bt_facade_scratch"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
}

TEST_F(QueryCreatorTest, ExecuteReturnsNulloptOnInvalidSql)
{
    QueryCreator qc(testDbConfigOrDefault());

    EXPECT_FALSE(qc.execute("THIS IS NOT SQL").has_value());
}

TEST_F(QueryCreatorTest, ExecuteNamedInsertThenSelectRoundTripsThroughThePool)
{
    // Named counterpart of ExecuteInsertThenSelectRoundTripsThroughThePool
    // above -- same insert/select, but through the executeNamed() overloads,
    // proving the facade wires QcNativeConnection::executeNamed() through the
    // pool correctly, not just execute().
    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_scratch").set("id", 1LL).set("value", 42LL);
    ASSERT_TRUE(qc.executeNamed(insert).has_value());

    QcSqlQuery query;
    query.fromTable("qc_bt_facade_scratch");
    query.addReturnValues({"value"});
    query.where("id").isEqualTo(1LL);

    auto result = qc.executeNamed(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    const QcSqlBase::QcVariant & cell = (*result)[0].at("value");
    const long long value = std::holds_alternative<std::string>(cell)
        ? std::stoll(std::get<std::string>(cell))
        : std::get<long long>(cell);
    EXPECT_EQ(value, 42);
}

TEST_F(QueryCreatorTest, ExecuteNamedRawSqlPassesThroughToTheConnection)
{
    QueryCreator qc(testDbConfigOrDefault());

    const std::string sql = "INSERT INTO " + qi("qc_bt_facade_scratch") + " (" + qi("id") + ", " + qi("value") + ") VALUES ("
        + QcSqlDialect::placeholder(1, primaryTestDriver()) + ", " + QcSqlDialect::placeholder(2, primaryTestDriver()) + ")";
    ASSERT_TRUE(qc.executeNamed(sql, {1LL, 7LL}).has_value());

    auto result = qc.executeNamed("SELECT " + qi("value") + " FROM " + qi("qc_bt_facade_scratch"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0].at("value")), 7);
}

TEST_F(QueryCreatorTest, ExecuteNamedReturnsNulloptOnInvalidSql)
{
    QueryCreator qc(testDbConfigOrDefault());

    EXPECT_FALSE(qc.executeNamed("THIS IS NOT SQL").has_value());
}

TEST_F(QueryCreatorTest, ExecuteInsertReturningYieldsTheInsertedRow)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no INSERT ... RETURNING syntax -- see the LastInsertId-emulation tests below instead";
    }
    // Proves QueryCreator::execute(const QcSqlInsert&) actually goes through
    // executeReturning(), not plain execute() -- on Oracle specifically,
    // this is the only place returning()'s RETURNING...INTO OUT binds
    // (QcSqlStatement::returningColumnCount) get exercised through the
    // facade, since QueryCreator owns the pool/lease itself rather than
    // handing back a raw QcNativeConnection the way test_integration_dml.cpp's
    // run() helper does. See that file's identically-purposed
    // InsertReturningYieldsTheInsertedRow for the lower-level version of
    // this same proof, across all four RETURNING-capable drivers.
    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_scratch").set("id", 1LL).set("value", 42LL);
    insert.returning({"id", "value"});

    auto result = qc.execute(insert);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 1);
    EXPECT_EQ(asInt64((*result)[0][1]), 42);
}

TEST_F(QueryCreatorTest, ExecuteNamedInsertReturningYieldsTheInsertedRowKeyedByColumnName)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no INSERT ... RETURNING syntax -- see the LastInsertId-emulation tests below instead";
    }
    // Named counterpart of ExecuteInsertReturningYieldsTheInsertedRow above
    // -- proves QueryCreator::executeNamed(const QcSqlInsert&) goes through
    // executeStatementNamed()/executeReturningNamed(), including Oracle's
    // returningColumnNames-keyed OUT-bind path.
    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_scratch").set("id", 1LL).set("value", 42LL);
    insert.returning({"id", "value"});

    auto result = qc.executeNamed(insert);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0].at("id")), 1);
    EXPECT_EQ(asInt64((*result)[0].at("value")), 42);
}

TEST_F(QueryCreatorTest, ExecuteNamedUpdateReturningYieldsTheUpdatedRowKeyedByColumnName)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no UPDATE ... RETURNING syntax";
    }
    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_scratch").set("id", 1LL).set("value", 1LL);
    ASSERT_TRUE(qc.execute(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_facade_scratch").set("value", 99LL);
    update.where("id").isEqualTo(1LL);
    update.returning({"value"});

    auto result = qc.executeNamed(update);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0].at("value")), 99);
}

TEST_F(QueryCreatorTest, ExecuteNamedDeleteReturningYieldsTheDeletedRowKeyedByColumnName)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no DELETE ... RETURNING syntax";
    }
    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_scratch").set("id", 1LL).set("value", 5LL);
    ASSERT_TRUE(qc.execute(insert).has_value());

    QcSqlDelete del;
    del.from("qc_bt_facade_scratch");
    del.where("id").isEqualTo(1LL);
    del.returning({"value"});

    auto result = qc.executeNamed(del);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0].at("value")), 5);
}

TEST_F(QueryCreatorTest, ExecuteInsertReturningEmulatedViaLastInsertIdOnMysql)
{
    if (primaryTestDriver() != QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL-specific: LAST_INSERT_ID() emulation";
    }
    // MySQL has no INSERT ... RETURNING syntax at all -- returning()'s
    // autoIncrementColumn overload (see qcsqlinsert.h) is what
    // QueryCreator::execute(const QcSqlInsert&) uses instead: BEGIN, the
    // insert, a follow-up SELECT keyed on LAST_INSERT_ID(), COMMIT, all
    // against the one connection leased from the pool for this call (see
    // querycreator.cpp's executeMysqlInsertReturning()). Needs its own
    // AUTO_INCREMENT table -- qc_bt_facade_scratch's id is caller-assigned
    // (see SetUp()), not AUTO_INCREMENT, so LAST_INSERT_ID() would have
    // nothing meaningful to report against it.
    dropTableIfExists("qc_bt_facade_autoincrement");
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_facade_autoincrement (id INT PRIMARY KEY AUTO_INCREMENT, name VARCHAR(200) NOT NULL)").has_value());

    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_autoincrement").set("name", std::string("Widget"));
    insert.returning({"id", "name"}, "id");

    auto result = qc.execute(insert);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_GT(asInt64((*result)[0][0]), 0);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "Widget");

    // The row really was inserted (and the transaction really committed),
    // not just echoed back without persisting.
    auto countResult = qc.execute("SELECT COUNT(*) FROM " + qi("qc_bt_facade_autoincrement"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 1);

    dropTableIfExists("qc_bt_facade_autoincrement");
}

TEST_F(QueryCreatorTest, ExecuteNamedInsertReturningEmulatedViaLastInsertIdOnMysql)
{
    if (primaryTestDriver() != QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL-specific: LAST_INSERT_ID() emulation";
    }
    // Named counterpart of ExecuteInsertReturningEmulatedViaLastInsertIdOnMysql
    // above -- proves executeMysqlInsertReturningNamed() (querycreator.cpp)
    // wires the same BEGIN/insert/LAST_INSERT_ID() prefix into a follow-up
    // executeNamed() call instead of execute() for the final SELECT.
    dropTableIfExists("qc_bt_facade_autoincrement");
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_facade_autoincrement (id INT PRIMARY KEY AUTO_INCREMENT, name VARCHAR(200) NOT NULL)").has_value());

    QueryCreator qc(testDbConfigOrDefault());

    QcSqlInsert insert;
    insert.into("qc_bt_facade_autoincrement").set("name", std::string("Gadget"));
    insert.returning({"id", "name"}, "id");

    auto result = qc.executeNamed(insert);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_GT(asInt64((*result)[0].at("id")), 0);
    EXPECT_EQ(std::get<std::string>((*result)[0].at("name")), "Gadget");

    // The row really was inserted (and the transaction really committed),
    // not just echoed back without persisting.
    auto countResult = qc.execute("SELECT COUNT(*) FROM " + qi("qc_bt_facade_autoincrement"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 1);

    dropTableIfExists("qc_bt_facade_autoincrement");
}
