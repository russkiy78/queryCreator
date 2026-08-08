// "Battle" tests for INSERT/UPDATE/DELETE against a real database.
//
// Every INSERT/UPDATE/DELETE below is built through the QcSqlInsert/
// QcSqlUpdate/QcSqlDelete fluent builders (see qcsqlinsert.h/qcsqlupdate.h/
// qcsqldelete.h) and run through QcNativeConnection::execute() with the
// resulting QcSqlStatement{sql, params} -- not hand-assembled SQL text --
// the same way test_integration_select.cpp exercises QcSqlQuery. The only
// SQL text left as raw strings here is genuinely outside what those
// builders express: DDL (SetUp/TearDown), transaction control
// (BEGIN/COMMIT/ROLLBACK), read-only verification SELECTs, and the three
// JSON tests that assign a dialect function's *return value* to a column
// (`SET metadata = jsonb_set(metadata, ...)`) -- a computed expression, not
// a column = literal assignment, which is what set() models.
//
// Each test gets a freshly (re)created scratch table (see SetUp()/TearDown())
// so DML tests can freely insert/update/delete without interfering with each
// other -- unlike test_integration_select.cpp's suite-wide read-only shared
// dataset, mutation tests need per-test isolation.
//
// The whole suite is skipped (GTEST_SKIP()) if no database is reachable.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "query/qcnativeconnection.h"
#include "query/qcsqlbase.h"
#include "query/qcsqldelete.h"
#include "query/qcsqldialect.h"
#include "query/qcsqlinsert.h"
#include "query/qcsqlupdate.h"
#include "testdbconfig.h"
#include "testintegrationsupport.h"

namespace {

// Shorthand for QcSqlDialect::quoteIdentifier() -- used below to build the
// hand-written SQL text (DDL, raw verification SELECTs, computed-expression
// UPDATEs) that never goes through the query builder and so is never quoted
// automatically. Quotes for primaryTestDriver() specifically (not
// quoteIdentifier()'s PostgreSQL default) -- s_conn was opened against that
// driver, so its quoting rules are the only ones that produce valid
// identifiers here. See the identical helper (and its rationale) in
// test_integration_select.cpp.
std::string qi(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name, primaryTestDriver());
}

} // namespace

class QcIntegrationDmlTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite();
    static void TearDownTestSuite();
    void SetUp() override;
    void TearDown() override;

    static std::optional<QcResultSet> execute(const std::string & sql, const QcSqlBase::QcVariantList & params = {});
    // Renders and runs a QcSqlInsert/QcSqlUpdate/QcSqlDelete in one call --
    // every DML statement below goes through this, never a hand-written SQL
    // string.
    template <typename Statement>
    static std::optional<QcResultSet> run(const Statement & statement);
    // Named counterpart of run() above -- executeReturningNamed() instead of
    // executeReturning(), see the "Named result rows" section below.
    template <typename Statement>
    static std::optional<QcNamedResultSet> runNamed(const Statement & statement);
    // "DROP TABLE IF EXISTS" itself is not standard DDL every driver here
    // accepts -- Oracle rejects the IF EXISTS clause outright (ORA-00933,
    // verified directly), so a best-effort "drop this table, don't care if
    // it wasn't there" needs a PL/SQL wrapper swallowing ORA-00942 there
    // instead.
    static void dropTableIfExists(const std::string & table);

    static std::unique_ptr<QcNativeConnection> s_conn;
};

std::unique_ptr<QcNativeConnection> QcIntegrationDmlTest::s_conn;

void QcIntegrationDmlTest::SetUpTestSuite()
{
    try {
        s_conn = std::make_unique<QcNativeConnection>(testDbConfigOrDefault());
    } catch (const std::exception & e) {
        GTEST_SKIP() << "No local database reachable via testDbConfigOrDefault(): " << e.what();
    }
}

void QcIntegrationDmlTest::TearDownTestSuite()
{
    s_conn.reset();
}

std::optional<QcResultSet> QcIntegrationDmlTest::execute(const std::string & sql, const QcSqlBase::QcVariantList & params)
{
    return s_conn->execute(sql, params);
}

template <typename Statement>
std::optional<QcResultSet> QcIntegrationDmlTest::run(const Statement & statement)
{
    // primaryTestDriver(), not toSql()'s PostgreSQL default -- s_conn was
    // opened against primaryTestDriver() (see testDbConfigOrDefault() in
    // SetUpTestSuite()), and every driver but that one renders incompatible
    // SQL (wrong placeholder syntax, wrong quoting, ...).
    const QcSqlStatement rendered = statement.toSql(primaryTestDriver());
    // executeReturning() rather than plain execute() -- needed for Oracle's
    // RETURNING...INTO OUT binds (rendered.returningColumnCount), a no-op
    // distinction (identical to execute()) on every other driver and for
    // every non-RETURNING statement (returningColumnCount == 0) -- see
    // QcNativeConnection::executeReturning()'s doc comment.
    return s_conn->executeReturning(rendered.sql, rendered.params, rendered.returningColumnCount);
}

template <typename Statement>
std::optional<QcNamedResultSet> QcIntegrationDmlTest::runNamed(const Statement & statement)
{
    const QcSqlStatement rendered = statement.toSql(primaryTestDriver());
    return s_conn->executeReturningNamed(rendered.sql, rendered.params, rendered.returningColumnCount, rendered.returningColumnNames);
}

void QcIntegrationDmlTest::dropTableIfExists(const std::string & table)
{
    if (primaryTestDriver() == QcDbDriver::Oracle) {
        // Quoted, case-matching SetUp()'s CREATE TABLE below -- see the doc
        // comment on QcSqlDialect::quoteIdentifier() in qcsqldialect.h for why
        // an unquoted DROP TABLE here would miss a quoted-lowercase table.
        execute("BEGIN EXECUTE IMMEDIATE 'DROP TABLE \"" + table + "\"'; EXCEPTION WHEN OTHERS THEN NULL; END;");
    } else {
        execute("DROP TABLE IF EXISTS " + table);
    }
}

