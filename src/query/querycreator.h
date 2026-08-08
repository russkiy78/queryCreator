#ifndef QUERYCREATOR_H
#define QUERYCREATOR_H

#include <cstddef>
#include <optional>
#include <string>

#include "qcconnectionparams.h"
#include "qcconnectionpool.h"
#include "qcnativeconnection.h"
#include "qcsqlbase.h"

class QcSqlQuery;
class QcSqlInsert;
class QcSqlUpdate;
class QcSqlDelete;

// Facade tying the query builder (QcSqlQuery/QcSqlInsert/QcSqlUpdate/
// QcSqlDelete's toSql()) to a connection pool (QcConnectionPool::execute())
// in a single call, replacing the two manual steps every caller previously
// had to write out themselves:
//   const QcSqlStatement stmt = query.toSql();
//   pool.acquire().connection().execute(stmt.sql, stmt.params);
// Owns its pool (constructed from `params` at QueryCreator construction
// time) rather than borrowing one -- the point of the facade is "set up
// once, then one call per query", not another way to plumb an
// externally-owned pool through.
class QueryCreator
{
public:
    explicit QueryCreator(const QcConnectionParams & params,
                           QcConnectionPool::Mode mode = QcConnectionPool::Mode::Permanent,
                           std::size_t poolSize = 1);

    std::optional<QcResultSet> execute(const QcSqlQuery & query);
    std::optional<QcResultSet> execute(const QcSqlInsert & insert);
    std::optional<QcResultSet> execute(const QcSqlUpdate & update);
    std::optional<QcResultSet> execute(const QcSqlDelete & del);

    // Escape hatch for raw SQL text the builders above don't express (DDL,
    // BEGIN/COMMIT/ROLLBACK, computed-expression UPDATEs like
    // "SET metadata = jsonb_set(metadata, ...)", ...) -- same (sql, params)
    // contract as QcNativeConnection::execute().
    std::optional<QcResultSet> execute(const std::string & sql, const QcSqlBase::QcVariantList & params = {});

    // Named counterparts of the six execute() overloads above -- same
    // builder-in/pool-out contract, rows come back as QcNamedResultSet
    // (column name -> value) instead of positional QcResultSet. See
    // QcNativeConnection::executeNamed()/executeReturningNamed() for where
    // the column names actually come from per driver.
    std::optional<QcNamedResultSet> executeNamed(const QcSqlQuery & query);
    std::optional<QcNamedResultSet> executeNamed(const QcSqlInsert & insert);
    std::optional<QcNamedResultSet> executeNamed(const QcSqlUpdate & update);
    std::optional<QcNamedResultSet> executeNamed(const QcSqlDelete & del);
    std::optional<QcNamedResultSet> executeNamed(const std::string & sql, const QcSqlBase::QcVariantList & params = {});

private:
    QcConnectionPool m_pool;
    // Copied from the QcConnectionParams this was constructed with -- lets
    // every execute()/executeNamed() overload above render its SQL for the
    // right dialect (see toSql(QcDbDriver) on each builder class) without
    // callers having to pass it themselves.
    QcDbDriver m_driver;

    // Shared by the QcSqlInsert/Update/Delete overloads above: renders
    // `statement.sql`/`statement.params` through the connection this lease
    // holds, using executeReturning() rather than plain execute() so
    // Oracle's RETURNING...INTO OUT binds (statement.returningColumnCount)
    // are handled correctly -- a no-op distinction on every other driver,
    // see QcNativeConnection::executeReturning()'s doc comment.
    std::optional<QcResultSet> executeStatement(const QcSqlStatement & statement);
    // Named counterpart of executeStatement() above -- mirrors it exactly,
    // through executeReturningNamed() instead of executeReturning().
    std::optional<QcNamedResultSet> executeStatementNamed(const QcSqlStatement & statement);
};

#endif // QUERYCREATOR_H
