# queryCreator — API Guide

Practical guide: how to build an SQL query through the query builder and how
to connect to a database and execute it. Everything shown below is actually
implemented and covered by tests (unit and/or integration — against a live DB where
not stated otherwise). For line-by-line signatures of each class —
[components.md](components.md); for architectural decisions and rationale
behind specific mechanisms (why Oracle `RETURNING` works differently, why
identifier quoting is "conservative", etc.) —
[architecture.md](architecture.md); how to run tests and the coverage map —
[testing.md](testing.md).

## 1. Building a `SELECT` query (`QcSqlQuery`)

`QcSqlQuery` — a `SELECT` builder via fluent call chains, whose entire public
API (`QcSqlQuery`/`QcSqlQueryElement`/`QcSqlQueryValue`) is implemented and
covered by tests, including generation of the final SQL text via `toSql()`.

```cpp
#include "query/qcsqlquery.h"

QcSqlQuery query;

query.addReturnValues({
    "id",
    "name <display_name>",   // "column <alias>" — column + alias on a single line
});

query.addReturnValue("balance <balance_display>")
    .upperCase()
    .cast(QcSqlBase::dataTypes::_string_, QcSqlBase::dataTypes::_float_)
    .round(2);

query.fromTable("users");

// isEqualTo()/isLike()/... return bool (success of setting the condition), not
// QcSqlQueryElement& — they cannot be chained back onto query in a single
// expression, hence the separate statements for where()/and_()/or_().
query.where("id").isEqualTo(5LL);
query.and_("name").isLike("%test%");

query.orderDesc({"id"});
query.limit(20);
```

`query.toSql()` turns all of this into `QcSqlStatement{sql, params}` — ready
SQL text (placeholders for the active driver, see section 4) + a list of
bind parameters in the same order as in the text. An empty `addReturnValues()`
(never called) renders as `SELECT *`.

### 1.1. `FROM`: table, alias, subquery

```cpp
query.fromTable("users");            // FROM users
query.fromTable("employees e");      // FROM employees AS e (alias — second word
                                      // separated by space, no separate parameter;
                                      // needed for self-join, see section 1.2)

QcSqlQuery deptStats;
deptStats.fromTable("employees");
deptStats.addReturnValue("department_id");
deptStats.addReturnValue("salary <avg_salary>").avg();
deptStats.groupBy({"department_id"});

QcSqlQuery outer;
outer.fromSubQuery("dept_stats", deptStats); // FROM (SELECT ...) AS dept_stats
outer.addReturnValues({"department_id", "avg_salary"});
outer.where("avg_salary").isGreaterThan(90000.0);
```

`fromTable()`/`fromSubQuery()` are mutually exclusive — a second call to either
overwrites what the first one set (`FROM` — exactly one source). On Oracle,
the alias after a `FROM`/`JOIN` source renders without the `AS` keyword
(`FROM dual d`, not `FROM dual AS d` — on Oracle this is `ORA-00933`); on
the other four drivers — with `AS`. For column aliases in the `SELECT` list
(`"col <alias>"`) and for CTEs (`WITH name AS (...)`), `AS` works the same
everywhere, including Oracle — the restriction applies only to table/subquery
aliases.

### 1.2. `JOIN`

Five types, each with — alias, `QcSqlQuery` as a source, and raw SQL text
for the condition (`onCondition` does not go through quoting — write it yourself as
the final SQL should look, just like `addFreeText`, see 1.4):

```cpp
QcSqlQuery deptRef;
deptRef.fromTable("departments");

QcSqlQuery query;
query.fromTable("employees e");
query.addJoin("d", deptRef, "d.id = e.department_id");           // INNER JOIN
query.addLeftJoin("d", deptRef, "d.id = e.department_id");       // LEFT JOIN
query.addRightJoin("d", deptRef, "d.id = e.department_id");      // RIGHT JOIN
query.addFullJoin("d", deptRef, "d.id = e.department_id");       // FULL JOIN
query.addCrossJoin("d", deptRef);                                // CROSS JOIN, no ON
```

`asSubQuery = true` (last optional argument, except for
`addCrossJoin`) renders the source as `JOIN (SELECT ...) AS alias ON ...`
instead of `JOIN table AS alias ON ...` — use it when the join itself is built not
from a table, but from another `QcSqlQuery`:

```cpp
QcSqlQuery deptAvg;
deptAvg.fromTable("employees");
deptAvg.addReturnValue("department_id");
deptAvg.addReturnValue("salary <avg_salary>").avg();
deptAvg.groupBy({"department_id"});

query.addJoin("da", deptAvg, "da.department_id = e.department_id", /*asSubQuery=*/true);
```

Self-join (both sides — the same table) requires aliases on both
sides, otherwise identically named columns are indistinguishable — the same limitation
appears in named results (`row.at("id")` — see section 7):

```cpp
QcSqlQuery managerRef;
managerRef.fromTable("employees");

QcSqlQuery query;
query.fromTable("employees e");
query.addLeftJoin("m", managerRef, "m.id = e.manager_id");
query.addReturnValues({"m.id <manager_id>", "e.id"});
```