void QcIntegrationDmlTest::SetUp()
{
    dropTableIfExists("qc_bt_dml_scratch");

    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            ASSERT_TRUE(execute(
                "CREATE TABLE qc_bt_dml_scratch ("
                "  id INTEGER PRIMARY KEY,"
                "  name VARCHAR(200) NOT NULL,"
                "  amount NUMERIC(14,2) NULL,"
                "  notes TEXT NULL,"
                "  payload BYTEA NULL,"
                "  metadata JSONB NULL,"
                "  is_archived INTEGER NOT NULL DEFAULT 0"
                ")").has_value());
            break;
        case QcDbDriver::SQLite:
            ASSERT_TRUE(execute(
                "CREATE TABLE qc_bt_dml_scratch ("
                "  id INTEGER PRIMARY KEY,"
                "  name TEXT NOT NULL,"
                "  amount NUMERIC NULL,"
                "  notes TEXT NULL,"
                "  payload BLOB NULL,"
                "  metadata TEXT NULL CHECK (metadata IS NULL OR json_valid(metadata)),"
                "  is_archived INTEGER NOT NULL DEFAULT 0"
                ")").has_value());
            break;
        case QcDbDriver::Oracle:
            // NUMBER(1) for is_active/is_archived (no native BOOLEAN in Oracle SQL,
            // same convention as SQLite's INTEGER 0/1 above); JSON is a native
            // datatype since 21c (verified directly against this XE instance) --
            // same validate-on-write guarantee JSONB gives on PostgreSQL. Every
            // identifier is double-quoted, lowercase, matching exactly what
            // QcSqlDialect::quoteIdentifier()/quoteRef() emit for this same text --
            // see the doc comment on quoteIdentifier() in qcsqldialect.h.
            ASSERT_TRUE(execute(
                "CREATE TABLE \"qc_bt_dml_scratch\" ("
                "  \"id\" NUMBER PRIMARY KEY,"
                "  \"name\" VARCHAR2(200) NOT NULL,"
                "  \"amount\" NUMBER(14,2) NULL,"
                "  \"notes\" CLOB NULL,"
                "  \"payload\" BLOB NULL,"
                "  \"metadata\" JSON NULL,"
                "  \"is_archived\" NUMBER(1) DEFAULT 0 NOT NULL"
                ")").has_value());
            break;
        case QcDbDriver::MSSQL:
            // NVARCHAR(MAX) (not VARCHAR(MAX)) for name/notes -- this suite's own
            // SpecialCharactersAndUnicodeRoundTripThroughDml test inserts Japanese
            // text and an emoji, which plain VARCHAR's codepage-dependent narrow
            // encoding can't represent (see the UTF-16 round-tripping comment in
            // qcdrivermssql.cpp); INTEGER for is_archived, same 0/1 convention
            // as SQLite/Oracle above; JSON has no native datatype, so metadata gets
            // an explicit ISJSON() CHECK for the same validate-on-write guarantee
            // JSONB gives on PostgreSQL.
            ASSERT_TRUE(execute(
                "CREATE TABLE qc_bt_dml_scratch ("
                "  id INT PRIMARY KEY,"
                "  name NVARCHAR(200) NOT NULL,"
                "  amount DECIMAL(14,2) NULL,"
                "  notes NVARCHAR(MAX) NULL,"
                "  payload VARBINARY(MAX) NULL,"
                "  metadata NVARCHAR(MAX) NULL CHECK (metadata IS NULL OR ISJSON(metadata) = 1),"
                "  is_archived INT NOT NULL DEFAULT 0"
                ")").has_value());
            break;
        case QcDbDriver::MySQL:
            // LONGTEXT/LONGBLOB (not TEXT/BLOB, which cap at 64 KiB) -- this suite's
            // own Huge text/binary tests insert up to 7 MB values. No native
            // BOOLEAN (BOOL is just an alias for TINYINT(1)) -- INT 0/1 for
            // is_archived, same convention as SQLite/Oracle/MSSQL above. JSON is a
            // native datatype (5.7.8+) and validates on write by itself, same as
            // Oracle's native JSON column above -- no separate CHECK needed.
            ASSERT_TRUE(execute(
                "CREATE TABLE qc_bt_dml_scratch ("
                "  id INT PRIMARY KEY,"
                "  name VARCHAR(200) NOT NULL,"
                "  amount DECIMAL(14,2) NULL,"
                "  notes LONGTEXT NULL,"
                "  payload LONGBLOB NULL,"
                "  metadata JSON NULL,"
                "  is_archived INT NOT NULL DEFAULT 0"
                ")").has_value());
            break;
    }
}

void QcIntegrationDmlTest::TearDown()
{
    dropTableIfExists("qc_bt_dml_scratch");
}

// =====================================================================
// INSERT / UPDATE / DELETE basics
// =====================================================================

TEST_F(QcIntegrationDmlTest, InsertSingleRowPersistsAllColumns)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch")
        .set("id", 1LL)
        .set("name", std::string("Widget"))
        .set("amount", 19.99)
        .set("is_archived", 0LL);
    ASSERT_TRUE(run(insert).has_value());

    const std::string selectSql = "SELECT " + qi("name") + ", " + qi("amount") + ", " + qi("is_archived") + " FROM " + qi("qc_bt_dml_scratch")
        + " WHERE " + qi("id") + " = " + QcSqlDialect::placeholder(1, primaryTestDriver());
    auto result = execute(selectSql, {1LL});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "Widget");
    EXPECT_NEAR(asDouble((*result)[0][1]), 19.99, 0.01);
    EXPECT_EQ(asInt64((*result)[0][2]), 0);
}

