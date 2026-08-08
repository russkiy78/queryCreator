#ifndef QCNATIVECONNECTION_H
#define QCNATIVECONNECTION_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "qcconnectionparams.h"
#include "qcdbdriver.h"
#include "qcsqlbase.h"

class IQcDriverConnection;

using QcResultRow = QcSqlBase::QcVariantList;
using QcResultSet = std::vector<QcResultRow>;

// Named counterpart of QcResultRow/QcResultSet -- each row as column name ->
// value instead of a positional vector, for callers that would rather look
// up "id"/"name" than remember column order. Additive, parallel API: see
// executeNamed()/executeReturningNamed() below, which populate this from
// exactly the same underlying rows execute()/executeReturning() produce,
// just paired with column names read from driver metadata (or, on Oracle's
// RETURNING...INTO OUT-bind path, from QcSqlStatement::returningColumnNames
// -- see executeReturningNamed()'s doc comment). std::map (not
// unordered_map): result sets here are always small enough (a handful of
// columns) that ordering for iteration/comparison in tests outweighs any
// hash-map lookup speed advantage.
//
// Duplicate column names (self-join, ambiguous JOIN without aliases) are not
// an error -- the map only keeps the last column with that name, silently.
// This is a deliberate, documented limitation, not a bug: the same principle
// already applies to quoteRef()/addFreeText elsewhere in this project --
// disambiguate yourself (alias the columns) if you need every one of them.
using QcNamedRow = std::map<std::string, QcSqlBase::QcVariant>;
using QcNamedResultSet = std::vector<QcNamedRow>;

// One physical connection to the database, opened through whichever native
// client library QcConnectionParams::driver selects at runtime -- must have
// been compiled into this build (QC_DB_DRIVERS, see CMakeLists.txt) or the
// constructor throws. Not thread-safe by itself — a single connection must
// only ever be used by one thread at a time; QcConnectionPool is what makes
// that guarantee hold.
class QcNativeConnection
{
public:
    explicit QcNativeConnection(const QcConnectionParams & params);
    ~QcNativeConnection();

    QcNativeConnection(const QcNativeConnection &) = delete;
    QcNativeConnection & operator=(const QcNativeConnection &) = delete;

    bool isOpen() const;

    // Actively probes the connection with a round-trip — there's no cheaper
    // way to know a peer-closed socket is dead before trying to use it (a
    // purely local status read doesn't notice a server-killed connection
    // until something actually attempts I/O on it). Used by QcConnectionPool
    // to transparently replace dead connections on acquire().
    bool isAlive();

    // Opaque handle to the underlying driver connection (PGconn* etc.) —
    // exposed for identity checks (tests) until QcSqlQuery gains a way to
    // execute itself through this connection instead of raw SQL text.
    void * nativeHandle() const;

    // `sql` may contain native positional placeholders ($1, $2, ... for
    // PostgreSQL; plain "?" for SQLite) bound left-to-right from `params`.
    // Returns the result rows for statements that produce them (SELECT,
    // RETURNING, ...), an empty set for statements that don't
    // (INSERT/UPDATE/DDL/...), or nullopt if the statement failed.
    // Column value typing differs by driver: PostgreSQL's text wire protocol
    // hands every non-NULL cell back as the raw text the driver returned it
    // as (std::string) — there's no column-type metadata plumbed through yet
    // to decide long long/double instead. SQLite's C API gives typed values
    // for free (no wire-protocol stringification step to route around), so
    // its cells come back as long long/double/std::string/std::vector<std::byte>
    // matching each column's actual SQLite storage class.
    std::optional<QcResultSet> execute(const std::string & sql, const QcSqlBase::QcVariantList & params = {});

    // Same contract as execute() above, plus support for a driver-specific
    // "RETURNING extra columns" clause QcSqlInsert/Update/Delete::returning()
    // may have appended (see QcSqlDialect::returningClause() and
    // QcSqlStatement::returningColumnCount). On every driver except Oracle,
    // that clause already rides back as an ordinary result row through plain
    // execute() -- this is exactly equivalent to execute(sql, params) there,
    // and `returningColumnCount` is ignored. On Oracle, `sql` ends with
    // `returningColumnCount` trailing "RETURNING ... INTO :N, :N+1, ..."
    // OUT-bind placeholders, immediately after the ordinary `params.size()`
    // IN placeholders -- those are bound as OUT parameters (not read from
    // `params`, which only supplies the IN values) and read back into a
    // single result row after the statement executes, since Oracle's
    // RETURNING INTO uses OUT binds rather than an ordinary result set.
    std::optional<QcResultSet> executeReturning(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                                 std::size_t returningColumnCount);

    // Named counterpart of execute() above -- identical SQL/params contract,
    // rows come back as QcNamedRow (column name -> value) instead of a
    // positional QcResultRow. Column names come from ordinary driver result-set
    // metadata (PostgreSQL: PQfname; SQLite: sqlite3_column_name; MSSQL:
    // SQLDescribeCol; MySQL: MYSQL_FIELD::name; Oracle: OCI_ATTR_NAME) --
    // exactly the alias a SELECT list gave the column ("col <alias>"
    // rendering, see QcSqlDialect) if one was given, the plain column name
    // otherwise. Purely additive: execute() is untouched and keeps returning
    // positional rows, this is a second, parallel way to fetch the same data.
    std::optional<QcNamedResultSet> executeNamed(const std::string & sql, const QcSqlBase::QcVariantList & params = {});

    // Named counterpart of executeReturning() above -- same (sql, params,
    // returningColumnCount) contract, plus `returningColumnNames` (in the
    // same order as the `returningColumnCount` trailing OUT-bind placeholders
    // on Oracle) since Oracle's RETURNING...INTO OUT-bind path has no
    // result-set metadata of its own to read column names from the way an
    // ordinary SELECT/RETURNING result set does -- see
    // QcSqlStatement::returningColumnNames. On every other driver,
    // `returningColumnNames` is ignored (RETURNING/OUTPUT already rides back
    // as an ordinary result set there, so column names come from driver
    // metadata like executeNamed() above) and this is exactly equivalent to
    // executeNamed(sql, params).
    std::optional<QcNamedResultSet> executeReturningNamed(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                                           std::size_t returningColumnCount,
                                                           const QcSqlBase::QcStringList & returningColumnNames);

    // Identifies the native client library this build is linked against for
    // `driver` (e.g. "PostgreSQL (libpq) client version 180004") — proves
    // that driver actually links, not just configures. Returns a
    // placeholder string (not a throw -- no connection is opened here) if
    // `driver` wasn't compiled into this build at all.
    static std::string nativeDriverInfo(QcDbDriver driver = QcDbDriver::PostgreSQL);

private:
    std::unique_ptr<IQcDriverConnection> m_impl;
};

#endif // QCNATIVECONNECTION_H
