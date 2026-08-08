<div align="center"><img src="logo/logo-big-black.png" alt="queryCreator logo" width="400"></div>

# queryCreator

A lightweight SQL query-builder library for pure C++20, with no external
dependencies in the library itself. Queries are built through fluent method
chains (`.where(...).isEqualTo(...)`, `.addReturnValue(...).upperCase()`)
instead of writing raw SQL strings by hand; the finished query can be
executed immediately through one of five native DB drivers.

## Example

```cpp
#include "query/querycreator.h"
#include "query/qcsqlquery.h"

QcConnectionParams params;
params.host = "127.0.0.1";
params.port = "5432";
params.database = "demo";
params.user = "demo";
params.password = "demo";

QueryCreator qc(params); // owns a connection pool

QcSqlQuery query;
query.fromTable("users");
query.addReturnValues({"id", "name <display_name>"});
query.where("id").isEqualTo(5LL);
query.and_("name").isLike("%test%");

auto result = qc.execute(query); // renders SQL for the active driver and executes
if (result) {
    for (auto & row : *result) { /* row[i] -- QcSqlBase::QcVariant */ }
}
```

The complete practical API guide — [docs/usage.md](docs/usage.md).

## Features

- **Query builder** — `QcSqlQuery` (`SELECT`: `FROM`/`JOIN`/CTE/subqueries,
  `WHERE`/`HAVING` with the full comparator vocabulary, `GROUP BY`/`ORDER BY`/
  `LIMIT`/`OFFSET`, `UNION`/`INTERSECT`/`EXCEPT`) and `QcSqlInsert`/
  `QcSqlUpdate`/`QcSqlDelete` (including `RETURNING`/`OUTPUT` where the driver
  supports it).
- **20 SQL functions** in `SELECT` expressions (`QcSqlQueryValue`) — `CAST`,
  `CONCAT`, aggregates (`COUNT`/`SUM`/`AVG`/`MIN`/`MAX`), `COALESCE`/`NULLIF`,
  `TRIM`/`SUBSTRING`/`LENGTH`/`REPLACE`, searched `CASE WHEN`,
  `EXTRACT`/`DATEADD`-equivalents.
- **Five native drivers** — PostgreSQL, MySQL, MSSQL, SQLite, Oracle —
  selected at runtime (`QcConnectionParams::driver`), without ORM abstractions
  on top of the client libraries.
- **Automatic identifier quoting** and dialect differences
  (placeholders, `CAST`-types, `LIMIT`/`OFFSET`, `CONCAT`, `RETURNING`, `IS
  DISTINCT FROM`, ...) centralized in `QcSqlDialect` — see
  [docs/architecture.md](docs/architecture.md).
- **`QcConnectionPool`** — thread-safe connection pool (`Permanent`/
  `OnDemand`), reconnect of dead connections, `tryAcquire(timeout)`.
- **`QueryCreator`** — facade: builds a query, renders and executes it through
  the pool in one call (`qc.execute(query)`).
- **Named result access** — `executeNamed()`/
  `executeReturningNamed()` return rows as `map<string, QcVariant>`
  (column → value) — alongside the positional API.

## Requirements

- Compiler with C++20 support (tested on GCC 13; target platforms —
  Linux and Windows/MSVC).
- CMake ≥ 3.21.
- Network at first configuration — tests pull GoogleTest via
  `FetchContent`, and some DB drivers via vcpkg (see below).

The library itself (query builder) pulls no third-party dependencies — the whole API
is built on the STL. The only project-wide dependency is the native
DB client, selected at configuration time.

## DB Drivers

Driver selection is two-stage:

- **At build time** the CMake option `QC_DB_DRIVERS` (default —
  `PostgreSQL`) selects *which* of the five drivers are compiled into this
  binary: a semicolon-separated list (e.g. `-DQC_DB_DRIVERS="PostgreSQL;SQLite"`)
  or `All` — all five at once.
- **At runtime** `QcConnectionParams::driver` (`enum class QcDbDriver {
  PostgreSQL, Oracle, MySQL, SQLite, MSSQL }`) selects which of the
  compiled-in drivers to use for a given connection.
  The `QcNativeConnection` constructor throws `std::runtime_error` if
  the requested driver was not compiled in.

| Driver | Client library | How it's pulled in |
|---|---|---|
| `PostgreSQL` (default) | libpq | vcpkg |
| `MySQL` | libmysqlclient (8.4 LTS branch) | vcpkg |
| `MSSQL` | ODBC (Microsoft ODBC Driver 18 / FreeTDS) | system ODBC (Windows) / unixODBC (Linux/macOS) |
| `SQLite` | sqlite3 | amalgamation vendored in the repo, [`third_party/sqlite/`](third_party/sqlite/) |
| `Oracle` | OCI (Oracle Instant Client) | installed manually — not distributed via vcpkg (licensing) |