TEST_F(QcIntegrationDmlTest, InsertMultipleRowsInLoopAllPersist)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    constexpr int rowCount = 300;
    for (int i = 1; i <= rowCount; ++i) {
        insert.set("id", static_cast<long long>(i)).set("name", "row" + std::to_string(i));
        ASSERT_TRUE(run(insert).has_value());
    }

    auto result = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(asInt64((*result)[0][0]), rowCount);
}

TEST_F(QcIntegrationDmlTest, UpdateModifiesOnlyTargetedRow)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    insert.set("id", 1LL).set("name", std::string("A"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("id", 2LL).set("name", std::string("B"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("id", 3LL).set("name", std::string("C"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("name", std::string("B-updated"));
    update.where("id").isEqualTo(2LL);
    ASSERT_TRUE(run(update).has_value());

    auto result = execute("SELECT " + qi("id") + ", " + qi("name") + " FROM " + qi("qc_bt_dml_scratch") + " ORDER BY " + qi("id"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3u);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "A");
    EXPECT_EQ(std::get<std::string>((*result)[1][1]), "B-updated");
    EXPECT_EQ(std::get<std::string>((*result)[2][1]), "C");
}

TEST_F(QcIntegrationDmlTest, UpdateWithWhereAffectsOnlyMatchingRows)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    for (int i = 1; i <= 5; ++i) {
        insert.set("id", static_cast<long long>(i)).set("name", "row" + std::to_string(i)).set("amount", static_cast<double>(i * 10));
        ASSERT_TRUE(run(insert).has_value());
    }

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("is_archived", 1LL);
    update.where("amount").isGreaterThan(25.0);
    ASSERT_TRUE(run(update).has_value());

    auto result = execute("SELECT " + qi("id") + ", " + qi("is_archived") + " FROM " + qi("qc_bt_dml_scratch") + " ORDER BY " + qi("id"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 5u);
    for (auto & row : *result) {
        const long long id = asInt64(row[0]);
        EXPECT_EQ(asInt64(row[1]), id > 2 ? 1 : 0); // amounts 30/40/50 (ids 3/4/5) exceed 25
    }
}

TEST_F(QcIntegrationDmlTest, DeleteRemovesTargetedRowOnly)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    insert.set("id", 1LL).set("name", std::string("A"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("id", 2LL).set("name", std::string("B"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlDelete del;
    del.from("qc_bt_dml_scratch");
    del.where("id").isEqualTo(1LL);
    ASSERT_TRUE(run(del).has_value());

    auto result = execute("SELECT " + qi("id") + " FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 2);
}

TEST_F(QcIntegrationDmlTest, DeleteWithWhereRemovesOnlyMatchingRows)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    for (int i = 1; i <= 5; ++i) {
        insert.set("id", static_cast<long long>(i)).set("name", "row" + std::to_string(i)).set("is_archived", static_cast<long long>(i % 2));
        ASSERT_TRUE(run(insert).has_value());
    }

    QcSqlDelete del;
    del.from("qc_bt_dml_scratch");
    del.where("is_archived").isEqualTo(1LL);
    ASSERT_TRUE(run(del).has_value());

    auto result = execute("SELECT " + qi("id") + " FROM " + qi("qc_bt_dml_scratch") + " ORDER BY " + qi("id"));
    ASSERT_TRUE(result.has_value());
    std::vector<long long> remaining;
    for (auto & row : *result) remaining.push_back(asInt64(row[0]));
    EXPECT_EQ(remaining, (std::vector<long long>{2, 4})); // odd ids (1,3,5) were archived and got deleted
}

TEST_F(QcIntegrationDmlTest, DeleteAllRowsEmptiesTable)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    for (int i = 1; i <= 10; ++i) {
        insert.set("id", static_cast<long long>(i)).set("name", "row" + std::to_string(i));
        ASSERT_TRUE(run(insert).has_value());
    }

    QcSqlDelete del;
    del.from("qc_bt_dml_scratch");
    ASSERT_TRUE(run(del).has_value());

    auto result = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(asInt64((*result)[0][0]), 0);
}

// =====================================================================
// RETURNING
// =====================================================================

// RETURNING/OUTPUT/RETURNING...INTO is implemented (in
// QcSqlDialect::returningClause()) for PostgreSQL/SQLite/MSSQL/Oracle --
// run() above uses QcNativeConnection::executeReturning() rather than plain
// execute(), which is what makes Oracle's OUT-bind variant of RETURNING
// transparent to the assertions below (they look identical to the other
// three drivers' ordinary-result-row RETURNING/OUTPUT). MySQL alone has no
// single-statement equivalent at all -- see
// QueryCreatorTest::ExecuteInsertReturningEmulatedViaLastInsertIdOnMysql in
// test_querycreator.cpp for its separate, INSERT-only mechanism (needs
// QueryCreator to orchestrate multiple statements on one leased connection,
// which this file's run()/execute() helpers, wrapping a single raw
// QcNativeConnection, don't do) -- and qcsqldialect.h for why UPDATE/DELETE
// stay unsupported there. Every test in this section GTEST_SKIPs on MySQL
// rather than being compiled out, since MySQL may or may not be
// primaryTestDriver() in a build that also compiles other drivers in.

TEST_F(QcIntegrationDmlTest, InsertReturningYieldsTheInsertedRow)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no INSERT ... RETURNING syntax -- see QueryCreatorTest's LastInsertId-emulation tests instead";
    }
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("Widget")).set("amount", 19.99);
    insert.returning({"id", "name"});

    auto result = run(insert);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 1);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "Widget");

    // The row really was inserted, not just echoed back without persisting.
    auto countResult = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 1);
}

// Multi-row RETURNING (WHERE matches more than one row) -- skipped on
// Oracle, see UpdateReturningOnMultipleRowsFailsCleanlyOnOracle below for
// why: this project's executeReturningStatement() binds Oracle's OUT
// placeholders as scalars, one value per RETURNING column, which only ever
// has room for one row's worth of data. Real multi-row DML RETURNING on
// Oracle needs OCI_DATA_AT_EXEC + application-supplied OCIBindDynamic()
// callbacks (per Oracle's own OCI Programmer's Guide -- verified directly
// too: a scalar bind against a 2-row UPDATE fails outright, see the
// Oracle-only tests below) -- a materially different, dynamic-piecewise-fetch
// mechanism, not implemented here. PostgreSQL/SQLite/MSSQL have no such
// limitation -- RETURNING/OUTPUT rides back as an ordinary multi-row result
// set on all three, same as any SELECT.
TEST_F(QcIntegrationDmlTest, UpdateReturningYieldsEachUpdatedRow)
{
    if (primaryTestDriver() == QcDbDriver::MySQL || primaryTestDriver() == QcDbDriver::Oracle) {
        GTEST_SKIP() << "MySQL has no RETURNING; Oracle's scalar OUT-bind can't hold multiple rows -- see the Oracle-only test below";
    }
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    insert.set("id", 1LL).set("name", std::string("A"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("id", 2LL).set("name", std::string("B"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("id", 3LL).set("name", std::string("C"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("is_archived", 1LL);
    update.where("id").isLessThanOrEqualTo(2LL);
    update.returning({"id"});

    auto result = run(update);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    std::vector<long long> ids;
    for (auto & row : *result) ids.push_back(asInt64(row[0]));
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<long long>{1, 2}));

    // Row 3 (not matched by WHERE) must be untouched.
    auto untouched = execute("SELECT " + qi("is_archived") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 3");
    ASSERT_TRUE(untouched.has_value());
    EXPECT_EQ(asInt64((*untouched)[0][0]), 0);
}

// Oracle counterpart of UpdateReturningYieldsEachUpdatedRow above, scoped to
// a WHERE that matches exactly one row -- the case the scalar OUT-bind in
// executeReturningStatement() actually supports (see its doc comment in
// qcdriveroracle.cpp).
TEST_F(QcIntegrationDmlTest, UpdateReturningYieldsTheUpdatedRowOnOracle)
{
    if (primaryTestDriver() != QcDbDriver::Oracle) {
        GTEST_SKIP() << "Oracle-specific: scalar OUT-bind single-row RETURNING";
    }
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    insert.set("id", 1LL).set("name", std::string("A"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("id", 2LL).set("name", std::string("B"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("is_archived", 1LL);
    update.where("id").isEqualTo(2LL);
    update.returning({"id"});

    auto result = run(update);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 2);

    // Row 1 (not matched by WHERE) must be untouched.
    auto untouched = execute("SELECT " + qi("is_archived") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(untouched.has_value());
    EXPECT_EQ(asInt64((*untouched)[0][0]), 0);
}

// Documents the actual boundary of Oracle RETURNING...INTO support here,
// verified directly rather than assumed: a scalar OUT bind has room for
// exactly one row's value per RETURNING column, so a DML that affects more
// than one row fails -- ORA-24369, "required callbacks not registered for
// one or more bind handles" (Oracle's own error for "this needs
// OCI_DATA_AT_EXEC + OCIBindDynamic() callbacks, a materially different,
// dynamic-piecewise-fetch mechanism, not implemented here").
//
// The first version of this test caught a real correctness bug, not just
// documented an unsupported case: the underlying UPDATE actually still
// applied to both rows despite OCIStmtExecute() reporting failure -- the
// error surfaces only for the RETURNING side, after the DML itself already
// ran, and under the OCI_COMMIT_ON_SUCCESS mode executeReturningStatement()
// used at the time, that meant a nullopt result with the write silently
// committed anyway. Fixed by never using OCI_COMMIT_ON_SUCCESS for
// RETURNING...INTO statements -- executeReturningStatement() now always
// executes with OCI_DEFAULT and commits/rolls back explicitly afterward
// based on the real outcome (see its doc comment in qcdriveroracle.cpp)
// -- so this test now also verifies the rollback actually happened, not
// just that run() reported failure.
TEST_F(QcIntegrationDmlTest, UpdateReturningOnMultipleRowsFailsCleanlyOnOracle)
{
    if (primaryTestDriver() != QcDbDriver::Oracle) {
        GTEST_SKIP() << "Oracle-specific: scalar OUT-bind can't hold multiple rows";
    }
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    insert.set("id", 1LL).set("name", std::string("A"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("id", 2LL).set("name", std::string("B"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("is_archived", 1LL);
    update.where("id").isLessThanOrEqualTo(2LL); // matches both rows
    update.returning({"id"});

    EXPECT_FALSE(run(update).has_value());

    // Neither row was left updated -- the failed statement's changes were
    // rolled back, not silently committed.
    auto result = execute("SELECT " + qi("is_archived") + " FROM " + qi("qc_bt_dml_scratch") + " ORDER BY " + qi("id"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    EXPECT_EQ(asInt64((*result)[0][0]), 0);
    EXPECT_EQ(asInt64((*result)[1][0]), 0);
}

TEST_F(QcIntegrationDmlTest, DeleteReturningYieldsTheDeletedRow)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no DELETE ... RETURNING syntax";
    }
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("ToDelete"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlDelete del;
    del.from("qc_bt_dml_scratch");
    del.where("id").isEqualTo(1LL);
    del.returning({"name"});

    auto result = run(del);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "ToDelete");

    // The row really is gone, not just echoed back without deleting.
    auto countResult = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 0);
}

// =====================================================================
// Named result rows -- executeReturningNamed()
// =====================================================================

TEST_F(QcIntegrationDmlTest, InsertReturningNamedYieldsTheInsertedRowKeyedByColumnName)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no INSERT ... RETURNING syntax";
    }
    // Named counterpart of InsertReturningYieldsTheInsertedRow above -- same
    // insert, but read back through executeReturningNamed() instead of
    // executeReturning(). On Oracle specifically this exercises
    // returningColumnNames (QcSqlStatement's field, threaded through
    // QcSqlInsert::toSql() unconditionally) rather than driver result-set
    // metadata, since RETURNING...INTO's OUT binds have none of their own --
    // see executeReturningNamedStatement()'s doc comment in
    // qcdriveroracle.cpp.
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("Widget")).set("amount", 19.99);
    insert.returning({"id", "name"});

    auto result = runNamed(insert);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    const QcNamedRow & row = (*result)[0];
    ASSERT_EQ(row.size(), 2u);
    EXPECT_EQ(asInt64(row.at("id")), 1);
    EXPECT_EQ(std::get<std::string>(row.at("name")), "Widget");

    // The row really was inserted, not just echoed back without persisting.
    auto countResult = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 1);
}

TEST_F(QcIntegrationDmlTest, UpdateReturningNamedYieldsTheUpdatedRowKeyedByColumnName)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no UPDATE ... RETURNING syntax";
    }
    // WHERE matches exactly one row -- portable across every other driver,
    // including Oracle's scalar-OUT-bind limitation (see
    // UpdateReturningYieldsTheUpdatedRowOnOracle above for why multi-row
    // RETURNING is Oracle-only-tested separately, not repeated here for the
    // named path).
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    insert.set("id", 1LL).set("name", std::string("A"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("is_archived", 1LL);
    update.where("id").isEqualTo(1LL);
    update.returning({"id"});

    auto result = runNamed(update);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0].at("id")), 1);
}

TEST_F(QcIntegrationDmlTest, DeleteReturningNamedYieldsTheDeletedRowKeyedByColumnName)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no DELETE ... RETURNING syntax";
    }
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("ToDelete"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlDelete del;
    del.from("qc_bt_dml_scratch");
    del.where("id").isEqualTo(1LL);
    del.returning({"name"});

    auto result = runNamed(del);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0].at("name")), "ToDelete");
}

TEST_F(QcIntegrationDmlTest, ExecuteNamedOnPlainInsertWithoutReturningYieldsEmptyResultSet)
{
    // Every driver, not just the RETURNING-capable ones above -- mirrors
    // execute()'s "empty QcResultSet (not nullopt) for statements that don't
    // produce rows" contract (see qcnativeconnection.h) for the named path.
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("NoReturning"));
    const QcSqlStatement statement = insert.toSql(primaryTestDriver());

    auto namedResult = s_conn->executeNamed(statement.sql, statement.params);
    ASSERT_TRUE(namedResult.has_value());
    EXPECT_TRUE(namedResult->empty());

    // The row really was inserted, not just echoed back without persisting.
    auto countResult = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 1);
}

