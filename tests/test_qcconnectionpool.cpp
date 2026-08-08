#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

#include "query/qcconnectionparams.h"
#include "query/qcconnectionpool.h"
#include "query/qcnativeconnection.h"
#include "testdbconfig.h"

namespace {

// Connection parameters come from tests/db_config.ini (see
// db_config.ini.example) when present, falling back to this project's
// long-standing local-dev defaults otherwise (see testdbconfig.cpp) -- if
// no database matching primaryTestDriver() is reachable, connecting throws,
// which SetUp() below turns into a GTEST_SKIP() rather than a hard failure.
QcConnectionParams testParams()
{
    return testDbConfigOrDefault();
}

// A trivial, valid-on-every-driver query for tests that only care whether
// a connection is usable, not what it returns -- Oracle has no bare
// "SELECT 1" (no implicit one-row source table; needs "FROM DUAL", see
// qcdriveroracle.cpp's kPingQuery), every other driver here accepts it bare.
const char * selectOne()
{
    return (primaryTestDriver() == QcDbDriver::Oracle) ? "SELECT 1 FROM DUAL" : "SELECT 1";
}

// A fresh on-disk path for tests that need real file-lock contention between
// two independent connections -- the shared-cache in-memory database above
// is fine for pool mechanics, but exercising sqlite3_busy_timeout() honestly
// needs an actual file two separate handles can contend over.
std::string uniqueTempDbPath()
{
    static std::atomic<long long> counter{0};
    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path()
            / ("qc_connectionpool_test_" + std::to_string(tag) + "_" + std::to_string(counter++) + ".sqlite3"))
        .string();
}

// Every test here needs a real, reachable database — skip instead of failing
// when one isn't available (e.g. no local PostgreSQL on this machine/CI).
class QcConnectionPoolTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        try {
            QcNativeConnection probe(testParams());
        } catch (const std::exception & e) {
            GTEST_SKIP() << "No local database reachable via testParams(): " << e.what();
        }
    }
};

} // namespace

TEST(QcConnectionPool, RejectsZeroSize)
{
    EXPECT_THROW(QcConnectionPool(QcConnectionParams{}, QcConnectionPool::Mode::Permanent, 0), std::invalid_argument);
}

// The runtime-dispatch counterpart of the old "won't even compile" story:
// QC_DB_DRIVERS (CMakeLists.txt) now selects which native drivers this
// binary links, and QcConnectionParams::driver picks among them at runtime
// -- constructing a QcNativeConnection for a driver outside that compiled-in
// set must fail loudly (not silently pick some other driver's behavior).
// Finds whichever of the five drivers this particular build did NOT compile
// in (compiledInDrivers(), see testdbconfig.cpp) -- skips only if every
// driver was compiled in (QC_DB_DRIVERS=All), since there's nothing left to
// prove not-compiled-in behavior with.
TEST(QcConnectionPool, ConstructingWithDriverNotCompiledIntoThisBuildThrows)
{
    constexpr QcDbDriver kEveryDriver[] = {
        QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL,
    };
    const std::vector<QcDbDriver> compiled = compiledInDrivers();

    const QcDbDriver * uncompiled = nullptr;
    for (const QcDbDriver & driver : kEveryDriver) {
        if (std::find(compiled.begin(), compiled.end(), driver) == compiled.end()) {
            uncompiled = &driver;
            break;
        }
    }
    if (!uncompiled) {
        GTEST_SKIP() << "every driver is compiled into this build (QC_DB_DRIVERS=All) -- nothing to prove here";
    }

    QcConnectionParams params;
    params.driver = *uncompiled;
    EXPECT_THROW(QcNativeConnection connection(params), std::runtime_error);
}

// Runs against whichever of PostgreSQL/MSSQL/MySQL is primaryTestDriver() --
// all three reject a nonexistent database at login time regardless of
// otherwise-valid credentials (PostgreSQL: trusts loopback auth on this test
// box, so a bad password wouldn't exercise this; MSSQL: error 4060, "Cannot
// open database ... requested by the login"; MySQL: error 1049, "Unknown
// database"). SQLite/Oracle have their own dedicated tests below instead
// (SQLite's SQLITE_OPEN_CREATE makes a missing *database* succeed by
// provisioning it; Oracle rejects a bad *service name*, not a database name).
TEST_F(QcConnectionPoolTest, NativeConnectionThrowsWhenDatabaseDoesNotExist)
{
    if (primaryTestDriver() != QcDbDriver::PostgreSQL && primaryTestDriver() != QcDbDriver::MSSQL && primaryTestDriver() != QcDbDriver::MySQL) {
        GTEST_SKIP() << "database-does-not-exist has its own dedicated test for this driver";
    }
    QcConnectionParams params = testParams();
    params.database = "qc_this_database_does_not_exist";

    EXPECT_THROW(QcNativeConnection connection(params), std::runtime_error);
}