MySQL does not support `FULL JOIN`/`FULL OUTER JOIN` at all in any form
(`addFullJoin()` there renders SQL that will fail at execution time — the builder does not
attempt to emulate it via `LEFT JOIN UNION RIGHT JOIN`, the emulation
is left to the calling code).

### 1.3. Common table expressions (`WITH`)

```cpp
QcSqlQuery cte;
cte.fromTable("employees");
cte.addReturnValues({"id", "department_id"});
cte.where("is_active").isEqualTo(1LL);

QcSqlQuery query;
query.with_("active_emps", cte);
query.fromTable("active_emps");
query.addReturnValue("* <cnt>").count();
// WITH active_emps AS (SELECT id, department_id FROM employees WHERE is_active = ?)
// SELECT COUNT(*) AS cnt FROM active_emps
```

Multiple `with_()` calls in a row accumulate in call order (`WITH a AS (...), b AS
(...)`); a CTE can only reference what was defined before it, in
the spirit of regular SQL `WITH` — the builder does not check this, the error (if any)
will only surface at execution time.

### 1.4. `WHERE`: comparison operators, `AND`/`OR`, parentheses, raw text

`where()`/`and_()`/`or_()` return `QcSqlQueryElement &`, on which
exactly one comparator is then called — full list:

```cpp
element.isEqualTo(val); element.isNotEqualTo(val);
element.isGreaterThan(val); element.isGreaterThanOrEqualTo(val);
element.isLessThan(val); element.isLessThanOrEqualTo(val);
element.isLike("%pat%"); element.isIlike("%pat%");     // ILIKE is emulated via
element.isNotLike("%pat%"); element.isNotILike("%pat%"); // LOWER(...) on drivers without native ILIKE
element.isNull(); element.isNotNull();
element.isIn({1LL, 2LL, 3LL}); element.isNotIn({1LL, 2LL});
element.isIn(subQuery); element.isNotIn(subQuery);        // IN/NOT IN (SELECT ...)
element.isBetween(1LL, 10LL); element.isNotBetween(1LL, 10LL);
element.isDistinctFrom(val); element.isNotDistinctFrom(val); // NULL-safe !=/=,
                                                               // emulated where IS DISTINCT FROM is absent
element.exists(subQuery); element.notExists(subQuery);
element.isEqualTo(subQuery); element.isNotEqualTo(subQuery); // column = (SELECT ...)
```

`AND`/`OR` and grouping parentheses — via explicit calls, in the order of the target SQL:

```cpp
query.where("a").isEqualTo(1LL);
query.and_OpenParenthesis("b").isEqualTo(2LL);   // AND (b = ? OR c = ?)
query.or_("c").isEqualTo(3LL);
query.closeParenthesis();
// or the same thing without the sugar variant:
// query.and_(...); query.openParenthesis(); ...; query.closeParenthesis();
```

`cast()` on an element wraps the column itself in `CAST(col AS type)` before
comparison (`WHERE CAST(status AS ...) = ?`) — the comparator is always
`isEqualTo` (two independent `cast(from, to, value)` constructors: one for
a regular value, one for a subquery on the right).

**`addFreeText(text, values)`** — escape hatch for conditions that lack a
dedicated method (JSON operators, arbitrary expressions): write `?` in
place of each value, regardless of dialect — `toSql()` will renumber
them into `$N`/`:N` at the appropriate positions when assembling the full query:

```cpp
query.addFreeText("metadata->>'level' = ?", {std::string("admin")});
```

`having()`/`and_Having()`/`or_Having()` — the same `QcSqlQueryElement` and the same
set of comparators, applied after `GROUP BY` (see 1.5); they accept both
a plain column name and an aggregate expression as text (`having("COUNT(*)")`
— a string that does not look like a simple identifier passes through
quoting untouched, see section 9).

### 1.5. `GROUP BY` / `HAVING`

```cpp
query.addReturnValue("department_id");
query.addReturnValue("* <emp_count>").count();
query.addReturnValue("salary <avg_salary>").avg();
query.groupBy({"department_id"});
query.having("COUNT(*)").isGreaterThan(5LL);
```

### 1.6. `ORDER BY` / `LIMIT`/`OFFSET` / `DISTINCT`