// =====================================================================
// NULL handling / constraints
// =====================================================================

TEST_F(QcIntegrationDmlTest, InsertNullIntoNullableColumnRoundTrips)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("A")).set("notes", QcSqlBase::QcVariant{});
    ASSERT_TRUE(run(insert).has_value());

    auto result = execute("SELECT " + qi("notes") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_TRUE(std::holds_alternative<std::monostate>((*result)[0][0]));
}

TEST_F(QcIntegrationDmlTest, UpdateSetsColumnToNull)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("A")).set("notes", std::string("some note"));
    ASSERT_TRUE(run(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("notes", QcSqlBase::QcVariant{});
    update.where("id").isEqualTo(1LL);
    ASSERT_TRUE(run(update).has_value());

    auto result = execute("SELECT " + qi("notes") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<std::monostate>((*result)[0][0]));
}

TEST_F(QcIntegrationDmlTest, NotNullConstraintViolationReturnsNullopt)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", QcSqlBase::QcVariant{}); // name is NOT NULL
    auto result = run(insert);
    EXPECT_FALSE(result.has_value());

    auto countResult = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 0);
}

TEST_F(QcIntegrationDmlTest, UniqueConstraintViolationReturnsNullopt)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("A"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("name", std::string("Duplicate")); // id stays 1 -- id is a PRIMARY KEY
    auto result = run(insert);
    EXPECT_FALSE(result.has_value());

    auto countResult = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 1);
}