`PostgreSQL`/`MySQL` are pulled in automatically via
[vcpkg manifest mode](https://learn.microsoft.com/vcpkg/consume/manifest-mode)
(see [`vcpkg.json`](vcpkg.json)) — this requires a cloned and
bootstrapped vcpkg, with the path in `VCPKG_ROOT`:

```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh   # Windows: .\vcpkg\bootstrap-vcpkg.bat
export VCPKG_ROOT=$(pwd)/vcpkg   # Windows (PowerShell): $env:VCPKG_ROOT = "$pwd\vcpkg"
```

After that — the usual `cmake --preset` (see [`CMakePresets.json`](CMakePresets.json)),
which itself forwards the vcpkg toolchain and the necessary `VCPKG_MANIFEST_FEATURES`:

```bash
cmake --preset postgresql   # or: mysql, mssql, sqlite, oracle, all
cmake --build --preset postgresql
ctest --preset postgresql --output-on-failure
```

`sqlite`/`oracle` do not require `VCPKG_ROOT` — SQLite is fully vendored in the
project tree, and for Oracle you need to manually install Instant Client (Basic +
SDK) and point to it via `OCI_ROOT`/`ORACLE_HOME`. `mssql`
requires system unixODBC (Linux/macOS) or system ODBC (Windows) plus a
registered SQL Server ODBC driver (e.g. `msodbcsql18`). The `all`
preset compiles all five drivers into one binary — requires all
listed dependencies at once.

Without presets (e.g. for system libpq already installed via
`apt`/`dnf`):

```bash
cmake -S . -B build -DQC_DB_DRIVERS=PostgreSQL
cmake --build build
./build/queryCreator
```

## Testing

```bash
ctest --preset postgresql --output-on-failure
```

Tests use GoogleTest, pulled in automatically via CMake `FetchContent`.
Some tests are integration tests against a real database (not mocked): if the
required DB is unavailable, they are skipped (`GTEST_SKIP`), not failed.
Connection data — [`tests/db_config.ini`](tests/db_config.ini) (gitignored,
template — [`tests/db_config.ini.example`](tests/db_config.ini.example)).
Details — [docs/testing.md](docs/testing.md).

Disable test builds: `-DBUILD_TESTING=OFF`.

## Project structure

```
CMakeLists.txt
CMakePresets.json          # cmake --preset <postgresql|mysql|mssql|sqlite|oracle|all>
vcpkg.json                  # vcpkg manifest — features postgresql/mysql
cmake/
└── FindOracleOCI.cmake     # Oracle Instant Client discovery (OCI not covered by vcpkg)
third_party/sqlite/         # vendored SQLite amalgamation (sqlite3.c/.h)
src/
├── main.cpp                # demo usage example
└── query/                  # query-building engine
    ├── qcsqlbase.h              # common types/enums (compareTypes/functionTypes/dataTypes/...), QcSqlStatement
    ├── qcdbdriver.h              # enum QcDbDriver -- runtime driver selection
    ├── qcdeepptr.h               # QcDeepPtr<T> -- deep-copying owning pointer (subquery ownership)
    ├── qcsqldialect.h/.cpp       # everything in SQL generation that differs by QcDbDriver
    ├── qcsqlquery.h/.cpp         # SELECT builder + toSql()/useDriver()
    ├── qcsqlqueryelement.h/.cpp  # one WHERE/HAVING condition + toSql()
    ├── qcsqlqueryvalue.h/.cpp    # one SELECT value (function chain) + toSql()
    ├── qcsqlinsert.h/.cpp        # INSERT builder + toSql()
    ├── qcsqlupdate.h/.cpp        # UPDATE builder + toSql()
    ├── qcsqldelete.h/.cpp        # DELETE builder + toSql()
    ├── qcconnectionparams.h      # connection parameters (driver/host/port/database/user/password)
    ├── qcdriverconnection.h/.cpp # IQcDriverConnection interface, common to all driver backends
    ├── qcdriverfactory.h         # create*Connection() one per driver, guarded by #ifdef QC_DB_HAS_*
    ├── qcdriverpostgresql.cpp    # PostgreSQL driver backend (libpq)
    ├── qcdriveroracle.cpp        # Oracle driver backend (OCI)
    ├── qcdrivermysql.cpp         # MySQL driver backend (libmysqlclient)
    ├── qcdriversqlite.cpp        # SQLite driver backend
    ├── qcdrivermssql.cpp         # MSSQL driver backend (ODBC)
    ├── qcnativeconnection.h/.cpp # one physical connection -- dispatcher by QcConnectionParams::driver
    ├── qcconnectionpool.h/.cpp   # thread-safe connection pool (Permanent/OnDemand)
    └── querycreator.h/.cpp       # facade: toSql() + execute() through pool in one call
tests/                       # GoogleTest -- see docs/testing.md
.github/workflows/ci.yml     # build + test on Linux and Windows for each driver
```

## Documentation

- [docs/usage.md](docs/usage.md) — practical guide to the entire
  public API: building queries, connecting to a DB, `QueryCreator`,
  named results.
- [docs/architecture.md](docs/architecture.md) — architecture: layers,
  class diagram, driver internals and SQL generation.
- [docs/components.md](docs/components.md) — line-by-line breakdown of the
  public API of each file.
- [docs/testing.md](docs/testing.md) — test harness setup, how to
  run tests, coverage map.

## Known limitations

- Multi-row `INSERT` (one `VALUES` with multiple row groups) is not
  supported — call `toSql()`/`execute()` in a loop.
- `returning()` on MySQL works only for `INSERT`, only through
  `QueryCreator`, and requires the explicit name of the `AUTO_INCREMENT` column.
  For `UPDATE`/`DELETE` it has no effect.
- `returning()` on Oracle for `UPDATE`/`DELETE` actually affecting
  more than one row returns `nullopt` (OUT-binds here are scalar, not
  array-based) — the DML itself is not committed in this case.
- `distinctOn()` — natively only on PostgreSQL, silently degrades
  to plain `DISTINCT` on the other four drivers.
- `limit(..., withTies=true)` on SQLite renders ANSI syntax that SQLite
  does not support — fails at execution, not at SQL generation.
- `addFullJoin()` on MySQL renders `FULL JOIN`, which MySQL does not
  support in any form.
- Named results (`executeNamed()`/`QcNamedRow`) with duplicate
  column names (self-join without aliases) preserve only the last one.
- No timeout on query execution itself (only on connection and on
  waiting for a free connection in the pool); `execute()` does not reopen
  the connection and does not retry the command on connection loss during
  execution — only on lease from the pool.
