# Getting Started

## Requirements

- **C++20** compiler (tested with GCC 13; target platforms: Linux and Windows/MSVC)
- **CMake** ≥ 3.21
- Network access for first-time configuration (test dependencies pulled via `FetchContent`; some DB driver dependencies via vcpkg)

The query builder library itself has **zero external dependencies** — the entire API is built on the STL (`std::string`, `std::vector`, `std::variant`, `std::regex`, etc.).

## Installation

### Step 1: Clone the Repository

```bash
git clone https://github.com/russkiy78/queryCreator.git
cd queryCreator
```

### Step 2: Set Up vcpkg (for PostgreSQL/MySQL)

PostgreSQL and MySQL drivers are pulled in via [vcpkg manifest mode](https://learn.microsoft.com/vcpkg/consume/manifest-mode):

```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=$(pwd)/vcpkg
```

SQLite is vendored in the repository and requires no vcpkg. Oracle needs a manual Instant Client installation.

### Step 3: Build with CMake Presets

```bash
# Choose your driver preset:
cmake --preset postgresql   # PostgreSQL only
cmake --preset mysql        # MySQL only
cmake --preset mssql        # MSSQL only
cmake --preset sqlite       # SQLite only
cmake --preset oracle       # Oracle only
cmake --preset all          # All five drivers

# Build
cmake --build --preset postgresql

# Run tests
ctest --preset postgresql --output-on-failure
```

### Step 4: Without Presets (system libpq)

```bash
cmake -S . -B build -DQC_DB_DRIVERS=PostgreSQL
cmake --build build
./build/queryCreator
```

## Minimal Working Example

```cpp
#include "query/querycreator.h"
#include "query/qcsqlquery.h"

int main() {
    // 1. Connection parameters
    QcConnectionParams params;
    params.host = "127.0.0.1";
    params.port = "5432";
    params.database = "demo";
    params.user = "demo";
    params.password = "demo";

    // 2. Create facade (owns a connection pool)
    QueryCreator qc(params);

    // 3. Build a query
    QcSqlQuery query;
    query.fromTable("users");
    query.addReturnValues({"id", "name"});
    query.where("id").isEqualTo(5LL);

    // 4. Execute — render SQL and run in one call
    auto result = qc.execute(query);
    if (result) {
        for (auto & row : *result) {
            // row[0] = id, row[1] = name
        }
    }

    return 0;
}
```

## Build Matrix (`QC_DB_DRIVERS`)

The CMake cache variable `QC_DB_DRIVERS` controls which native client libraries
are compiled into the binary:

| Value | Effect |
|-------|--------|
| `PostgreSQL` (default) | libpq only |
| `PostgreSQL;SQLite` | libpq + vendored sqlite3 |
| `MySQL;MSSQL` | libmysqlclient + ODBC |
| `All` | All five drivers |

At runtime, `QcConnectionParams::driver` selects which of the compiled-in
drivers to use for a given connection. The constructor of `QcNativeConnection`
throws `std::runtime_error` if the requested driver was not compiled in.

## Test Database Configuration

Integration tests run against a **real database** (not mocked):

```bash
# Copy and edit the config template
cp tests/db_config.ini.example tests/db_config.ini
# Edit tests/db_config.ini — fill in [postgresql] / [mysql] / etc.
```

If `tests/db_config.ini` is absent, hardcoded local defaults are used.
Tests **skip** (`GTEST_SKIP`) when the database is unreachable — they never
fail the run on machines without a local DB.

## Next Steps

- **[SELECT Queries](/api/select-queries)** — Building SELECT statements
- **[WHERE Conditions](/api/where-conditions)** — All 20+ comparison operators
- **[SELECT Functions](/api/select-functions)** — 20 SQL functions for result columns
- **[JSON Fields](/api/json-fields)** — Cross-driver JSON extraction and comparison
- **[INSERT/UPDATE/DELETE](/api/dml-builders)** — Data modification builders