// =====================================================================
// Transactions
// =====================================================================

TEST_F(QcIntegrationDmlTest, TransactionCommitPersistsChanges)
{
    ASSERT_TRUE(execute("BEGIN").has_value());
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    insert.set("id", 1LL).set("name", std::string("A"));
    ASSERT_TRUE(run(insert).has_value());
    insert.set("id", 2LL).set("name", std::string("B"));
    ASSERT_TRUE(run(insert).has_value());
    ASSERT_TRUE(execute("COMMIT").has_value());

    auto result = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(asInt64((*result)[0][0]), 2);
}

TEST_F(QcIntegrationDmlTest, TransactionRollbackDiscardsChanges)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    insert.set("id", 1LL).set("name", std::string("Committed"));
    ASSERT_TRUE(run(insert).has_value());

    ASSERT_TRUE(execute("BEGIN").has_value());
    insert.set("id", 2LL).set("name", std::string("RolledBack"));
    ASSERT_TRUE(run(insert).has_value());
    ASSERT_TRUE(execute("ROLLBACK").has_value());

    auto result = execute("SELECT " + qi("id") + ", " + qi("name") + " FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 1);
}

// =====================================================================
// Huge text
// =====================================================================

TEST_F(QcIntegrationDmlTest, HugeTextInsertReadBackByteForByteMatch)
{
    const std::string hugeText = generateRandomText(5 * 1024 * 1024, 111);

    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("huge-text")).set("notes", hugeText);
    ASSERT_TRUE(run(insert).has_value());

    auto result = execute("SELECT " + qi("notes") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    const std::string & readBack = std::get<std::string>((*result)[0][0]);
    ASSERT_EQ(readBack.size(), hugeText.size());
    EXPECT_EQ(readBack, hugeText);
}

