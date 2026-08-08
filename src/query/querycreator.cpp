#include "querycreator.h"

#include "qcsqldelete.h"
#include "qcsqlinsert.h"
#include "qcsqlquery.h"
#include "qcsqlupdate.h"

namespace {
// Common prefix shared by the positional and named MySQL INSERT...RETURNING
// emulation below (executeMysqlInsertReturning()/executeMysqlInsertReturningNamed()):
// BEGIN, run the insert, then read back LAST_INSERT_ID() -- everything up to
// but not including the follow-up SELECT, which is the one step that
// actually differs (positional execute() vs. named executeNamed()). On any
// failure this has already issued ROLLBACK (except when BEGIN itself never
// started a transaction to roll back) and returns nullopt; on success it
// returns the id value ready to bind into the follow-up SELECT, leaving that
// SELECT and the final COMMIT/ROLLBACK to the caller.
//
// mysql_insert_id() (a client-side call needing no round trip) was
// considered instead of this SELECT -- rejected because it would need a new
// QcNativeConnection method exposing a MySQL-only C API detail, where this
// stays entirely inside the existing execute(sql, params) contract every
// other driver already uses. LAST_INSERT_ID() comes back through this
// project's typed MySQL fetch path as an ordinary long long, safe to rebind
// directly below.
//
// NOT safe to call while the caller already has its own explicit transaction
// open on this same leased connection: MySQL has no nested transactions, so
// the "BEGIN" below would implicitly commit whatever the caller had open
// first -- a documented limitation (see docs/architecture.md), not something
// this function detects or guards against.
std::optional<QcSqlBase::QcVariant> beginInsertAndFetchLastInsertId(QcNativeConnection & conn, const QcSqlStatement & statement)
{
    if (!conn.execute("BEGIN")) {
        return std::nullopt;
    }
    if (!conn.execute(statement.sql, statement.params)) {
        conn.execute("ROLLBACK");
        return std::nullopt;
    }
    const auto idResult = conn.execute("SELECT LAST_INSERT_ID()");
    if (!idResult || idResult->size() != 1 || (*idResult)[0].empty()) {
        conn.execute("ROLLBACK");
        return std::nullopt;
    }
    return (*idResult)[0][0];
}

std::optional<QcResultSet> executeMysqlInsertReturning(QcNativeConnection & conn, const QcSqlStatement & statement)
{
    const auto id = beginInsertAndFetchLastInsertId(conn, statement);
    if (!id) {
        return std::nullopt;
    }
    auto selectResult = conn.execute(statement.mysqlReturningSelectSql, {*id});
    if (!selectResult) {
        conn.execute("ROLLBACK");
        return std::nullopt;
    }
    if (!conn.execute("COMMIT")) {
        return std::nullopt;
    }
    return selectResult;
}

// Named counterpart of executeMysqlInsertReturning() above -- identical
// BEGIN/insert/LAST_INSERT_ID() prefix (shared via
// beginInsertAndFetchLastInsertId()), only the follow-up SELECT differs
// (executeNamed() instead of execute()), so the result comes back keyed by
// column name instead of positionally.
std::optional<QcNamedResultSet> executeMysqlInsertReturningNamed(QcNativeConnection & conn, const QcSqlStatement & statement)
{
    const auto id = beginInsertAndFetchLastInsertId(conn, statement);
    if (!id) {
        return std::nullopt;
    }
    auto selectResult = conn.executeNamed(statement.mysqlReturningSelectSql, {*id});
    if (!selectResult) {
        conn.execute("ROLLBACK");
        return std::nullopt;
    }
    if (!conn.execute("COMMIT")) {
        return std::nullopt;
    }
    return selectResult;
}
} // namespace

QueryCreator::QueryCreator(const QcConnectionParams & params, QcConnectionPool::Mode mode, std::size_t poolSize)
    : m_pool(params, mode, poolSize)
    , m_driver(params.driver)
{
}

std::optional<QcResultSet> QueryCreator::executeStatement(const QcSqlStatement & statement)
{
    return m_pool.acquire().connection().executeReturning(statement.sql, statement.params, statement.returningColumnCount);
}

std::optional<QcResultSet> QueryCreator::execute(const QcSqlQuery & query)
{
    const QcSqlStatement statement = query.toSql(m_driver);
    return execute(statement.sql, statement.params);
}

std::optional<QcResultSet> QueryCreator::execute(const QcSqlInsert & insert)
{
    const QcSqlStatement statement = insert.toSql(m_driver);
    if (!statement.mysqlReturningSelectSql.empty()) {
        auto lease = m_pool.acquire();
        return executeMysqlInsertReturning(lease.connection(), statement);
    }
    return executeStatement(statement);
}

std::optional<QcResultSet> QueryCreator::execute(const QcSqlUpdate & update)
{
    const QcSqlStatement statement = update.toSql(m_driver);
    return executeStatement(statement);
}

std::optional<QcResultSet> QueryCreator::execute(const QcSqlDelete & del)
{
    const QcSqlStatement statement = del.toSql(m_driver);
    return executeStatement(statement);
}

std::optional<QcResultSet> QueryCreator::execute(const std::string & sql, const QcSqlBase::QcVariantList & params)
{
    return m_pool.acquire().connection().execute(sql, params);
}

std::optional<QcNamedResultSet> QueryCreator::executeStatementNamed(const QcSqlStatement & statement)
{
    return m_pool.acquire().connection().executeReturningNamed(statement.sql, statement.params, statement.returningColumnCount,
                                                                 statement.returningColumnNames);
}

std::optional<QcNamedResultSet> QueryCreator::executeNamed(const QcSqlQuery & query)
{
    const QcSqlStatement statement = query.toSql(m_driver);
    return executeNamed(statement.sql, statement.params);
}

std::optional<QcNamedResultSet> QueryCreator::executeNamed(const QcSqlInsert & insert)
{
    const QcSqlStatement statement = insert.toSql(m_driver);
    if (!statement.mysqlReturningSelectSql.empty()) {
        auto lease = m_pool.acquire();
        return executeMysqlInsertReturningNamed(lease.connection(), statement);
    }
    return executeStatementNamed(statement);
}

std::optional<QcNamedResultSet> QueryCreator::executeNamed(const QcSqlUpdate & update)
{
    const QcSqlStatement statement = update.toSql(m_driver);
    return executeStatementNamed(statement);
}

std::optional<QcNamedResultSet> QueryCreator::executeNamed(const QcSqlDelete & del)
{
    const QcSqlStatement statement = del.toSql(m_driver);
    return executeStatementNamed(statement);
}

std::optional<QcNamedResultSet> QueryCreator::executeNamed(const std::string & sql, const QcSqlBase::QcVariantList & params)
{
    return m_pool.acquire().connection().executeNamed(sql, params);
}