// Runs against whichever of PostgreSQL/MSSQL/MySQL is primaryTestDriver() --
// all three expose a connect-timeout knob this project maps
// QcConnectionParams::connectTimeoutSeconds onto (libpq's connect_timeout,
// ODBC's SQL_ATTR_LOGIN_TIMEOUT, libmysqlclient's MYSQL_OPT_CONNECT_TIMEOUT
// respectively -- see each driver's openConnection() in qcdriver*.cpp).
// Oracle's OCILogon2 has no such knob at all (not implemented, see
// qcdriveroracle.cpp); SQLite has no network to time out on in the first
// place (connectTimeoutSeconds maps to sqlite3_busy_timeout() instead, see
// BusyTimeoutBoundsWaitOnALockedDatabase below) -- neither is tested here.
TEST_F(QcConnectionPoolTest, ConnectTimeoutBoundsAnUnreachableHost)
{
    if (primaryTestDriver() != QcDbDriver::PostgreSQL && primaryTestDriver() != QcDbDriver::MSSQL && primaryTestDriver() != QcDbDriver::MySQL) {
        GTEST_SKIP() << "connectTimeoutSeconds has no effect on this driver";
    }
    // 10.255.255.1 is non-routable in this sandbox and silently drops
    // packets rather than refusing the connection (verified separately) —
    // exactly the case connect_timeout exists for. Without it, the native
    // client would hang for the OS's own TCP connect timeout (commonly 60s+).
    QcConnectionParams params = testParams();
    params.host = "10.255.255.1";
    params.connectTimeoutSeconds = 1;

    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(QcNativeConnection connection(params), std::runtime_error);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST_F(QcConnectionPoolTest, NativeConnectionThrowsWhenParentDirectoryDoesNotExist)
{
    if (primaryTestDriver() != QcDbDriver::SQLite) {
        GTEST_SKIP() << "SQLite-specific: SQLITE_OPEN_CREATE";
    }
    // SQLITE_OPEN_CREATE (see qcdriversqlite.cpp) makes connecting to a
    // missing *file* succeed by provisioning it -- there's no SQLite
    // equivalent to PostgreSQL's "database does not exist" error for a bare
    // name. A missing *directory* in the path is the closest real failure:
    // SQLite can't create the file there either.
    QcConnectionParams params;
    params.driver = QcDbDriver::SQLite;
    params.database = "/qc_this_directory_does_not_exist_xyz/db.sqlite3";

    EXPECT_THROW(QcNativeConnection connection(params), std::runtime_error);
}

TEST_F(QcConnectionPoolTest, BusyTimeoutBoundsWaitOnALockedDatabase)
{
    if (primaryTestDriver() != QcDbDriver::SQLite) {
        GTEST_SKIP() << "SQLite-specific: sqlite3_busy_timeout()";
    }
    // SQLite has no network to time out on, so connectTimeoutSeconds instead
    // maps to sqlite3_busy_timeout() -- how long to wait on another
    // connection's write lock (see qcdriversqlite.cpp). Exercised here
    // with two real connections to the same on-disk file so the lock is
    // genuine, not simulated.
    const std::string path = uniqueTempDbPath();

    QcConnectionParams writerParams;
    writerParams.driver = QcDbDriver::SQLite;
    writerParams.database = path;
    QcNativeConnection writer(writerParams);
    ASSERT_TRUE(writer.execute("CREATE TABLE t (id INTEGER)").has_value());
    ASSERT_TRUE(writer.execute("BEGIN IMMEDIATE").has_value());
    ASSERT_TRUE(writer.execute("INSERT INTO t (id) VALUES (1)").has_value()); // uncommitted -- holds the write lock

    QcConnectionParams contenderParams;
    contenderParams.driver = QcDbDriver::SQLite;
    contenderParams.database = path;
    contenderParams.connectTimeoutSeconds = 1;
    QcNativeConnection contender(contenderParams);

    const auto start = std::chrono::steady_clock::now();
    const auto result = contender.execute("INSERT INTO t (id) VALUES (2)");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.has_value()); // SQLITE_BUSY once busy_timeout elapses
    EXPECT_GE(elapsed, std::chrono::milliseconds(900));
    EXPECT_LT(elapsed, std::chrono::seconds(5));

    writer.execute("ROLLBACK");
    std::filesystem::remove(path);
}