TEST_F(QcIntegrationDmlTest, HugeTextUpdateReplacesContent)
{
    const std::string firstText = generateRandomText(3 * 1024 * 1024, 222);
    const std::string secondText = generateRandomText(6 * 1024 * 1024, 333);

    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("huge-text")).set("notes", firstText);
    ASSERT_TRUE(run(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("notes", secondText);
    update.where("id").isEqualTo(1LL);
    ASSERT_TRUE(run(update).has_value());

    auto result = execute("SELECT " + qi("notes") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    const std::string & readBack = std::get<std::string>((*result)[0][0]);
    EXPECT_EQ(readBack.size(), secondText.size());
    EXPECT_EQ(readBack, secondText);
    EXPECT_NE(readBack.size(), firstText.size());
}

TEST_F(QcIntegrationDmlTest, HugeTextRowDeleteRemovesIt)
{
    const std::string hugeText = generateRandomText(4 * 1024 * 1024, 444);
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("huge-text")).set("notes", hugeText);
    ASSERT_TRUE(run(insert).has_value());

    QcSqlDelete del;
    del.from("qc_bt_dml_scratch");
    del.where("id").isEqualTo(1LL);
    ASSERT_TRUE(run(del).has_value());

    auto result = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(asInt64((*result)[0][0]), 0);
}

// =====================================================================
// Huge binary
// =====================================================================

TEST_F(QcIntegrationDmlTest, HugeBinaryInsertReadBackByteForByteMatch)
{
    const std::vector<std::byte> hugeBlob = generateRandomBytes(5 * 1024 * 1024, 555);

    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("huge-blob")).set("payload", hugeBlob);
    ASSERT_TRUE(run(insert).has_value());

    auto result = execute("SELECT " + qi("payload") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    const std::vector<std::byte> readBack = asBytes((*result)[0][0]);
    ASSERT_EQ(readBack.size(), hugeBlob.size());
    EXPECT_EQ(readBack, hugeBlob);
}

TEST_F(QcIntegrationDmlTest, HugeBinaryUpdateReplacesContent)
{
    const std::vector<std::byte> firstBlob = generateRandomBytes(2 * 1024 * 1024, 666);
    const std::vector<std::byte> secondBlob = generateRandomBytes(7 * 1024 * 1024, 777);

    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("huge-blob")).set("payload", firstBlob);
    ASSERT_TRUE(run(insert).has_value());

    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("payload", secondBlob);
    update.where("id").isEqualTo(1LL);
    ASSERT_TRUE(run(update).has_value());

    auto result = execute("SELECT " + qi("payload") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    const std::vector<std::byte> readBack = asBytes((*result)[0][0]);
    EXPECT_EQ(readBack.size(), secondBlob.size());
    EXPECT_EQ(readBack, secondBlob);
}

TEST_F(QcIntegrationDmlTest, HugeBinaryRowDeleteRemovesIt)
{
    const std::vector<std::byte> hugeBlob = generateRandomBytes(3 * 1024 * 1024, 888);
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("huge-blob")).set("payload", hugeBlob);
    ASSERT_TRUE(run(insert).has_value());

    QcSqlDelete del;
    del.from("qc_bt_dml_scratch");
    del.where("id").isEqualTo(1LL);
    ASSERT_TRUE(run(del).has_value());

    auto result = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(asInt64((*result)[0][0]), 0);
}

TEST_F(QcIntegrationDmlTest, BinaryDataWithEmbeddedNulBytesPreservesExactLength)
{
    const std::vector<std::byte> blob = {
        std::byte{0x41}, std::byte{0x00}, std::byte{0x42}, std::byte{0x00}, std::byte{0x00}, std::byte{0x43},
    };

    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("nul-bytes")).set("payload", blob);
    ASSERT_TRUE(run(insert).has_value());

    auto result = execute("SELECT " + qi("payload") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    const std::vector<std::byte> readBack = asBytes((*result)[0][0]);
    ASSERT_EQ(readBack.size(), blob.size());
    EXPECT_EQ(readBack, blob);
}

// =====================================================================
// JSON: create, add, modify
// =====================================================================

