# Database Connection

## Architecture

Three levels form the lower layer:

1. **`QcConnectionParams`** — POD with connection parameters
2. **`QcNativeConnection`** — RAII wrapper for one physical connection
3. **`QcConnectionPool`** — thread-safe pool of connections

## Connection Parameters

```cpp
#include "query/qcconnectionparams.h"

QcConnectionParams params;
params.driver  = QcDbDriver::PostgreSQL;  // runtime driver selection
params.host    = "127.0.0.1";
params.port    = "5432";
params.database = "demo";
params.user     = "demo";
params.password = "demo";
params.connectTimeoutSeconds = 5;  // 0 = driver's own default
```

Not all fields are meaningful for every driver:

| Driver | Fields Used |
|--------|-------------|
| PostgreSQL | All five |
| MySQL | All five |
| MSSQL | `host`/`port` as `SERVER=host,port` in ODBC connection string |
| SQLite | Only `database` (as file path) |
| Oracle | `host`/`port`/`database` as Easy Connect string `host:port/database` |

## Single Connection (No Pool)

```cpp
#include "query/qcnativeconnection.h"

QcNativeConnection connection(params);  // throws std::runtime_error on failure

auto result = connection.execute(
    "SELECT id, name FROM groups WHERE id = $1",
    {1LL}
);

if (result) {
    for (const auto & row : *result) {
        // row[i] is a QcSqlBase::QcVariant
    }
}
```

### isAlive()

Actively probes the connection with a real round-trip query
(`SELECT 1` / `SELECT 1 FROM DUAL`). A purely local status read cannot
detect a server-killed connection until something actually attempts I/O.

### isOpen()

Checks local state only — faster than `isAlive()` but may report `true`
for a connection the server has already dropped.

### nativeHandle()

Returns the opaque driver-specific handle (`PGconn*`, `sqlite3*`, etc.)
for identity checks and debugging.

### execute() / executeReturning()

```cpp
// Standard execution
auto result = connection.execute(sql, params);
// nullopt = error; empty set = success with no rows (DDL, INSERT without RETURNING)
// non-empty = result rows

// Oracle RETURNING support — reads OUT-bind placeholders
auto result = connection.executeReturning(sql, params, returningColumnCount);
```

`executeReturning()` is needed only for Oracle (RETURNING INTO uses OUT binds).
On every other driver it is exactly equivalent to `execute()`, and
`returningColumnCount` is ignored.

### executeNamed() / executeReturningNamed()

Named counterparts returning `QcNamedResultSet` (rows as `map<columnName, QcVariant>`):

```cpp
auto result = connection.executeNamed(sql, params);
if (result) {
    for (const QcNamedRow & row : *result) {
        auto id   = row.at("id");
        auto name = row.at("name");
    }
}
```

Column names come from driver result-set metadata (or
`QcSqlStatement::returningColumnNames` on Oracle's OUT-bind path).

### Cell Typing

The type of each cell in the result varies by driver:

| Driver | Non-NULL cells |
|--------|---------------|
| PostgreSQL | `std::string` (text wire protocol) |
| Oracle | `std::string` (`SQLT_STR`), except CLOB/BLOB → `std::string`/`std::vector<std::byte>` |
| SQLite | `long long`/`double`/`std::string`/`std::vector<std::byte>` (by storage class) |
| MySQL | Typed via `enum_field_types` from binary protocol |
| MSSQL | Typed via `SQLDescribeCol` |

## Connection Pool

```cpp
#include "query/qcconnectionpool.h"
```

Two operating modes:

### Permanent Mode

```cpp
QcConnectionPool pool(params, QcConnectionPool::Mode::Permanent, 4);
// Opens 4 connections immediately, keeps them alive, reuses them.

{
    auto lease = pool.acquire();  // blocks if all 4 are in use
    lease.connection().execute("SELECT 1");
}  // connection returned to pool (RAII)

// Dead connections transparently replaced on next acquire()
```

### OnDemand Mode

```cpp
QcConnectionPool pool(params, QcConnectionPool::Mode::OnDemand, 8);
// No connections kept open between uses.

{
    auto lease = pool.acquire();  // opens a new connection now
    lease.connection().execute("SELECT 1");
}  // connection closed here, not returned to pool
// `size` = max concurrent connections, not pool capacity
```

### tryAcquire(timeout)

```cpp
if (auto lease = pool.tryAcquire(std::chrono::milliseconds(200))) {
    lease->connection().execute("SELECT 1");
} else {
    // pool exhausted within timeout
}
```

### Lease (RAII Handle)

`acquire()` and `tryAcquire()` return `QcConnectionPool::Lease` — an RAII
handle that automatically returns the connection to the pool (Permanent)
or closes it (OnDemand) when destroyed. This includes exception unwind paths.

- Move-only (copying is forbidden)
- `lease.connection()` provides access to the underlying `QcNativeConnection &`
- Never exposes raw `QcNativeConnection*` without the RAII wrapper

### Thread Safety

Both modes are protected by a single `std::mutex` + `std::condition_variable`
(tested under ThreadSanitizer). A single connection obtained through a `Lease`
must not be used by multiple threads simultaneously — same rule as any API
on top of a socket.

### Reconnection

In Permanent mode, an idle connection is checked with `isAlive()` before
being handed out. Dead connections are transparently replaced. If
replacement fails (DB unreachable), the slot is preserved as a `nullptr`
placeholder — the exception propagates to the caller, and the slot is
retried on the next `acquire()`.

## What's Not Implemented

- **Statement timeout** (`statement_timeout`) — only connection/pool-wait timeouts
  are supported. The calling code may set `connectTimeoutSeconds` for the
  initial connect.
- **Automatic SQL retry** on connection loss *during* execution (not at
  lease time) — unsafe for non-idempotent commands, left to the caller.
- **Pool shrink/grow** at runtime — `size` is fixed at construction.