TEST_F(QcConnectionPoolTest, NativeConnectionThrowsWhenServiceNameDoesNotExist)
{
    if (primaryTestDriver() != QcDbDriver::Oracle) {
        GTEST_SKIP() << "Oracle-specific: Easy Connect service name";
    }
    // Mirrors NativeConnectionThrowsWhenDatabaseDoesNotExist above -- a bad
    // service name in the Easy Connect string (see qcdriveroracle.cpp) is
    // rejected by the listener (ORA-12514) regardless of otherwise-valid
    // credentials.
    QcConnectionParams params = testParams();
    params.database = "qc_this_service_does_not_exist";

    EXPECT_THROW(QcNativeConnection connection(params), std::runtime_error);
}

TEST_F(QcConnectionPoolTest, PermanentModeHandsOutWorkingConnections)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 2);

    auto lease = pool.acquire();
    EXPECT_TRUE(lease.connection().isOpen());
    EXPECT_TRUE(lease.connection().execute(selectOne()).has_value());
}

TEST_F(QcConnectionPoolTest, PermanentModeReusesTheSamePhysicalConnection)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 1);

    void * firstHandle = nullptr;
    {
        auto lease = pool.acquire();
        firstHandle = lease.connection().nativeHandle();
    }
    {
        auto lease = pool.acquire();
        EXPECT_EQ(lease.connection().nativeHandle(), firstHandle);
    }
}

TEST_F(QcConnectionPoolTest, PermanentModeBlocksUntilAConnectionIsReleased)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 1);

    std::promise<void> acquiring;
    std::atomic<bool> acquired{false};
    std::thread waiter;

    {
        auto lease = pool.acquire();

        waiter = std::thread([&] {
            acquiring.set_value();
            auto second = pool.acquire();
            acquired = true;
        });

        acquiring.get_future().wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        EXPECT_FALSE(acquired.load()); // only connection is still checked out
    } // lease released here -> waiter's acquire() should unblock

    waiter.join();
    EXPECT_TRUE(acquired.load());
}

TEST_F(QcConnectionPoolTest, TryAcquireTimesOutWhenPoolIsExhausted)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 1);

    auto lease = pool.acquire();

    const auto start = std::chrono::steady_clock::now();
    auto timedOut = pool.tryAcquire(std::chrono::milliseconds(100));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(timedOut.has_value());
    EXPECT_GE(elapsed, std::chrono::milliseconds(100));
}

TEST_F(QcConnectionPoolTest, TryAcquireSucceedsOnceAConnectionIsFree)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 1);

    {
        auto lease = pool.acquire();
    } // released before tryAcquire below

    auto lease = pool.tryAcquire(std::chrono::milliseconds(100));
    ASSERT_TRUE(lease.has_value());
    EXPECT_TRUE(lease->connection().execute(selectOne()).has_value());
}