TEST_F(QcIntegrationDmlTest, JsonColumnInsertIsQueryableImmediately)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("json-row"))
        .set("metadata", std::string("{\"status\":\"pending\",\"priority\":3}"));
    ASSERT_TRUE(run(insert).has_value());

    std::optional<QcResultSet> result;
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            result = execute("SELECT metadata->>'status', (metadata->>'priority')::int FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::SQLite:
            result = execute("SELECT json_extract(metadata, '$.status'), json_extract(metadata, '$.priority') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::Oracle:
            // "metadata"/"qc_bt_dml_scratch"/"id" are quoted here (unlike the
            // identical-looking MSSQL/MySQL branch below) because this suite's
            // Oracle schema itself is created with quoted, case-preserved DDL (see
            // SetUp()) -- an unquoted reference here would fold to uppercase and
            // miss the column/table entirely.
            result = execute("SELECT JSON_VALUE(" + qi("metadata") + ", '$.status'), JSON_VALUE(" + qi("metadata") + ", '$.priority') FROM "
                + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
            break;
        case QcDbDriver::MSSQL:
        case QcDbDriver::MySQL:
            result = execute("SELECT JSON_VALUE(metadata, '$.status'), JSON_VALUE(metadata, '$.priority') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
    }
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "pending");
    EXPECT_EQ(asInt64((*result)[0][1]), 3);
}

TEST_F(QcIntegrationDmlTest, JsonFieldAddedViaJsonSetFunction)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("json-row"))
        .set("metadata", std::string("{\"status\":\"pending\"}"));
    ASSERT_TRUE(run(insert).has_value());

    // "bonus" doesn't exist in the seeded document yet -- jsonb_set()/
    // json_set() add it. This assigns a *computed* SQL expression (the
    // function's return value, referencing the very column it updates), not
    // a column = literal, which is what QcSqlUpdate::set() models -- stays
    // raw SQL (see the file-level comment above).
    std::optional<QcResultSet> result;
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = jsonb_set(metadata, '{bonus}', '1000'::jsonb) WHERE id = 1").has_value());
            result = execute("SELECT (metadata->>'bonus')::int FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::SQLite:
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = json_set(metadata, '$.bonus', 1000) WHERE id = 1").has_value());
            result = execute("SELECT json_extract(metadata, '$.bonus') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::Oracle:
            // JSON_TRANSFORM (21c+, verified directly against this XE instance) is
            // Oracle's jsonb_set()/json_set() equivalent.
            ASSERT_TRUE(execute("UPDATE " + qi("qc_bt_dml_scratch") + " SET " + qi("metadata") + " = JSON_TRANSFORM(" + qi("metadata")
                + ", SET '$.bonus' = 1000) WHERE " + qi("id") + " = 1").has_value());
            result = execute("SELECT JSON_VALUE(" + qi("metadata") + ", '$.bonus') FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
            break;
        case QcDbDriver::MSSQL:
            // JSON_MODIFY() is T-SQL's jsonb_set()/json_set() equivalent -- it adds
            // a path that doesn't exist yet the same way it overwrites one that
            // does (see JsonFieldModifiedViaJsonSetFunction below). A non-string
            // new_value argument (the plain integer literal 1000, not a quoted
            // string) is inserted unquoted, i.e. as a JSON number rather than a
            // JSON string containing digits.
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = JSON_MODIFY(metadata, '$.bonus', 1000) WHERE id = 1").has_value());
            result = execute("SELECT JSON_VALUE(metadata, '$.bonus') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::MySQL:
            // JSON_SET() (5.7.8+) is MySQL's jsonb_set()/json_set() equivalent -- it
            // adds a path that doesn't exist yet the same way it overwrites one that
            // does (see JsonFieldModifiedViaJsonSetFunction below). An SQL integer
            // literal argument (1000, not a quoted string) is stored as a JSON
            // number, not a JSON string containing digits (verified directly).
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = JSON_SET(metadata, '$.bonus', 1000) WHERE id = 1").has_value());
            result = execute("SELECT JSON_VALUE(metadata, '$.bonus') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
    }
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 1000);
}

TEST_F(QcIntegrationDmlTest, JsonFieldModifiedViaJsonSetFunction)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("json-row"))
        .set("metadata", std::string("{\"status\":\"pending\"}"));
    ASSERT_TRUE(run(insert).has_value());

    // "status" already exists -- jsonb_set()/json_set() overwrite it in
    // place. Same computed-expression caveat as JsonFieldAddedViaJsonSetFunction above.
    std::optional<QcResultSet> result;
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = jsonb_set(metadata, '{status}', '\"approved\"'::jsonb) WHERE id = 1").has_value());
            result = execute("SELECT metadata->>'status' FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::SQLite:
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = json_set(metadata, '$.status', 'approved') WHERE id = 1").has_value());
            result = execute("SELECT json_extract(metadata, '$.status') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::Oracle:
            ASSERT_TRUE(execute("UPDATE " + qi("qc_bt_dml_scratch") + " SET " + qi("metadata") + " = JSON_TRANSFORM(" + qi("metadata")
                + ", SET '$.status' = 'approved') WHERE " + qi("id") + " = 1").has_value());
            result = execute("SELECT JSON_VALUE(" + qi("metadata") + ", '$.status') FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
            break;
        case QcDbDriver::MSSQL:
            // 'approved' is a string-typed new_value here, so JSON_MODIFY quotes it
            // as a JSON string (contrast with the unquoted numeric literal in
            // JsonFieldAddedViaJsonSetFunction above).
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = JSON_MODIFY(metadata, '$.status', 'approved') WHERE id = 1").has_value());
            result = execute("SELECT JSON_VALUE(metadata, '$.status') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::MySQL:
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = JSON_SET(metadata, '$.status', 'approved') WHERE id = 1").has_value());
            result = execute("SELECT JSON_VALUE(metadata, '$.status') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
    }
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "approved");
}

TEST_F(QcIntegrationDmlTest, JsonArrayElementAppendedViaJsonFunction)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("json-row"))
        .set("metadata", std::string("{\"tags\":[\"a\",\"b\"]}"));
    ASSERT_TRUE(run(insert).has_value());

    // Same computed-expression caveat as the two JSON tests above.
    std::optional<QcResultSet> result;
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            ASSERT_TRUE(execute(
                "UPDATE qc_bt_dml_scratch SET metadata = jsonb_set(metadata, '{tags}', (metadata->'tags') || '\"c\"'::jsonb) WHERE id = 1"
            ).has_value());
            result = execute("SELECT jsonb_array_length(metadata->'tags'), metadata->'tags'->>2 FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::SQLite:
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = json_insert(metadata, '$.tags[#]', 'c') WHERE id = 1").has_value());
            result = execute("SELECT json_array_length(metadata, '$.tags'), json_extract(metadata, '$.tags[2]') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::Oracle:
            ASSERT_TRUE(execute("UPDATE " + qi("qc_bt_dml_scratch") + " SET " + qi("metadata") + " = JSON_TRANSFORM(" + qi("metadata")
                + ", APPEND '$.tags' = 'c') WHERE " + qi("id") + " = 1").has_value());
            result = execute("SELECT JSON_VALUE(" + qi("metadata") + ", '$.tags.size()'), JSON_VALUE(" + qi("metadata") + ", '$.tags[2]') FROM "
                + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
            break;
        case QcDbDriver::MSSQL:
            // JSON_MODIFY's "append $.path" path-mode form is T-SQL's array-append
            // equivalent. T-SQL has no scalar "array length" function even in this
            // 2022 instance -- OPENJSON(), a table-valued function that expands a
            // JSON array/object into rows, is the standard workaround; used here as
            // a correlated subquery (referencing the outer row's `metadata` column)
            // just to get a row count back as a single scalar.
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = JSON_MODIFY(metadata, 'append $.tags', 'c') WHERE id = 1").has_value());
            result = execute(
                "SELECT (SELECT COUNT(*) FROM OPENJSON(metadata, '$.tags')), JSON_VALUE(metadata, '$.tags[2]') "
                "FROM qc_bt_dml_scratch WHERE id = 1");
            break;
        case QcDbDriver::MySQL:
            // JSON_ARRAY_APPEND() is MySQL's array-append equivalent; JSON_LENGTH()
            // with a path argument is its scalar "array length" function (unlike
            // MSSQL, no OPENJSON()-style row-expansion workaround needed).
            ASSERT_TRUE(execute("UPDATE qc_bt_dml_scratch SET metadata = JSON_ARRAY_APPEND(metadata, '$.tags', 'c') WHERE id = 1").has_value());
            result = execute("SELECT JSON_LENGTH(metadata, '$.tags'), JSON_VALUE(metadata, '$.tags[2]') FROM qc_bt_dml_scratch WHERE id = 1");
            break;
    }
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 3);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "c");
}

TEST_F(QcIntegrationDmlTest, InvalidJsonRejectedByColumnConstraint)
{
    // PostgreSQL's JSONB column type validates on write natively; the SQLite
    // schema adds an explicit CHECK(json_valid(...)) for parity (see SetUp())
    // -- both should reject this the same way.
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", std::string("bad-json"))
        .set("metadata", std::string("{not valid json"));
    auto result = run(insert);
    EXPECT_FALSE(result.has_value());

    auto countResult = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 0);
}