```cpp
query.orderAsc({"last_name", "first_name"});
query.orderDesc({"created_at"});     // multiple calls accumulate in call
                                      // order, each preserving its own ASC/DESC

query.orderAsc({"manager_id"}, QcSqlQuery::_nullsFirst_);  // ... NULLS FIRST
query.orderDesc({"nickname"}, QcSqlQuery::_nullsLast_);    // ... DESC NULLS LAST
                                      // `nulls` applies to all columns
                                      // of a single call; for different NULLS positions
                                      // in the same ORDER BY — a separate call per
                                      // column. PostgreSQL/SQLite/Oracle —
                                      // native `NULLS FIRST`/`NULLS LAST`;
                                      // MySQL/MSSQL lack this syntax —
                                      // emulated via a leading tie-breaker
                                      // `CASE WHEN col IS NULL THEN 0 ELSE 1 END`
                                      // (see architecture.md)

query.limit(20);                     // LIMIT 20
query.limit(20, 40);                 // LIMIT 20 OFFSET 40
query.limit(20, 40, /*withTies=*/true); // ANSI OFFSET...FETCH NEXT...WITH TIES

query.distinct();                    // SELECT DISTINCT ...
query.distinctOn({"customer_id"});   // SELECT DISTINCT ON (customer_id) ... —
                                      // PostgreSQL only; on the other four
                                      // silently degrades to plain DISTINCT
                                      // (not an error, but semantically not the same thing)
```

PostgreSQL/MySQL/SQLite render `LIMIT n OFFSET m`; MSSQL/Oracle — ANSI
`OFFSET m ROWS FETCH NEXT n ROWS ONLY` (the only form they
support). `withTies` always switches to the ANSI form (`WITH TIES`
instead of `ONLY`) on all drivers — SQLite has no `WITH TIES` at all in
any form, so with `withTies=true` it renders syntax that will
fail at execution time (a deliberate decision, not silent degradation — see
[architecture.md](architecture.md)).

### 1.7. Set operations (`UNION`/`UNION ALL`/`INTERSECT`/`EXCEPT`)

```cpp
QcSqlQuery a;
a.fromTable("employees");
a.addReturnValues({"id"});
a.where("department_id").isEqualTo(1LL);

QcSqlQuery b;
b.fromTable("employees");
b.addReturnValues({"id"});
b.where("salary").isGreaterThan(120000.0);

a.unionWith(b); // (SELECT id FROM employees WHERE department_id = ?)
                 // UNION (SELECT id FROM employees WHERE salary > ?)
```

`unionAllWith()`/`intersectWith()`/`exceptWith()` — same form; multiple
can be chained in a row on a single builder, accumulating in call order.

### 1.8. Functions in the `SELECT` list (`QcSqlQueryValue`)

`addReturnValue(name)` returns `QcSqlQueryValue &`, on which a chain of
functions can be appended (in call order — each wraps the result of the
previous one):

```cpp
query.addReturnValue("balance <balance_display>")
    .upperCase()
    .cast(QcSqlBase::dataTypes::_string_, QcSqlBase::dataTypes::_float_)
    .round(2);
```

Full list:

- **`cast(from, to)`**, **`round(precision)`**, **`upperCase()`**,
  **`lowerCase()`** — as in the example above.
- **`concat({operand1, operand2, ...})`** — concatenation; each operand is
  either a column name (auto-quoted if it looks like a simple
  identifier) or a ready SQL text/literal (`"' '"`, `"'N/A'"`),
  left as-is. On Oracle/SQLite (before 3.44) renders via `||`, not
  `CONCAT(...)` — Oracle's `CONCAT()` is only binary, and older SQLite lacks it
  entirely.
- **`count(distinct = false)`**, **`sum()`**, **`avg()`**, **`min()`**,
  **`max()`** — standard aggregates, identical syntax on all five
  drivers. `COUNT(*)` — via `addReturnValue("* <alias>").count()`
  (`quoteIdentifier()` never quotes `"*"`), there is no dedicated
  no-argument overload for this.
- **`coalesce({fallback1, fallback2, ...})`** — `COALESCE(expr, ...)`, the same
  raw-convention for operands as `concat()`.
- **`nullIf(compareExpr)`** — `NULLIF(expr, compareExpr)`.
- **`trim()`** — ANSI `TRIM(expr)`, identical on all five.
- **`substring(start, length)`** — 1-based `start`; `SUBSTR(...)` everywhere,
  except MSSQL (`SUBSTRING(...)`, same argument order).
- **`length()`** — `LENGTH(expr)` everywhere, except MSSQL (`LEN(expr)`, which
  also trims trailing spaces before counting — a real behavioral
  difference, not just spelling).
- **`replace(search, replacement)`** — `REPLACE(expr, search, replacement)`.
- **`case_({{cond1, result1}, {cond2, result2}, ...}, elseResult)`** —
  searched `CASE WHEN cond1 THEN result1 ... ELSE elseResult END`. Unlike
  the other functions in this list, it does **not** wrap the current expression
  of the chain — it resets it and builds a new one, so it makes sense to call it first
  (or as the only) link in the chain.
- **`extract(part)`** / **`dateAdd(part, amount)`** — `part` from
  `QcSqlBase::datePartTypes` (`_year_`/`_month_`/`_day_`/`_hour_`/`_minute_`/
  `_second_`). `extract()` — `EXTRACT(YEAR FROM expr)`-equivalent (on MSSQL —
  `DATEPART`, on SQLite — `strftime(...)` with numeric cast).
  `dateAdd()` — adds `amount` units of `part` to a date/time
  (negative `amount` — subtraction); the most syntactically varied
  function across drivers in the builder, see
  [`QcSqlDialect::dateAddExpr()`](../src/query/qcsqldialect.h).

