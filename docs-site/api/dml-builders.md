# INSERT / UPDATE / DELETE

Three separate builders for data modification, all sharing the same
fluent-API and `toSql()` pattern as `QcSqlQuery`.

## INSERT (`QcSqlInsert`)

Single-row INSERT builder:

```cpp
#include "query/qcsqlinsert.h"

QcSqlInsert insert;
insert.into("users");
insert.set("id", 1LL);
insert.set("login", std::string("alice"));
insert.set("is_active", 1LL);
```

`set()` accumulates `column = value` pairs in order. A repeated `set()` on
an already-set column **overwrites** the value in-place (no duplicate column)
— convenient for reusing one builder in a loop.

### RETURNING

Request inserted columns back:

```cpp
insert.returning({"id", "created_at"});

// PostgreSQL/SQLite: INSERT INTO users (...) VALUES (...) RETURNING "id", "created_at"
// MSSQL:            INSERT INTO users (...) OUTPUT inserted."id", inserted."created_at" VALUES (...)
// Oracle:           INSERT INTO users (...) VALUES (...) RETURNING "id", "created_at" INTO :N, :N+1
// MySQL:            (no clause — see emulation below)
```

### MySQL RETURNING Emulation

MySQL has no `INSERT ... RETURNING` at all. A special overload accepts the
`AUTO_INCREMENT` column name and causes `QueryCreator` to run a follow-up
`SELECT` in the same transaction:

```cpp
insert.into("users")
    .set("login", std::string("alice"))
    .returning({"id", "login"}, "id");
// QueryCreator::execute(insert) on MySQL:
//   BEGIN; INSERT ...; SELECT id, login FROM users WHERE id = LAST_INSERT_ID(); COMMIT;
```

This works **only through `QueryCreator`** (multi-statement orchestration
needed) and **only for INSERT** (`LAST_INSERT_ID()` is meaningless for
UPDATE/DELETE).

## UPDATE (`QcSqlUpdate`)

```cpp
#include "query/qcsqlupdate.h"

QcSqlUpdate update;
update.table("users");
update.set("login", std::string("alice2"));
update.set("updated_at", std::string("2025-01-01"));
update.where("id").isEqualTo(1LL);
update.and_("is_active").isEqualTo(1LL);
```

The WHERE side is the same `QcSqlQueryElement` with the same comparator
vocabulary (`isEqualTo`, `isLike`, `isIn`, `isBetween`, ...) and
AND/OR/parenthesis sugar as `QcSqlQuery::where()`.

```cpp
update.returning({"id", "login"});
// Adds RETURNING / OUTPUT clause
```

## DELETE (`QcSqlDelete`)

```cpp
#include "query/qcsqldelete.h"

QcSqlDelete del;
del.from("users");
del.where("id").isEqualTo(1LL);
del.or_("status").isEqualTo(std::string("banned"));
```

Same WHERE mechanics as UPDATE, minus the SET list.

```cpp
del.returning({"id"});
// Adds RETURNING / OUTPUT clause
```

## RETURNING Across Drivers

`returning()` is available on all three builders with per-driver behavior:

| Driver | Clause | Notes |
|--------|--------|-------|
| **PostgreSQL** | `RETURNING col1, col2` | At end of statement |
| **SQLite** | `RETURNING col1, col2` | Supported since 3.35.0 |
| **MSSQL** | `OUTPUT inserted.col1, inserted.col2` | Between columns and VALUES; `deleted.` for DELETE |
| **Oracle** | `RETURNING col1, col2 INTO :N, :N+1` | Values via OUT binds, not result rows. Update/Delete affecting >1 row returns `nullopt` |
| **MySQL** | *(no clause)* | INSERT only via `QueryCreator` emulation (see above) |

### Oracle RETURNING Limitations

- OUT binds are **scalar**, not array-based — designed for statements
  affecting exactly one row (typical for INSERT with PK, and UPDATE/DELETE
  with PK-like WHERE).
- An UPDATE/DELETE with `returning()` affecting more than one row returns
  `nullopt` cleanly (the DML is not committed).
- Use `executeReturning()` (not plain `execute()`) — `returningColumnCount`
  in `QcSqlStatement` tells how many trailing OUT-bind placeholders to read.
  `QueryCreator` handles this automatically.

### MySQL Limitations

- No `RETURNING`/`OUTPUT` at statement level.
- The `SELECT ... WHERE id = LAST_INSERT_ID()` emulation is wrapped in
  a transaction — the inserted row is visible to the follow-up SELECT.
- Do **not** call this inside an already-open explicit transaction — MySQL
  has no nested transactions; the internal `BEGIN` would implicitly commit
  the outer transaction.

## SQL Generation

All three builders render via `toSql()` → `QcSqlStatement{sql, params}`:

```cpp
const QcSqlStatement stmt = insert.toSql();
auto result = connection.execute(stmt.sql, stmt.params);
```

Multi-row INSERT (one `VALUES` with multiple row groups) is **not
supported** — call `toSql()`/`execute()` in a loop.

## EDSL (Easy Domain-Specific Language)

The `set()` method on `QcSqlInsert` and `QcSqlUpdate` supports the full
`QcVariant` type system:

```cpp
using QcVariant = std::variant<
    std::monostate,           // NULL
    long long,                // integers
    double,                   // floats
    std::string,              // text
    std::vector<std::byte>    // BLOB
>;
```

All values are bound through native parameterized queries — safe against
SQL injection by construction.
