#ifndef QCDRIVERFACTORY_H
#define QCDRIVERFACTORY_H

#include <memory>
#include <string>

#include "qcconnectionparams.h"
#include "qcdriverconnection.h"

// One factory function + one nativeDriverInfo() function per driver, each
// only declared (and only ever defined, in that driver's own qcdriver*.cpp)
// when that driver was selected via QC_DB_DRIVERS -- see CMakeLists.txt.
// create*Connection() throws std::runtime_error on connection failure,
// exactly like the pre-Bridge-split openConnection() helpers each used to.
// QcNativeConnection's constructor/nativeDriverInfo() (qcnativeconnection.cpp)
// is the only caller, dispatching on QcConnectionParams::driver /
// QcDbDriver behind the matching #ifdef.

#ifdef QC_DB_HAS_POSTGRESQL
std::unique_ptr<IQcDriverConnection> createPostgreSqlConnection(const QcConnectionParams & params);
std::string postgreSqlDriverInfo();
#endif

#ifdef QC_DB_HAS_ORACLE
std::unique_ptr<IQcDriverConnection> createOracleConnection(const QcConnectionParams & params);
std::string oracleDriverInfo();
#endif

#ifdef QC_DB_HAS_MYSQL
std::unique_ptr<IQcDriverConnection> createMySqlConnection(const QcConnectionParams & params);
std::string mySqlDriverInfo();
#endif

#ifdef QC_DB_HAS_SQLITE
std::unique_ptr<IQcDriverConnection> createSqliteConnection(const QcConnectionParams & params);
std::string sqliteDriverInfo();
#endif

#ifdef QC_DB_HAS_MSSQL
std::unique_ptr<IQcDriverConnection> createMssqlConnection(const QcConnectionParams & params);
std::string mssqlDriverInfo();
#endif

#endif // QCDRIVERFACTORY_H