TEST_F(QcConnectionPoolTest, PermanentModeReconnectsAfterConnectionIsDroppedServerSide)
{
    // No SQLite/Oracle/MSSQL equivalent here: this is specifically about
    // detecting a server process killing a connection out from under the
    // pool (isAlive()'s reason for existing, see qcnativeconnection.h) via
    // that driver's own admin-kill statement -- SQLite has no server
    // process to drop a connection from, and Oracle/MSSQL aren't exercised
    // by this particular test (no equivalent kill statement wired up here).
    if (primaryTestDriver() != QcDbDriver::PostgreSQL && primaryTestDriver() != QcDbDriver::MySQL) {
        GTEST_SKIP() << "no server-side kill statement wired up for this driver";
    }
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 1);

    if (primaryTestDriver() == QcDbDriver::PostgreSQL) {
        std::string deadPid;
        {
            auto lease = pool.acquire();
            auto result = lease.connection().execute("SELECT pg_backend_pid()::text");
            ASSERT_TRUE(result.has_value());
            deadPid = std::get<std::string>((*result)[0][0]);
        } // returned to the pool, still alive at this point

        {
            // A second, unpooled connection to kill the pooled one from outside.
            QcNativeConnection admin(testParams());
            auto killed = admin.execute("SELECT pg_terminate_backend($1)", {deadPid});
            ASSERT_TRUE(killed.has_value());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Must transparently reconnect instead of handing back the dead connection.
        auto lease = pool.acquire();
        auto result = lease.connection().execute("SELECT pg_backend_pid()::text");
        ASSERT_TRUE(result.has_value());
        EXPECT_NE(std::get<std::string>((*result)[0][0]), deadPid);
    } else {
        // MySQL -- KILL <connection id> is MySQL's equivalent of
        // pg_terminate_backend(), and (verified directly) a user may KILL
        // their own other connections without any extra privilege
        // (SUPER/CONNECTION_ADMIN is only needed to kill a *different*
        // user's connection).
        long long deadId = 0;
        {
            auto lease = pool.acquire();
            auto result = lease.connection().execute("SELECT CONNECTION_ID()");
            ASSERT_TRUE(result.has_value());
            deadId = std::get<long long>((*result)[0][0]);
        } // returned to the pool, still alive at this point

        {
            // A second, unpooled connection to kill the pooled one from outside.
            QcNativeConnection admin(testParams());
            auto killed = admin.execute("KILL " + std::to_string(deadId));
            ASSERT_TRUE(killed.has_value());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Must transparently reconnect instead of handing back the dead connection.
        auto lease = pool.acquire();
        auto result = lease.connection().execute("SELECT CONNECTION_ID()");
        ASSERT_TRUE(result.has_value());
        EXPECT_NE(std::get<long long>((*result)[0][0]), deadId);
    }
}

TEST_F(QcConnectionPoolTest, OnDemandModeOpensDistinctConcurrentConnections)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::OnDemand, 2);

    auto first = pool.acquire();
    auto second = pool.acquire();

    EXPECT_TRUE(first.connection().execute(selectOne()).has_value());
    EXPECT_TRUE(second.connection().execute(selectOne()).has_value());
    EXPECT_NE(first.connection().nativeHandle(), second.connection().nativeHandle());
}

TEST_F(QcConnectionPoolTest, OnDemandModeCapsConcurrentConnections)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::OnDemand, 1);

    std::promise<void> acquiring;
    std::atomic<bool> acquired{false};
    std::thread waiter;

    {
        auto lease = pool.acquire();

        waiter = std::thread([&] {
            acquiring.set_value();
            auto second = pool.acquire();
            acquired = true;
        });

        acquiring.get_future().wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        EXPECT_FALSE(acquired.load());
    }

    waiter.join();
    EXPECT_TRUE(acquired.load());
}

TEST_F(QcConnectionPoolTest, PermanentModeIsThreadSafeUnderConcurrentUse)
{
    constexpr int poolSize = 4;
    constexpr int threadCount = 16;
    constexpr int iterationsPerThread = 20;

    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, poolSize);
    std::atomic<int> successCount{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < iterationsPerThread; ++j) {
                auto lease = pool.acquire();
                if (lease.connection().execute(selectOne()).has_value()) {
                    ++successCount;
                }
            }
        });
    }
    for (auto & t : threads) {
        t.join();
    }

    EXPECT_EQ(successCount.load(), threadCount * iterationsPerThread);
}

TEST_F(QcConnectionPoolTest, OnDemandModeIsThreadSafeUnderConcurrentUse)
{
    constexpr int poolSize = 4;
    constexpr int threadCount = 8;
    constexpr int iterationsPerThread = 5;

    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::OnDemand, poolSize);
    std::atomic<int> successCount{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < iterationsPerThread; ++j) {
                auto lease = pool.acquire();
                if (lease.connection().execute(selectOne()).has_value()) {
                    ++successCount;
                }
            }
        });
    }
    for (auto & t : threads) {
        t.join();
    }

    EXPECT_EQ(successCount.load(), threadCount * iterationsPerThread);
}