Resulting "kitchen sink" example — `JOIN` + `WHERE` + aggregates + `GROUP BY` +
`HAVING` + `ORDER BY` + `LIMIT` together, exactly as in
`KitchenSinkQueryCombinesJoinWhereGroupByHavingOrderByLimit`
(`tests/test_integration_select.cpp`):

```cpp
QcSqlQuery deptRef;
deptRef.fromTable("departments");

QcSqlQuery query;
query.fromTable("employees e");
query.addJoin("d", deptRef, "d.id = e.department_id");
query.where("e.is_active").isEqualTo(1LL);
query.and_("d.region").isIn({std::string("EU"), std::string("APAC")});
query.addReturnValue("d.id");
query.addReturnValue("* <active_count>").count();
query.addReturnValue("e.salary <avg_salary>").avg();
query.groupBy({"d.id"});
query.having("COUNT(*)").isGreaterThan(0LL);
query.orderDesc({"avg_salary"});
query.limit(3);
```

### 1.9. JSON fields (`isXxxJson...()` / `jsonExtract...()`)

Before this section, the only way to reach a value inside a
JSON column was raw SQL text via `addFreeText()`/`where()`/
`addReturnValue()` — functional, but requiring a separate branch for each of the
five drivers (`metadata->>'level'` on PostgreSQL, `json_extract(...)` on
SQLite, `JSON_VALUE(...)` on MSSQL/Oracle, etc. — see example in
`JsonFieldExtractionReturnsSeededValue`, `tests/test_integration_select.cpp`).
`isXxxJson...()` (on `QcSqlQueryElement`, for `WHERE`/`HAVING`) and
`jsonExtract()`/`jsonExtractNumber()`/`jsonExtractRaw()` (on
`QcSqlQueryValue`, for `SELECT` list) remove this branching entirely —
the same call renders as the correct syntax on its own,
via [`QcSqlDialect::jsonExtractExpr()`](../src/query/qcsqldialect.h).

**`jsonSearchPath`** — a lightweight path without a leading root marker: `.`
separates nested object keys, `[N]` — array element index, e.g.
`"a.b[2].c"` — the value at `.a.b[2].c` (object `a` → nested object `b` →
third element of array → nested object `c`). The same format works on both
the `WHERE` side and the `SELECT` side.

```cpp
query.fromTable("employees");
query.where("metadata").isEqualToJsonNumber(5LL, "level");
query.and_("metadata").isLikeJsonText("c%", "skills[0]");
query.addReturnValue("metadata <rating>").jsonExtractNumber("rating");
query.addReturnValue("metadata <skills_raw>").jsonExtractRaw("skills");
```

**`QcSqlQueryElement`** — compares the value extracted by
`jsonSearchPath`, instead of the column itself; the comparator (`=`/`>`/`LIKE`/...) —
the same set as for regular `where()` conditions (section 1.4), comparison with
`val` works as before via `bind()`. Three families:

- **`isEqualToJsonNumber`/`isNotEqualToJsonNumber`/`isGreaterThanJsonNumber`/
  `isGreaterThanOrEqualToJsonNumber`/`isLessThanJsonNumber`/
  `isLessThanOrEqualToJsonNumber`** — the extracted value is cast to a
  numeric SQL type, comparison is numeric (`10 > 9`), not lexicographic
  (`"10" > "9"` would be `false`).
- **`isEqualToJsonText`/`isNotEqualToJsonText`/`isGreaterThanJsonText`/
  `isGreaterThanOrEqualToJsonText`/`isLessThanJsonText`/
  `isLessThanOrEqualToJsonText`** and **`isLikeJsonText`/`isIlikeJsonText`/
  `isNotLikeJsonText`/`isNotILikeJsonText`** — the extracted scalar as
  unquoted text (the JSON string `"admin"` becomes plain
  `admin`).
- **`isLikeJsonArrayAsText`/`isIlikeJsonArrayAsText`** — unlike
  `isLikeJsonText`, searches the *raw* JSON representation of the value at the path
  (array/object remains with its brackets/quotes) — this way one can search
  across the whole array at once (`isLikeJsonArrayAsText("%\"python\"%", "skills")`
  will find `"python"` at any position of the `skills` array), not just
  a single indexed element.

**`QcSqlQueryValue`** — `jsonExtract(jsonSearchPath)` (scalar, unquoted text),
`jsonExtractNumber(jsonSearchPath)` (cast to number),
`jsonExtractRaw(jsonSearchPath)` (raw JSON fragment — array/object with
its brackets, analogous to `JSON_QUERY` on MSSQL/Oracle as opposed to
the scalar `JSON_VALUE`). Like any other function in the
`QcSqlQueryValue` chain (section 1.8), it wraps the current expression — you can
continue the chain further (`jsonExtractNumber("rating").round(1)`).

