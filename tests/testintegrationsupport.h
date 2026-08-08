#ifndef TESTINTEGRATIONSUPPORT_H
#define TESTINTEGRATIONSUPPORT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "query/qcsqlbase.h"

// Small helpers shared by the integration ("battle") test suites
// (test_integration_select.cpp, test_integration_dml.cpp) that talk to a
// real database through QcNativeConnection/QcConnectionPool.

// Reads a numeric cell regardless of how the active driver represents it:
// PostgreSQL's text wire protocol returns every non-NULL cell as a
// std::string (see qcnativeconnection.h), while SQLite hands back typed
// long long/double values natively -- these normalize either shape to a
// single C++ type so assertions don't need to branch on QC_DB_*.
double asDouble(const QcSqlBase::QcVariant & value);
long long asInt64(const QcSqlBase::QcVariant & value);
// PostgreSQL's boolean text output is "t"/"f"; SQLite has no boolean storage
// class and represents it as the integer 0/1 (see the schema in
// test_integration_select.cpp) -- both come back through here as bool.
bool asBool(const QcSqlBase::QcVariant & value);

// Reads a BLOB/bytea cell as raw bytes regardless of driver representation:
// SQLite returns std::vector<std::byte> natively; PostgreSQL's text wire
// protocol returns bytea as a hex-encoded std::string ("\x..." -- see
// toParamText() in qcnativeconnection.cpp), which this decodes.
std::vector<std::byte> asBytes(const QcSqlBase::QcVariant & value);

// Builds "$1, $2, $3" (PostgreSQL) or "?, ?, ?" (SQLite/...) for `count`
// consecutive placeholders starting at 1-based position `startIndex` -- for
// composing raw INSERT/UPDATE SQL text outside the QcSqlQuery builder (which
// has no INSERT/UPDATE/DELETE support, see qcsqlquery.h) using the same
// per-driver placeholder syntax QcSqlDialect uses for builder-generated SQL.
// Renders for primaryTestDriver() (see testdbconfig.h) -- the driver the
// calling suite's live connection actually talks to.
std::string placeholderList(std::size_t count, std::size_t startIndex = 1);

// Deterministic (seed-reproducible) filler content for "huge data" tests --
// plain ASCII words repeated/truncated to exactly `targetBytes`, and
// uniformly random bytes of exactly `count` length (every byte value,
// including 0x00 and 0xFF, appears across a large enough buffer).
std::string generateRandomText(std::size_t targetBytes, std::uint64_t seed);
std::vector<std::byte> generateRandomBytes(std::size_t count, std::uint64_t seed);

#endif // TESTINTEGRATIONSUPPORT_H