TEST_F(QcConnectionPoolTest, ExecuteBindsParametersAndReturnsRows)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 1);
    auto lease = pool.acquire();

    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL: {
            auto result = lease.connection().execute(
                "SELECT ($1::int + $2::int)::text, $3::text",
                {2LL, 3LL, std::string("hello")});

            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->size(), 1u);
            ASSERT_EQ((*result)[0].size(), 2u);
            EXPECT_EQ(std::get<std::string>((*result)[0][0]), "5");
            EXPECT_EQ(std::get<std::string>((*result)[0][1]), "hello");
            break;
        }
        case QcDbDriver::SQLite: {
            // SQLite hands back typed values natively (see qcnativeconnection.h) --
            // the arithmetic result comes back as long long, not text.
            auto result = lease.connection().execute(
                "SELECT (? + ?), ?",
                {2LL, 3LL, std::string("hello")});

            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->size(), 1u);
            ASSERT_EQ((*result)[0].size(), 2u);
            EXPECT_EQ(std::get<long long>((*result)[0][0]), 5);
            EXPECT_EQ(std::get<std::string>((*result)[0][1]), "hello");
            break;
        }
        case QcDbDriver::Oracle: {
            // Oracle -- like PostgreSQL -- has no column-type metadata plumbed
            // through yet (see qcnativeconnection.h), so every non-LOB cell comes
            // back as text regardless of the underlying NUMBER/VARCHAR2 type.
            auto result = lease.connection().execute(
                "SELECT TO_CHAR(:1 + :2), :3 FROM DUAL",
                {2LL, 3LL, std::string("hello")});

            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->size(), 1u);
            ASSERT_EQ((*result)[0].size(), 2u);
            EXPECT_EQ(std::get<std::string>((*result)[0][0]), "5");
            EXPECT_EQ(std::get<std::string>((*result)[0][1]), "hello");
            break;
        }
        case QcDbDriver::MSSQL: {
            // Like SQLite -- unlike PostgreSQL/Oracle above -- real column-type
            // metadata (SQLDescribeCol, see qcdrivermssql.cpp) drives how each
            // cell comes back, so the arithmetic result is long long, not text.
            // FROM DUAL isn't needed -- T-SQL allows a bare "SELECT expr, ...".
            auto result = lease.connection().execute(
                "SELECT (? + ?), ?",
                {2LL, 3LL, std::string("hello")});

            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->size(), 1u);
            ASSERT_EQ((*result)[0].size(), 2u);
            EXPECT_EQ(std::get<long long>((*result)[0][0]), 5);
            EXPECT_EQ(std::get<std::string>((*result)[0][1]), "hello");
            break;
        }
        case QcDbDriver::MySQL: {
            // Real column-type metadata drives how each cell comes back (see
            // categorize() in qcdrivermysql.cpp), like SQLite/MSSQL above --
            // but unlike them, the arithmetic result comes back as double, not long
            // long: MySQL's prepare-time metadata for "(? + ?)" reports it as
            // MYSQL_TYPE_DOUBLE (verified directly -- with both operands
            // placeholders of unresolved type, MySQL can't yet know they'll be
            // bound as integers, so it falls back to a generic numeric type rather
            // than inferring long long the way SQLite's dynamic typing or MSSQL's
            // "? + ?" resolution do).
            auto result = lease.connection().execute(
                "SELECT (? + ?), ?",
                {2LL, 3LL, std::string("hello")});

            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->size(), 1u);
            ASSERT_EQ((*result)[0].size(), 2u);
            EXPECT_DOUBLE_EQ(std::get<double>((*result)[0][0]), 5.0);
            EXPECT_EQ(std::get<std::string>((*result)[0][1]), "hello");
            break;
        }
    }
}

TEST_F(QcConnectionPoolTest, ExecuteRoundTripsNullParameters)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 1);
    auto lease = pool.acquire();

    std::optional<QcResultSet> result;
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            result = lease.connection().execute("SELECT $1::text", {QcSqlBase::QcVariant{}});
            break;
        case QcDbDriver::Oracle:
            result = lease.connection().execute("SELECT :1 FROM DUAL", {QcSqlBase::QcVariant{}});
            break;
        case QcDbDriver::MySQL:
        case QcDbDriver::SQLite:
        case QcDbDriver::MSSQL:
            result = lease.connection().execute("SELECT ?", {QcSqlBase::QcVariant{}});
            break;
    }

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_TRUE(std::holds_alternative<std::monostate>((*result)[0][0]));
}

TEST_F(QcConnectionPoolTest, ExecuteReturnsNulloptOnInvalidSql)
{
    QcConnectionPool pool(testParams(), QcConnectionPool::Mode::Permanent, 1);
    auto lease = pool.acquire();

    EXPECT_FALSE(lease.connection().execute("THIS IS NOT SQL").has_value());
}
