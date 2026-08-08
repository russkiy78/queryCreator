---
layout: home

hero:
  name: "queryCreator"
  text: "Lightweight SQL Query Builder"
  tagline: Pure C++20, five native DB drivers, fluent API
  actions:
    - theme: brand
      text: Get Started
      link: /getting-started
    - theme: alt
      text: View on GitHub
      link: https://github.com/russkiy78/queryCreator

features:
  - icon: 🏗️
    title: Fluent Query Builder
    details: Build SELECT, INSERT, UPDATE, and DELETE queries through chainable method calls. Full support for JOINs, CTEs, subqueries, set operations, and the complete SQL comparator vocabulary.
  - icon: 🗄️
    title: Five Native Drivers
    details: PostgreSQL, MySQL, MSSQL, SQLite, and Oracle — all selected at runtime from a single binary. No ORM abstractions, direct native client libraries.
  - icon: 🔄
    title: Automatic Dialect Rendering
    details: Placeholders, CAST types, LIMIT/OFFSET syntax, JSON extraction, identifier quoting, and RETURING/OUTPUT — all differences centralized in QcSqlDialect.
  - icon: 🔒
    title: Injection-Safe
    details: All values bound through real parameterized queries — not string concatenation. Native prepared statements on every driver.
  - icon: 🧵
    title: Thread-Safe Connection Pool
    details: Permanent and OnDemand modes with automatic dead-connection detection and replacement. RAII lease handles prevent connection leaks.
  - icon: 📦
    title: Zero External Dependencies
    details: The query builder library itself depends on nothing but the C++20 STL. No Boost, no frameworks, no package managers required.

---

## Quick Start

```bash
# Clone and build
git clone https://github.com/russkiy78/queryCreator.git
cd queryCreator
cmake --preset postgresql
cmake --build --preset postgresql
ctest --preset postgresql --output-on-failure
```

```cpp
#include "query/querycreator.h"
#include "query/qcsqlquery.h"

// Connect to PostgreSQL
QcConnectionParams params;
params.host = "127.0.0.1";
params.port = "5432";
params.database = "demo";
params.user = "demo";
params.password = "demo";

// Create a facade that owns a connection pool
QueryCreator qc(params);

// Build a SELECT query
QcSqlQuery query;
query.fromTable("users");
query.addReturnValues({"id", "name <display_name>"});
query.where("id").isEqualTo(5LL);
query.and_("name").isLike("%test%");
query.orderDesc({"id"});
query.limit(20);

// Render SQL and execute in one call
auto result = qc.execute(query);
if (result) {
    for (auto & row : *result) {
        // row[i] is a QcSqlBase::QcVariant (std::variant)
    }
}
```

## Supported Drivers

| Driver | Client Library | Placeholder | Quoting |
|--------|---------------|-------------|---------|
| **PostgreSQL** | libpq | `$1`, `$2`, ... | `"identifier"` |
| **MySQL** | libmysqlclient | `?` | `` `identifier` `` |
| **MSSQL** | ODBC | `?` | `[identifier]` |
| **SQLite** | sqlite3 | `?` | `"identifier"` |
| **Oracle** | OCI | `:1`, `:2`, ... | `"identifier"` |

## Project Structure

```
src/query/
├── qcsqlbase.h              Common types, enums, QcSqlStatement
├── qcsqlquery.h/.cpp        SELECT builder
├── qcsqlqueryelement.h/.cpp WHERE/HAVING conditions
├── qcsqlqueryvalue.h/.cpp   SELECT-list values with function chains
├── qcsqlinsert.h/.cpp       INSERT builder
├── qcsqlupdate.h/.cpp       UPDATE builder
├── qcsqldelete.h/.cpp       DELETE builder
├── qcsqldialect.h/.cpp      Per-driver SQL rendering
├── qcnativeconnection.h/.cpp Physical database connection
├── qcconnectionpool.h/.cpp  Thread-safe connection pool
└── querycreator.h/.cpp      Facade: build + execute in one call
```
