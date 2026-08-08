#ifndef TESTDBCONFIG_H
#define TESTDBCONFIG_H

#include <optional>
#include <string>
#include <vector>

#include "query/qcconnectionparams.h"
#include "query/qcdbdriver.h"

// Loads connection parameters for `section` (host/port/database/user/
// password/connect_timeout_seconds) from tests/db_config.ini -- see
// db_config.ini.example for the file format. Section names are the lowercase
// driver names used there ("postgresql"/"sqlite"/"mysql"/"mssql"/"oracle").
// Returns nullopt if the file doesn't exist or has no such section, so
// callers can fall back to a hardcoded default rather than fail outright.
std::optional<QcConnectionParams> loadTestDbConfig(const std::string & section);

// Every driver compiled into this test binary (QC_DB_HAS_POSTGRESQL/
// QC_DB_HAS_ORACLE/... -- see QC_DB_DRIVERS in CMakeLists.txt), in the same
// fixed order primaryTestDriver() below prioritizes them. Empty only if the
// build is somehow misconfigured (QC_DB_DRIVERS should always select at
// least one driver).
std::vector<QcDbDriver> compiledInDrivers();

// The one driver most of this suite's single-driver-focused tests (pool
// mechanics, connection-pool edge cases, ...) run against -- the first of
// compiledInDrivers() in priority order (PostgreSQL, Oracle, MySQL, SQLite,
// MSSQL), matching this project's long-standing "PostgreSQL is the
// day-to-day default" convention (see CMakeLists.txt) for any build that
// includes it, and falling through predictably otherwise. Multi-driver
// coverage itself (proving more than one driver really works from the same
// binary) lives in the tests that loop over compiledInDrivers() directly
// (see test_qcsqldialect.cpp and the CompiledInDrivers* tests in
// test_qcconnectionpool.cpp) rather than here.
QcDbDriver primaryTestDriver();

// loadTestDbConfig() for `driver`'s db_config.ini section, falling back to
// the project's long-standing local-dev defaults (demo/demo/demo@
// 127.0.0.1:5432 for PostgreSQL, a shared in-memory database for SQLite,
// ...) when db_config.ini doesn't provide one -- so the suite still runs out
// of the box before anyone has copied db_config.ini.example. Also sets
// QcConnectionParams::driver to `driver`, whether or not a db_config.ini
// section was found.
QcConnectionParams testDbConfigOrDefault(QcDbDriver driver);

// testDbConfigOrDefault(primaryTestDriver()) -- the common case for tests
// that only care about "some reachable, compiled-in driver", not a specific
// one.
QcConnectionParams testDbConfigOrDefault();

#endif // TESTDBCONFIG_H