// =====================================================================
// Injection safety / kitchen sink
// =====================================================================

TEST_F(QcIntegrationDmlTest, SpecialCharactersAndUnicodeRoundTripThroughDml)
{
    const std::string tricky = "O'Reilly says \"hello\"; DROP TABLE qc_bt_dml_scratch; -- 日本語 🎉";

    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch").set("id", 1LL).set("name", tricky);
    ASSERT_TRUE(run(insert).has_value());

    auto result = execute("SELECT " + qi("name") + " FROM " + qi("qc_bt_dml_scratch") + " WHERE " + qi("id") + " = 1");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), tricky);

    // The embedded "DROP TABLE" is just data (bound as a parameter via
    // set(), never concatenated into SQL text) -- the table must still exist
    // and still contain exactly this one row.
    auto countResult = execute("SELECT COUNT(*) FROM " + qi("qc_bt_dml_scratch"));
    ASSERT_TRUE(countResult.has_value());
    EXPECT_EQ(asInt64((*countResult)[0][0]), 1);
}

TEST_F(QcIntegrationDmlTest, KitchenSinkDmlSequenceAcrossManyRows)
{
    QcSqlInsert insert;
    insert.into("qc_bt_dml_scratch");
    constexpr int rowCount = 100;
    for (int i = 1; i <= rowCount; ++i) {
        insert.set("id", static_cast<long long>(i)).set("name", "row" + std::to_string(i))
            .set("amount", static_cast<double>(i)).set("is_archived", 0LL);
        ASSERT_TRUE(run(insert).has_value());
    }

    // archive every row with an even id -- the modulo expression is used as
    // a raw SQL LHS operand, same convention QcSqlQuery's WHERE already
    // allows (a bare string is spliced in verbatim, see
    // qcsqlqueryelement.cpp). Oracle has no "%" arithmetic operator in SQL
    // (verified directly: ORA-00920) -- MOD(a, b) is its equivalent.
    const std::string evenIdExpr = (primaryTestDriver() == QcDbDriver::Oracle) ? "MOD(" + qi("id") + ", 2)" : "id % 2";
    QcSqlUpdate update;
    update.table("qc_bt_dml_scratch").set("is_archived", 1LL);
    update.where(evenIdExpr).isEqualTo(0LL);
    ASSERT_TRUE(run(update).has_value());

    // delete archived rows with amount below 50
    QcSqlDelete del;
    del.from("qc_bt_dml_scratch");
    del.where("is_archived").isEqualTo(1LL);
    del.and_("amount").isLessThan(50.0);
    ASSERT_TRUE(run(del).has_value());

    auto result = execute("SELECT " + qi("id") + ", " + qi("is_archived") + " FROM " + qi("qc_bt_dml_scratch") + " ORDER BY " + qi("id"));
    ASSERT_TRUE(result.has_value());

    std::size_t expectedRemaining = 0;
    for (int i = 1; i <= rowCount; ++i) {
        const bool archived = (i % 2 == 0);
        const bool deleted = archived && i < 50;
        if (!deleted) ++expectedRemaining;
    }
    EXPECT_EQ(result->size(), expectedRemaining);

    for (auto & row : *result) {
        const long long id = asInt64(row[0]);
        EXPECT_FALSE(id % 2 == 0 && id < 50); // those were deleted
        EXPECT_EQ(asInt64(row[1]), id % 2 == 0 ? 1 : 0);
    }
}
