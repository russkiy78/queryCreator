#ifndef QCDRIVERCONNECTION_H
#define QCDRIVERCONNECTION_H

#include <optional>
#include <string>

#include "qcnativeconnection.h"
#include "qcsqlbase.h"

// Pairs positional rows (exactly what execute()/executeReturning() already
// produce) with a parallel list of column names -- shared by every driver
// backend's default executeReturningNamed() below and by
// QcNativeConnection::executeNamed() itself. Implements the
// duplicate-column-name caveat documented on QcNamedRow in
// qcnativeconnection.h: std::map::operator[] here keeps only the last write
// for a repeated key, silently -- not an error, alias the columns yourself
// if you need every one of them.
QcNamedResultSet toNamedResultSet(const QcSqlBase::QcStringList & columnNames, const QcResultSet & rows);

// One open native connection to a specific driver, behind a uniform
// interface -- implemented once per driver (QcPostgreSqlConnection,
// QcOracleConnection, QcMySqlConnection, QcSqliteConnection,
// QcMssqlConnection; see qcdriver*.cpp), constructed through the matching
// create*Connection() factory function in qcdriverfactory.h.
// QcNativeConnection itself is just a thin owner of one of these, chosen at
// runtime by QcConnectionParams::driver -- see qcnativeconnection.cpp.
class IQcDriverConnection
{
public:
    virtual ~IQcDriverConnection() = default;

    // Actively probes the connection with a round-trip -- see
    // QcNativeConnection::isAlive()'s doc comment for why a purely local
    // status read isn't enough.
    virtual bool isAlive() = 0;

    // Opaque handle to the underlying driver connection (PGconn* etc.) --
    // see QcNativeConnection::nativeHandle()'s doc comment.
    virtual void * nativeHandle() const = 0;

    // Same (sql, params, outColumnNames) contract as
    // QcNativeConnection::execute()/executeNamed() -- outColumnNames, when
    // non-null, is filled with the result set's column names (ignored by
    // callers that only want positional rows).
    virtual std::optional<QcResultSet> execute(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                                QcSqlBase::QcStringList * outColumnNames = nullptr) = 0;

    // Default: identical to execute() (ignores returningColumnCount) --
    // matches every driver except Oracle, whose RETURNING...INTO OUT-bind
    // path (see qcdriveroracle.cpp) overrides this.
    virtual std::optional<QcResultSet> executeReturning(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                                         std::size_t returningColumnCount);

    // Default: executeNamed()'s ordinary column-name-from-metadata path,
    // ignoring returningColumnCount/returningColumnNames -- overridden by
    // Oracle for the same reason as executeReturning() above.
    virtual std::optional<QcNamedResultSet> executeReturningNamed(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                                                   std::size_t returningColumnCount,
                                                                   const QcSqlBase::QcStringList & returningColumnNames);
};

#endif // QCDRIVERCONNECTION_H
