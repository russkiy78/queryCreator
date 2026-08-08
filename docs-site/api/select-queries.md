# SELECT Queries (`QcSqlQuery`)

`QcSqlQuery` is the SELECT statement builder. It constructs SQL through fluent
method chains and renders to `QcSqlStatement{sql, params}` via `toSql()`.

## Basic Usage

```cpp
#include "query/qcsqlquery.h"

QcSqlQuery query;

query.fromTable("users");
query.addReturnValues({"id", "name <display_name>"});
query.where("id").isEqualTo(5LL);
query.orderDesc({"id"});
query.limit(20);

const QcSqlStatement stmt = query.toSql();
// stmt.sql  = SELECT "id", "name" AS "display_name" FROM "users" WHERE "id" = $1 ORDER BY "id" DESC LIMIT 20
// stmt.params = [5]
```

If `addReturnValues()` is never called, the query renders as `SELECT *`.

## `FROM`: Table, Alias, Subquery

### Plain Table

```cpp
query.fromTable("users");          // FROM users
query.fromTable("employees e");    // FROM employees AS e (alias via whitespace)
```

### Subquery in FROM

```cpp
QcSqlQuery deptStats;
deptStats.fromTable("employees");
deptStats.addReturnValue("department_id");
deptStats.addReturnValue("salary <avg_salary>").avg();
deptStats.groupBy({"department_id"});

QcSqlQuery outer;
outer.fromSubQuery("dept_stats", deptStats);
// FROM (SELECT ...) AS dept_stats
```

`fromTable()` and `fromSubQuery()` are mutually exclusive — FROM has exactly
one source. Calling either overwrites the previous.

## JOINs

Five join types available, each taking an alias, a `QcSqlQuery` source, and
a raw `onCondition` string (not auto-quoted — write it as it should appear
in the final SQL):

```cpp
QcSqlQuery deptRef;
deptRef.fromTable("departments");

QcSqlQuery query;
query.fromTable("employees e");

// INNER JOIN
query.addJoin("d", deptRef, "d.id = e.department_id");

// LEFT JOIN
query.addLeftJoin("d", deptRef, "d.id = e.department_id");

// RIGHT JOIN
query.addRightJoin("d", deptRef, "d.id = e.department_id");

// FULL JOIN (not supported on MySQL)
query.addFullJoin("d", deptRef, "d.id = e.department_id");

// CROSS JOIN (no ON clause)
query.addCrossJoin("d", deptRef);
```

### Subquery JOIN

Set `asSubQuery = true` (the last optional argument) to render the source
as `JOIN (SELECT ...) AS alias ON ...`:

```cpp
QcSqlQuery deptAvg;
deptAvg.fromTable("employees");
deptAvg.addReturnValue("department_id");
deptAvg.addReturnValue("salary <avg_salary>").avg();
deptAvg.groupBy({"department_id"});

query.addJoin("da", deptAvg, "da.department_id = e.department_id", true);
```

### Self-Join

Requires aliases on both sides — columns with identical names are
otherwise ambiguous:

```cpp
QcSqlQuery managerRef;
managerRef.fromTable("employees");

QcSqlQuery query;
query.fromTable("employees e");
query.addLeftJoin("m", managerRef, "m.id = e.manager_id");
query.addReturnValues({"m.id <manager_id>", "e.id"});
```

## CTEs (`WITH`)

```cpp
QcSqlQuery cte;
cte.fromTable("employees");
cte.addReturnValues({"id", "department_id"});
cte.where("is_active").isEqualTo(1LL);

QcSqlQuery query;
query.with_("active_emps", cte);
query.fromTable("active_emps");
query.addReturnValue("* <cnt>").count();
// WITH active_emps AS (SELECT ...) SELECT COUNT(*) AS cnt FROM active_emps
```

Multiple `with_()` calls accumulate in order. A CTE may reference only
previously-defined CTEs (standard SQL `WITH`).

## GROUP BY / HAVING

```cpp
query.addReturnValue("department_id");
query.addReturnValue("* <emp_count>").count();
query.addReturnValue("salary <avg_salary>").avg();
query.groupBy({"department_id"});
query.having("COUNT(*)").isGreaterThan(5LL);
```

`having()` / `and_Having()` / `or_Having()` accept the same
`QcSqlQueryElement` comparator vocabulary as `WHERE` (see
[WHERE Conditions](/api/where-conditions)).

## ORDER BY / LIMIT / OFFSET / DISTINCT

```cpp
query.orderAsc({"last_name", "first_name"});
query.orderDesc({"created_at"});

// NULLS FIRST / NULLS LAST (native on PostgreSQL/SQLite/Oracle,
// emulated on MySQL/MSSQL)
query.orderAsc({"manager_id"}, QcSqlQuery::_nullsFirst_);
query.orderDesc({"nickname"}, QcSqlQuery::_nullsLast_);

// LIMIT/OFFSET
query.limit(20);                       // LIMIT 20
query.limit(20, 40);                   // LIMIT 20 OFFSET 40
query.limit(20, 40, true);             // ANSI OFFSET...FETCH NEXT...WITH TIES

// DISTINCT
query.distinct();                      // SELECT DISTINCT ...
query.distinctOn({"customer_id"});     // PostgreSQL only, degrades to DISTINCT elsewhere
```

NULLS placement (`nulls` parameter) applies to **all columns** in a single
`orderAsc()`/`orderDesc()` call. To mix NULLS FIRST and NULLS LAST in one
ORDER BY, call the method once per column.

The `withTies` flag switches to ANSI `OFFSET ... FETCH NEXT ... WITH TIES`
on all drivers. SQLite has no `WITH TIES` support — requesting it there
renders syntax that will fail at execution (a documented limitation).

## Set Operations

```cpp
QcSqlQuery a;
a.fromTable("employees");
a.addReturnValues({"id"});
a.where("department_id").isEqualTo(1LL);

QcSqlQuery b;
b.fromTable("employees");
b.addReturnValues({"id"});
b.where("salary").isGreaterThan(120000.0);

a.unionWith(b);        // UNION
a.unionAllWith(b);     // UNION ALL
a.intersectWith(b);    // INTERSECT
a.exceptWith(b);       // EXCEPT
```

Set operations accumulate in order. PostgreSQL/MySQL wrap both sides in
parentheses; SQLite/MSSQL/Oracle do not (per their grammar).

## `addFreeText()` — Raw SQL Escape Hatch

For conditions the builder has no dedicated method for, write raw SQL with
generic `?` placeholders (not dialect-specific placeholders — `toSql()`
renumbers them automatically):

```cpp
query.addFreeText("metadata->>'level' = ?", {std::string("admin")});
```

## SQL Generation

```cpp
// Top-level: fresh parameter list, configured dialect
QcSqlStatement stmt = query.toSql();

// Override dialect for this call only
QcSqlStatement pgStmt = query.toSql(QcDbDriver::PostgreSQL);

// Append to shared parameter list (used internally for subqueries)
QcSqlBase::QcVariantList params;
std::string sql = query.toSql(params, QcDbDriver::MySQL);
```

## Use Driver

Set the rendering dialect once, before `toSql()`:

```cpp
QcSqlQuery query;
query.useDriver(QcDbDriver::MySQL);
// All toSql() calls now render for MySQL
```
