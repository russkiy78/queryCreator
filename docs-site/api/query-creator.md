# QueryCreator Facade

`QueryCreator` ties the query builder (`toSql()`) to a connection pool
(`execute()`) in a single call. It replaces the two manual steps:

```cpp
// Without QueryCreator (manual):
const QcSqlStatement stmt = query.toSql();
auto result = pool.acquire().connection().execute(stmt.sql, stmt.params);

// With QueryCreator:
auto result = qc.execute(query);
```

## Creating a QueryCreator

```cpp
#include "query/querycreator.h"

QcConnectionParams params;
params.host = "127.0.0.1";
params.port = "5432";
params.database = "demo";
params.user = "demo";
params.password = "demo";

// Default: Permanent mode, pool size 1
QueryCreator qc(params);

// Custom mode and size
QueryCreator qc2(params, QcConnectionPool::Mode::OnDemand, 8);
```

`QueryCreator` **owns** its connection pool — constructed from `params`
at creation time. The point of the facade is "set up once, then one call
per query."

The dialect is set automatically from `QcConnectionParams::driver`. You
do not need to call `useDriver()` on the builders — `QueryCreator` passes
its driver to every `toSql()` internally.

## execute() Overloads

Four typed overloads — one per builder:

```cpp
std::optional<QcResultSet> execute(const QcSqlQuery & query);
std::optional<QcResultSet> execute(const QcSqlInsert & insert);
std::optional<QcResultSet> execute(const QcSqlUpdate & update);
std::optional<QcResultSet> execute(const QcSqlDelete & del);
```

Plus an escape hatch for raw SQL:

```cpp
std::optional<QcResultSet> execute(const std::string & sql, const QcVariantList & params = {});
```

### Full Example

```cpp
QueryCreator qc(params);

// SELECT
QcSqlQuery query;
query.fromTable("users");
query.addReturnValues({"id", "name"});
query.where("id").isEqualTo(5LL);
auto rows = qc.execute(query);

// INSERT
QcSqlInsert insert;
insert.into("users")
    .set("login", std::string("bob"))
    .set("is_active", 1LL);
auto insertResult = qc.execute(insert);

// UPDATE
QcSqlUpdate update;
update.table("users")
    .set("is_active", 0LL);
update.where("id").isEqualTo(5LL);
qc.execute(update);

// DELETE
QcSqlDelete del;
del.from("users");
del.where("id").isEqualTo(5LL);
qc.execute(del);

// Raw SQL (DDL, transactions, computed expressions)
qc.execute("BEGIN");
qc.execute("CREATE TABLE IF NOT EXISTS logs (ts TEXT, msg TEXT)");
qc.execute(
    "UPDATE users SET metadata = jsonb_set(metadata, '{tag}', ?::jsonb) WHERE id = ?",
    {std::string("\"admin\""), 1LL}
);
qc.execute("COMMIT");
```

## executeNamed() Overloads

Named counterparts returning `QcNamedResultSet` (rows as
`map<columnName, QcVariant>`):

```cpp
std::optional<QcNamedResultSet> executeNamed(const QcSqlQuery & query);
std::optional<QcNamedResultSet> executeNamed(const QcSqlInsert & insert);
std::optional<QcNamedResultSet> executeNamed(const QcSqlUpdate & update);
std::optional<QcNamedResultSet> executeNamed(const QcSqlDelete & del);
std::optional<QcNamedResultSet> executeNamed(const std::string & sql, const QcVariantList & params = {});
```

```cpp
QcSqlQuery query;
query.fromTable("employees");
query.addReturnValues({"id", "full_name <name>"});
query.where("department_id").isEqualTo(3LL);

auto result = qc.executeNamed(query);
if (result) {
    for (const QcNamedRow & row : *result) {
        auto id   = row.at("id");    // from metadata of rendered SQL
        auto name = row.at("name");  // alias "name", not "full_name"
    }
}
```

Column name resolution: the key is taken from driver result-set metadata —
if an alias was given (`"col <alias>"`), the key is the alias; otherwise,
the key is the column name itself.

Duplicate column names (self-join without aliases) keep only the **last**
one in the map. Disambiguate by aliasing (`"e.id <emp_id>"`,
`"m.id <manager_id>"`).

## MySQL RETURNING Emulation

`QueryCreator::execute(const QcSqlInsert &)` automatically handles MySQL's
`LAST_INSERT_ID()` emulation when the second `returning(columns, autoIncrementColumn)`
overload was used:

1. `BEGIN`
2. Execute the INSERT
3. `SELECT <columns> FROM <table> WHERE <autoIncrementColumn> = LAST_INSERT_ID()`
4. `COMMIT`
5. Return the SELECT result

The entire sequence runs on a single acquired connection, wrapped in a
transaction. This works **only for INSERT** — `LAST_INSERT_ID()` is
meaningless for UPDATE/DELETE.

### Caveats

- **Do not** use MySQL returning emulation inside an already-open explicit
  transaction — MySQL has no nested transactions; the internal `BEGIN`
  would implicitly commit the outer one.
- On every non-MySQL driver, the second overload behaves identically to the
  first — `autoIncrementColumn` is simply ignored.
- The returned values come from a second SELECT reading the row back, not
  from the INSERT itself. Within the same transaction the insert is always
  visible, but a concurrent delete between the two statements from
  *another* session could interleave (though rarely).

## Oracle RETURNING Handling

`QueryCreator` automatically uses `executeReturning()` (not plain `execute()`)
for INSERT/UPDATE/DELETE — so the Oracle RETURING...INTO OUT-bind path
is handled correctly. When working through `QueryCreator`, you never need
to worry about `executeReturning()` vs `execute()`.

## Manual Control (Lease-Level)

When you need explicit control over a specific connection (e.g. multiple
operations in one lease, or a transaction spanning several statements),
build your own pool and acquire leases manually:

```cpp
QcConnectionPool pool(params, QcConnectionPool::Mode::Permanent, 1);

auto lease = pool.acquire();
auto & conn = lease.connection();

conn.execute("BEGIN");
// ... multiple statements ...
conn.execute("COMMIT");
// lease returns connection to pool on scope exit
```

The manual path still works alongside `QueryCreator` — `QueryCreator` does
not replace it, just collapses the common "build + execute" pattern into
one call.
