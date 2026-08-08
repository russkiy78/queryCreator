#include "qcnativeconnection.h"

#include <stdexcept>

#include "qcdriverconnection.h"
#include "qcdriverfactory.h"

namespace {

std::unique_ptr<IQcDriverConnection> createDriverConnection(const QcConnectionParams & params)
{
    switch (params.driver) {
        case QcDbDriver::PostgreSQL:
#ifdef QC_DB_HAS_POSTGRESQL
            return createPostgreSqlConnection(params);
#else
            break;
#endif
        case QcDbDriver::Oracle:
#ifdef QC_DB_HAS_ORACLE
            return createOracleConnection(params);
#else
            break;
#endif
        case QcDbDriver::MySQL:
#ifdef QC_DB_HAS_MYSQL
            return createMySqlConnection(params);
#else
            break;
#endif
        case QcDbDriver::SQLite:
#ifdef QC_DB_HAS_SQLITE
            return createSqliteConnection(params);
#else
            break;
#endif
        case QcDbDriver::MSSQL:
#ifdef QC_DB_HAS_MSSQL
            return createMssqlConnection(params);
#else
            break;
#endif
    }
    throw std::runtime_error("QcNativeConnection: requested driver was not compiled into this build "
                              "(see QC_DB_DRIVERS in CMakeLists.txt)");
}

} // namespace

QcNativeConnection::QcNativeConnection(const QcConnectionParams & params)
    : m_impl(createDriverConnection(params))
{
}

QcNativeConnection::~QcNativeConnection() = default;

bool QcNativeConnection::isOpen() const
{
    return m_impl != nullptr;
}

bool QcNativeConnection::isAlive()
{
    return m_impl->isAlive();
}

void * QcNativeConnection::nativeHandle() const
{
    return m_impl->nativeHandle();
}

std::optional<QcResultSet> QcNativeConnection::execute(const std::string & sql, const QcSqlBase::QcVariantList & params)
{
    return m_impl->execute(sql, params);
}

std::optional<QcResultSet> QcNativeConnection::executeReturning(const std::string & sql,
                                                                  const QcSqlBase::QcVariantList & params,
                                                                  std::size_t returningColumnCount)
{
    return m_impl->executeReturning(sql, params, returningColumnCount);
}

std::optional<QcNamedResultSet> QcNativeConnection::executeNamed(const std::string & sql, const QcSqlBase::QcVariantList & params)
{
    QcSqlBase::QcStringList columnNames;
    auto rows = m_impl->execute(sql, params, &columnNames);
    if (!rows) {
        return std::nullopt;
    }
    return toNamedResultSet(columnNames, *rows);
}

std::optional<QcNamedResultSet> QcNativeConnection::executeReturningNamed(const std::string & sql,
                                                                           const QcSqlBase::QcVariantList & params,
                                                                           std::size_t returningColumnCount,
                                                                           const QcSqlBase::QcStringList & returningColumnNames)
{
    return m_impl->executeReturningNamed(sql, params, returningColumnCount, returningColumnNames);
}

std::string QcNativeConnection::nativeDriverInfo(QcDbDriver driver)
{
    switch (driver) {
        case QcDbDriver::PostgreSQL:
#ifdef QC_DB_HAS_POSTGRESQL
            return postgreSqlDriverInfo();
#else
            break;
#endif
        case QcDbDriver::Oracle:
#ifdef QC_DB_HAS_ORACLE
            return oracleDriverInfo();
#else
            break;
#endif
        case QcDbDriver::MySQL:
#ifdef QC_DB_HAS_MYSQL
            return mySqlDriverInfo();
#else
            break;
#endif
        case QcDbDriver::SQLite:
#ifdef QC_DB_HAS_SQLITE
            return sqliteDriverInfo();
#else
            break;
#endif
        case QcDbDriver::MSSQL:
#ifdef QC_DB_HAS_MSSQL
            return mssqlDriverInfo();
#else
            break;
#endif
    }
    return "(driver not compiled into this build)";
}