`QcSqlDialect::jsonExtractExpr()` — the single place with dialect
differences (full breakdown for each of the five drivers — in
[architecture.md](architecture.md#json-queries--qcsqldialectjsonextractexpr)).

## 2. Data modification (`QcSqlInsert` / `QcSqlUpdate` / `QcSqlDelete`)

`QcSqlQuery` builds only `SELECT` — for `INSERT`/`UPDATE`/`DELETE` there are three
separate builders of the same form (fluent API, `toSql()`):

```cpp
#include "query/qcsqlinsert.h"
#include "query/qcsqlupdate.h"
#include "query/qcsqldelete.h"

QcSqlInsert insert;
insert.into("users").set("id", 1LL).set("login", std::string("alice"));
// INSERT INTO users (id, login) VALUES ($1, $2)

QcSqlUpdate update;
update.table("users").set("login", std::string("alice2"));
update.where("id").isEqualTo(1LL);
// UPDATE users SET login = $1 WHERE id = $2

QcSqlDelete del;
del.from("users");
del.where("id").isEqualTo(1LL);
// DELETE FROM users WHERE id = $1
```

`set()` accumulates `column = value` pairs in call order; a repeated `set()` on
an already-set column overwrites the value in place, rather than adding
a duplicate to the column list — convenient when reusing a single builder in
a loop for inserting/updating many rows. The WHERE side of `QcSqlUpdate`/
`QcSqlDelete` — the same `QcSqlQueryElement` and the same set of comparators
(`isEqualTo`/`isLike`/`isIn`/`isBetween`/...) as in `QcSqlQuery::where()`
from section 1, including AND/OR/parentheses sugar grouping
(`and_OpenParenthesis`/`or_OpenParenthesis`/`openParenthesis`/
`closeParenthesis`). None of the three supports `JOIN`/subquery in the `FROM` role
— only the table name (`into`/`table`/`from`) and, for `UPDATE`/`DELETE`,
`WHERE` subqueries via `isIn(subQuery)`/`exists(subQuery)`/... (section 1.4).

Like `QcSqlQuery`, all three render via `toSql()` → `QcSqlStatement{sql,
params}` — the same contract that `QcNativeConnection::execute()` expects (see
section 5):

```cpp
const QcSqlStatement statement = insert.toSql();
auto result = lease.connection().execute(statement.sql, statement.params);
```

Multi-row insert (a single `VALUES` with multiple groups) is not
supported — `QcSqlInsert` builds exactly one row per `toSql()` call; for
multiple rows call `toSql()`/`execute()` in a loop (as in
`tests/test_integration_dml.cpp`).

### 2.1. `returning(columns)` — PostgreSQL / SQLite / MSSQL / Oracle

Available on all three builders, actually works on four out of five
drivers:

```cpp
QcSqlInsert insert;
insert.into("users").set("login", std::string("alice")).returning({"id"});
// PostgreSQL/SQLite: INSERT INTO users (login) VALUES ($1) RETURNING id
// MSSQL:              INSERT INTO users (login) OUTPUT inserted.id VALUES (?)
// Oracle:              INSERT INTO users (login) VALUES (:1) RETURNING id INTO :2

const QcSqlStatement statement = insert.toSql();
// executeReturning(), not execute() — on PostgreSQL/SQLite/MSSQL this is
// behaviorally the same as execute() (RETURNING/OUTPUT already arrive as
// regular result rows), but Oracle's "RETURNING ... INTO" reads
// values from OUT bind variables, not from result rows — see below.
auto result = lease.connection().executeReturning(statement.sql, statement.params, statement.returningColumnCount);
if (result && !result->empty()) {
    const QcSqlBase::QcVariant & insertedId = (*result)[0][0];
}
```

Oracle does not give values back as regular result rows — `RETURNING
col INTO :N` binds them through OUT bind variables, a separate execution
contract from "more rows in the result" — therefore reading them back requires
[`QcNativeConnection::executeReturning(sql, params,
returningColumnCount)`](architecture.md#returning-on-oracle-and-mysql),
not plain `execute()`; `returningColumnCount` is
`QcSqlStatement::returningColumnCount`, which `toSql()` fills in
automatically when `returning()` was called and the active driver is Oracle
(`0` on all others, where `executeReturning()` with `count=0` — exactly the same
as `execute()`). `QueryCreator::execute(insert/update/delete)` already
does this automatically — when working through the facade, there is no need to think about
`executeReturning()` explicitly, only in the manual path via `QcNativeConnection`.

**Oracle path limitation**: OUT binds here are scalar, not array —
designed for a statement that actually touches a single row (the usual case for
`INSERT`, and for `UPDATE`/`DELETE` with a PK-like `WHERE`). `UPDATE`/`DELETE` with
`returning()` that actually affects more than one row on Oracle will return
`nullopt` (cleanly, without side effects — the DML itself is not committed) —
not "first row silently", but an honest failure; for a multi-row variant,
a fundamentally different, piecewise/dynamic OCI mechanism would be needed,
not implemented here (see
[architecture.md](architecture.md#returning-on-oracle-and-mysql)).

### 2.2. `returning(columns, autoIncrementColumn)` — emulation on MySQL

On MySQL, `returning(columns)` (without the second argument) still **silently
produces no clause** — there is no equivalent of `RETURNING`/`OUTPUT` at all in
any form at the single-statement level. For `INSERT`, there is a separate
workaround made specifically for MySQL:

```cpp
QcSqlInsert insert;
insert.into("users").set("login", std::string("alice")).returning({"id", "login"}, "id");
// QueryCreator::execute(insert) on MySQL runs: BEGIN; the INSERT itself;
// SELECT id, login FROM users WHERE id = LAST_INSERT_ID(); COMMIT; --
// and returns the result of this SELECT, as if it were RETURNING.
```

`autoIncrementColumn` — the name of the AUTO_INCREMENT PK column of that table; there is
no way to infer it from here, so it must be specified. Works **only through
`QueryCreator`** (orchestration of multiple statements on the same
connection is needed — plain `QcNativeConnection::execute()` does not do this) and
**only for `INSERT`** — `LAST_INSERT_ID()` is only meaningful for the just-
inserted row; for `UPDATE`/`DELETE` there is no analogue (there, `returning()`
on MySQL remains without effect, as before). Strictly speaking, this is not a true
atomic `RETURNING` (values arrive in a second statement that reads the row
again, not from the `INSERT` itself), but it is wrapped in a transaction — one's own insert
in the same transaction is always visible to one's own subsequent queries, regardless
of concurrent activity from other sessions. It is not safe to call if the
calling code itself already holds an explicit transaction on the same connection —
MySQL has no nested transactions, the inner `BEGIN` will implicitly commit what
was opened externally. Details —
[architecture.md](architecture.md#returning-on-oracle-and-mysql).

## 3. Dialects and driver selection

Which of the five native client libraries are actually linked into a given
build is decided at CMake time, via the cache variable `QC_DB_DRIVERS`
(`PostgreSQL` by default; list separated by `;`/`,` or `All` — all five; see
[CMakeLists.txt](../CMakeLists.txt)). Which of the compiled drivers to
use at runtime — decided by the value of `QcConnectionParams::driver`
(`enum class QcDbDriver { PostgreSQL, Oracle, MySQL, SQLite, MSSQL }`, see
[qcdbdriver.h](../src/query/qcdbdriver.h)) when creating a connection; an attempt
to create `QcNativeConnection`/`QueryCreator` with a driver that was not
compiled in this build throws an exception.

SQL generation (placeholders `$1`/`:1`/`?`, type names for `CAST(...)`,
`LIMIT`/`OFFSET` syntax, identifier quoting and everything else
described in sections 1-2) follows a separate, also runtime-driven choice —
the dialect, which each builder (`QcSqlQuery`/`QcSqlInsert`/`QcSqlUpdate`/
`QcSqlDelete`) stores internally:

```cpp
QcSqlQuery query;
query.useDriver(QcDbDriver::MySQL);   // set once, before all toSql() calls
// ...
const QcSqlStatement statement = query.toSql(); // renders for MySQL

// or explicitly per call, without touching useDriver():
const QcSqlStatement pgStatement = query.toSql(QcDbDriver::PostgreSQL);
```

`useDriver()` was never called — defaults to PostgreSQL. The explicit
`toSql(driver)` argument is convenient when the same query needs to be
rendered for multiple drivers at once (tests) or once — for a
specific use case without modifying the builder's state. When working through
`QueryCreator` (section 6), the dialect is set automatically, from the
`QcConnectionParams` the `QueryCreator` was created with — no need to call
`useDriver()` yourself.

## 4. Connecting to a database

Actual DB work goes through the lower level — `QcConnectionParams` +
`QcNativeConnection` + `QcConnectionPool` (implemented and covered by tests for
all five drivers, selected at runtime via `QcConnectionParams::driver`
— see [testing.md](testing.md)).

### 4.1. Single connection, without a pool

The simplest way — open a single connection directly via
`QcNativeConnection`. Useful for scripts/utilities where a pool is not needed:

```cpp
#include "query/qcnativeconnection.h"

QcConnectionParams params;
params.host = "127.0.0.1";
params.port = "5432";
params.database = "demo";
params.user = "demo";
params.password = "demo";
params.connectTimeoutSeconds = 5; // 0 (default) = driver decides

QcNativeConnection connection(params); // throws std::runtime_error if unable to connect

auto result = connection.execute("SELECT id, name FROM groups WHERE id = $1", {1LL});
if (result) {
    for (const auto & row : *result) {
        // row[i] — QcSqlBase::QcVariant; cell typing depends on the driver:
        // PostgreSQL/Oracle return non-null cells as std::string (text
        // protocol/SQLT_STR), SQLite/MSSQL/MySQL — typed
        // (long long/double/std::string/std::vector<std::byte> according to the actual
        // column type), see the doc-comment for QcNativeConnection::execute().
    }
}
```

### 4.2. Connection pool (typical case for a service)

For an application that accesses the DB from multiple threads,
`QcConnectionPool` is needed — it also supports both "permanent" connection mode and
"connect-execute-disconnect" mode:

```cpp
#include "query/qcconnectionpool.h"

QcConnectionParams params;
params.host = "127.0.0.1";
params.port = "5432";
params.database = "demo";
params.user = "demo";
params.password = "demo";

// Permanent: size connections are opened once when the pool is created and
// reused. A dead (e.g. severed by the server) connection is
// transparently reopened on the next acquire() — the calling code
// never receives a known-broken connection.
QcConnectionPool pool(params, QcConnectionPool::Mode::Permanent, /*size=*/4);

{
    auto lease = pool.acquire(); // blocks if all 4 connections are busy
    auto result = lease.connection().execute("SELECT 1");
} // connection is automatically returned to the pool here (RAII, Lease class)

// tryAcquire — the same, but does not wait indefinitely.
if (auto lease = pool.tryAcquire(std::chrono::milliseconds(200))) {
    lease->connection().execute("SELECT 1");
} else {
    // pool exhausted within the allotted time
}
```

The second mode — `OnDemand`: a connection is opened on each `acquire()` and
closed upon return to the pool; `size` in this mode limits not
the number of stored connections, but how many can be open simultaneously:

```cpp
QcConnectionPool onDemand(params, QcConnectionPool::Mode::OnDemand, /*size=*/8);

{
    auto lease = onDemand.acquire(); // opens a new connection right now
    lease.connection().execute("SELECT 1");
} // connection is closed here, not returned to the pool
```

`acquire()`/`tryAcquire()` can be called from any number of threads simultaneously —
the pool is thread-safe (`std::mutex`/`std::condition_variable`, tested under
ThreadSanitizer). The same connection obtained via `Lease`
must not be used from two threads simultaneously — as with any API on top of
a socket.

## 5. SQL generation and linking to execution

`QcSqlQuery::toSql()`/`QcSqlInsert::toSql()`/... turn the built
query into `QcSqlStatement{sql, params}` — exactly the contract (raw SQL text
+ data for substitution) that `QcNativeConnection::execute()`/
`Lease::connection().execute()` expect:

```cpp
const QcSqlStatement statement = query.toSql(); // query from section 1

auto result = lease.connection().execute(statement.sql, statement.params);
```

## 6. `QueryCreator` — facade: render + execution in a single call

`QueryCreator` collapses the "`toSql()` → `execute()`" pairing from section 5 into
a single call. It owns its own `QcConnectionPool` (created along with
`QueryCreator`, from the passed `QcConnectionParams`, defaulting to
`Permanent`/`size=1` — the second and third constructor arguments override
the mode/size):

```cpp
#include "query/querycreator.h"

QcConnectionParams params;
params.host = "127.0.0.1";
params.port = "5432";
params.database = "demo";
params.user = "demo";
params.password = "demo";

QueryCreator qc(params); // owns a QcConnectionPool (Permanent, size=1 by default)

auto result = qc.execute(query); // renders query.toSql() and executes in one call
```

The same `execute()` overloads accept `QcSqlInsert`/`QcSqlUpdate`/
`QcSqlDelete` directly (and correctly handle their `returning()` — Oracle
via `executeReturning()`, MySQL emulation via `LAST_INSERT_ID()`, see
sections 2.1/2.2 — without needing to think about it on the calling side). For
SQL that the builders do not express (DDL, transactions, computed expressions —
see section 8), there is an `execute(sql, params)` overload — the same contract as
`QcNativeConnection::execute()`. The manual pairing via
`lease.connection().execute(statement.sql, statement.params)` still
works and remains needed where explicit control over a specific
`Lease` matters (e.g. multiple operations within a single connection lease, or
a transaction spanning several distinct statements).

## 7. Named access to results

Each `execute()` overload — `QueryCreator::execute()` (section 6) and
`QcNativeConnection::execute()`/`executeReturning()` (sections 4-5) — has
a named twin — `executeNamed()`/`executeReturningNamed()` — which
returns rows as `map<string, QcVariant>` (column name → value) instead of
a positional `vector`, using `row.at("id")`/
`row.at("name")` instead of `row[0]`/`row[1]`:

```cpp
QcSqlQuery query;
query.fromTable("employees");
query.addReturnValues({"id", "full_name <name>"}); // alias via "<...>"
query.where("department_id").isEqualTo(3LL);

auto result = qc.executeNamed(query); // QueryCreator, same qc as in section 6

if (result) {
    for (const QcNamedRow & row : *result) {
        // row.at("id"), row.at("name") -- "name", not "full_name": the key
        // is taken from the metadata of the already rendered SQL, meaning it is the alias
        // if one was given, otherwise — the column itself
    }
}
```

`execute()`/`executeReturning()` are unchanged by the introduction of this API —
`executeNamed()` is not a replacement, but a parallel way to read the same data.
The manual pairing via `lease.connection().executeNamed(sql, params)` is also
available, on the same principle as in section 8.

Identically named columns (self-join or an ambiguous `JOIN` without aliases, see
section 1.2) — not an error, but both values are not preserved: only
the last one in `SELECT` list order will remain in the map. If the columns need to be distinguished —
alias them (`"e.id <emp_id>"`, `"m.id <manager_id>"`), as shown above;
without an alias, the ambiguity is resolved silently, not diagnosed.

## 8. Executing SQL manually

The same contract (**raw SQL text + data to substitute into it**)
can be populated manually too, not through the builders — for example, for DDL, transactions
(`BEGIN`/`COMMIT`/`ROLLBACK`) or queries where the right-hand side of an assignment is
a computed SQL expression referencing the same column (`SET metadata =
jsonb_set(metadata, ...)`), rather than `column = value`, which is not what
`QcSqlUpdate::set()` models:

```cpp
auto result = lease.connection().execute(
    "UPDATE groups SET metadata = jsonb_set(metadata, '{tag}', $1::jsonb) WHERE id = $2",
    {std::string("\"admin\""), 1LL});

if (result) {
    // empty row set — normal result for UPDATE without RETURNING
}
```

- Parameters are truly bound (not string concatenation) — standard
  SQL injection protection; the specific mechanism depends on the driver
  (`PQexecParams` on PostgreSQL, `mysql_stmt_bind_param` on MySQL,
  `sqlite3_bind_*` on SQLite, `SQLBindParameter` on MSSQL, `OCIBindByPos` on
  Oracle — see [architecture.md](architecture.md)).
- `execute()` returns `std::optional<QcResultSet>`: `std::nullopt` —
  execution error; empty row set — the command executed but returned no
  rows (`INSERT`/`UPDATE`/`DELETE` without `RETURNING`, DDL); non-empty —
  result rows.
- `NULL` is passed as a parameter via `QcSqlBase::QcVariant{}` (the default
  value — `std::monostate`) and comes back the same way.
- Typing of non-null result cells depends on the driver — see section 4.1
  above: PostgreSQL/Oracle always `std::string`, SQLite/MSSQL/MySQL —
  typed according to the actual column type.
- `QueryCreator::execute(sql, params)`/`executeNamed(sql, params)` — the same
  contract, but through the pool `QueryCreator` owns itself (section 6), without manual
  `pool.acquire()`.

## 9. Identifier quoting

Table/column/alias names that the builder substitutes into SQL text itself
(`fromTable`/`addReturnValue`/`where`/`groupBy`/`orderAsc`/`returning`/...)
are quoted automatically for the active dialect — `"col"` on
PostgreSQL/SQLite/Oracle, `` `col` `` on MySQL, `[col]` on MSSQL
(`QcSqlDialect::quoteIdentifier()`/`quoteRef()`/`quoteTableRef()`, see
[architecture.md](architecture.md#sql-generation--qcsqldialect)
for the full rationale and the Oracle case-sensitivity nuance). Quoting is
**conservative**: a string is quoted only if it looks like a simple
identifier (`[A-Za-z_][A-Za-z0-9_$]*`, with dots for `table.column`, or
`*`) — otherwise it is passed into the SQL text unchanged. This is deliberate, not
an oversight: a "column name" in this API is also the only way to reach
SQL for which there is no dedicated method (aggregate expressions
(`"COUNT(*)"`), JSON operators (`"metadata->>'level'"`), etc. are regularly
passed through `where()`/`having()`/`addReturnValue()`, see section 1), and
partially/incorrectly quoting such a string would break it —
it is safer to leave it untouched.

Strings that the builder **never** quotes, because they are explicitly
documented as an escape hatch for raw SQL rather than an identifier name:
`JOIN` `onCondition` (section 1.2), `addFreeText()`'s `text` (section 1.4), and
function operands that take "ready SQL expression or literal" —
`concat()`/`coalesce()`/`nullIf()`/`replace()`/`case_()` (section 1.8) —
there, only the part of the operand that looks like a simple
identifier is quoted, following the same rule as `quoteRef()` above.

## Known limitations

- Multi-row `INSERT` (a single `VALUES` with multiple groups) is not
  supported — see section 2.
- `returning()` on MySQL works only for `INSERT` and only through
  `QueryCreator` — see section 2.2. For `UPDATE`/`DELETE` on MySQL, the clause
  is always empty.
- `returning()` on Oracle for `UPDATE`/`DELETE` that actually affects
  more than one row returns `nullopt` (not a partial result) — see
  section 2.1.
- `distinctOn()` — PostgreSQL only; on the other four, silently degrades
  to plain `DISTINCT` — see section 1.6.
- `limit(..., withTies=true)` on SQLite renders syntax that will fail
  at execution — SQLite has no `WITH TIES` in any form — see section
  1.6.
- `addFullJoin()` on MySQL renders `FULL JOIN`, which MySQL does not
  support — see section 1.2.
- Named results (`executeNamed()`/`QcNamedRow`) lose all but
  the last identically named column — see section 7.
