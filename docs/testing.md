# queryCreator — testing

## Requirement

**Any new functionality must be covered by a test in the same change in
which it is added.** The full statement of the rule and the "test needed
now or not yet" criteria — in [`CLAUDE.md`](../CLAUDE.md) at the repo root.
Here — the technical setup of the test harness: how it's built, how to run tests,
which files cover what, and what to watch for when adding new ones.

## Harness setup

- [`tests/CMakeLists.txt`](../tests/CMakeLists.txt) pulls in **GoogleTest**
  (`v1.16.0`) via CMake `FetchContent`, builds the `queryCreatorTests`
  binary, links it to `queryCreatorLib` and registers tests with
  `ctest` via `gtest_discover_tests()`.
- The root [`CMakeLists.txt`](../CMakeLists.txt) includes `tests/` via
  `include(CTest)` + `if(BUILD_TESTING) add_subdirectory(tests) endif()` —
  disabled by `-DBUILD_TESTING=OFF`.
- Running:
  ```bash
  cmake --preset postgresql   # or mysql/mssql/sqlite/oracle/all
  cmake --build --preset postgresql
  ctest --preset postgresql --output-on-failure
  ```
  or without presets: `cmake -S . -B build -DQC_DB_DRIVERS=PostgreSQL &&
  cmake --build build && ctest --test-dir build --output-on-failure`.

## Driver selection in tests

`QC_DB_DRIVERS` (see [CMakeLists.txt](../CMakeLists.txt)) decides which of the
five native clients are compiled into the test binary — semicolon-separated
list, or `All`. Which of the compiled-in drivers to actually use —
runtime selection, exactly as in the library itself (see
[architecture.md](architecture.md#native-db-drivers-two-independent-decisions)).
[`tests/testdbconfig.h`/`.cpp`](../tests/testdbconfig.cpp) gives test
code two concepts on top of this:

- **`compiledInDrivers()`** — which of the five are compiled into this binary (by
  `QC_DB_HAS_POSTGRESQL`/`QC_DB_HAS_ORACLE`/... — not mutually exclusive,
  all five may be set at once).
- **`primaryTestDriver()`** — the first from `compiledInDrivers()` in a fixed
  order (PostgreSQL → Oracle → MySQL → SQLite → MSSQL) — the single
  driver against which most tests run that are oriented toward
  one driver at a time (connection pool, integration suites). Multi-driver
  coverage (proof that more than one driver actually works from
  a single binary) — in tests that explicitly iterate over
  `compiledInDrivers()` (`test_qcsqldialect.cpp`,
  `CompiledInDrivers*` tests in `test_qcconnectionpool.cpp`).

Tests that need dialect-dependent behavior (skip a scenario
unavailable on a particular driver, or directly verify a dialect branch)
do so via runtime `if (primaryTestDriver() == QcDbDriver::MySQL) {
GTEST_SKIP() << "..."; }` / `switch (primaryTestDriver())` — not via
`#if defined(QC_DB_*)`: production code itself selects the dialect at runtime
(`switch (driver)` inside `QcSqlDialect`, not `#ifdef`), all five
dialect branches are always compiled, and tests that compare
`toSql()` text for a specific driver also have no reason to select a branch
at compile time. Where the `toSql()` text itself is compared
(`test_qcsqldialect.cpp`/`test_qcsqlqueryvalue.cpp` and portions of
`test_qcsqlqueryelement.cpp`/`test_qcsqlquery.cpp`/
`test_qcsqlinsert.cpp`/`test_qcsqlupdate.cpp`/`test_qcsqldelete.cpp`) — an explicit
`for (QcDbDriver driver : {...})` loop, verifying rendering under all five
drivers in one run.

Builders (`QcSqlQuery`/`QcSqlInsert`/`QcSqlUpdate`/`QcSqlDelete`/
`QcSqlQueryValue`/`QcSqlQueryElement`) themselves provide `useDriver(QcDbDriver)` —
set once, `toSql()` without an argument renders for it; explicit
`toSql(QcDbDriver)` — the primary tool for multi-driver test
loops (compare the same builder under different drivers without rebuilding
the object each time).

**Connection data** — [`tests/db_config.ini`](../tests/db_config.ini)
(gitignored, real credentials never committed). Template with all five
drivers — [`tests/db_config.ini.example`](../tests/db_config.ini.example):
copy to `tests/db_config.ini` and fill in the required section
(`[postgresql]`/`[sqlite]`/`[mysql]`/`[mssql]`/`[oracle]`). Absence of the file
or section — not an error: `testDbConfigOrDefault(driver)` falls back to
hardcoded local defaults (`demo`/`demo`/`demo`@`127.0.0.1:5432` for
PostgreSQL, shared-cache in-memory URI for SQLite, ...), so the harness
works "out of the box" without a file for what doesn't require a real network.

## File map

### Builder and SQL generation (unit tests, without a real DB)

| File | Covers |
|---|---|
| [`test_qcsqlqueryvalue.cpp`](../tests/test_qcsqlqueryvalue.cpp) | `QcSqlQueryValue`: alias-parsing constructor, all 19 chain-methods (state + `toSql()`), dialect branches where the form actually differs (`concat`/`length`/`substring`/`extract`/`dateAdd`), `jsonExtract`/`jsonExtractNumber`/`jsonExtractRaw` (state + `toSql()` in a loop for all five drivers + chain continuation after) |
| [`test_qcsqlqueryelement.cpp`](../tests/test_qcsqlqueryelement.cpp) | `QcSqlQueryElement`: all constructors, all comparators by value and by subquery, state reset between consecutive calls, deep copy with subquery, `toSql()` (text **and** actual bind values), `renderChain()` directly, all 18 `isXxxJson...()`-comparators (state + `toSql()` in a loop for all five drivers, including interaction of `isIlikeJsonText()`/`isNotILikeJsonText()` with the `ILIKE`→`LOWER()` dialect degradation), `isIncomplete()`/`validateChain()` (the shared completeness check every WHERE/HAVING-chain owner's `validate()` below is built on) |
| [`test_qcsqlquery.cpp`](../tests/test_qcsqlquery.cpp) | `QcSqlQuery`: `from`/`join`/`with_`/`where`-chains with `and_`/`or_`/parentheses/`having`/`orderBy`(including `NULLS FIRST`/`LAST`, native and emulated)/`groupBy`/`limit`/set-operations, deep copy, `toSql()` for each construct, end-to-end global placeholder numbering through JOIN-subquery + WHERE + set-operation, `validate()`/`toSql()`-throws-`QcQueryBuildError` for a non-CROSS `JOIN` with no `ON`, an unclosed `openParenthesis()`, an incomplete WHERE/HAVING condition, and a problem inside a nested `FROM`-subquery |
| [`test_qcsqlinsert.cpp`](../tests/test_qcsqlinsert.cpp) | `QcSqlInsert`: `QcVariant` types one per alternative + mixed, large payloads, overwrite-in-place `set()`, `returning()` (both overloads, form and position by driver), `validate()`/`toSql()`-throws-`QcQueryBuildError` when `into()` was never called (including with `returning()` set) |
| [`test_qcsqlupdate.cpp`](../tests/test_qcsqlupdate.cpp) | `QcSqlUpdate`: SET-list, WHERE-chain (via the same `QcSqlQueryElement`), `returning()`, `validate()`/`toSql()`-throws-`QcQueryBuildError` for no target table/unclosed parenthesis/incomplete WHERE condition, confirming a WHERE-less `UPDATE` (every row, on purpose) is *not* flagged |
| [`test_qcsqldelete.cpp`](../tests/test_qcsqldelete.cpp) | `QcSqlDelete`: WHERE-chain, `returning()`, `validate()`/`toSql()`-throws-`QcQueryBuildError` for no target table/unclosed parenthesis/incomplete WHERE condition, confirming a WHERE-less `DELETE` is *not* flagged |
| [`test_qcsqldialect.cpp`](../tests/test_qcsqldialect.cpp) | `QcSqlDialect` directly: `placeholder`/`dataTypeName`/`limitOffsetClause`/`concatExpr`/`returningClause`/`quoteIdentifier`/`quoteRef`/`quoteTableRef`/`lengthExpr`/`substringExpr`/`extractExpr`/`dateAddExpr`/`orderByEntry`/`jsonExtractExpr` (all three `kind`, nested path with array index, path with leading array index), in a loop for all five drivers |

All six query builder classes expose no public getters — where
`toSql()` doesn't provide enough observability, tests use friend
fixtures (`…WhiteBoxTest`, declared as `friend class` right in the header) — see
[architecture.md](architecture.md#white-boxes-for-tests-instead-of-public-getters).

### Lower level and facade (integration, against a real DB)

| File | Covers |
|---|---|
| [`test_qcconnectionpool.cpp`](../tests/test_qcconnectionpool.cpp) | `QcNativeConnection`/`QcConnectionPool`: both pool modes, multithreading (also tested under ThreadSanitizer), parameter binding + result parsing, `NULL`, connection and pool-wait timeouts, reconnect after actual connection drop (`pg_terminate_backend()` on PostgreSQL), verification that requesting a driver outside `compiledInDrivers()` honestly throws |
| [`test_querycreator.cpp`](../tests/test_querycreator.cpp) | `QueryCreator`: `INSERT`→`SELECT`→`UPDATE`→`SELECT`→`DELETE`→`SELECT` through each `execute()`/`executeNamed()` overload, raw `execute(sql, params)`, `nullopt` on invalid SQL, `std::runtime_error` from constructor when DB is unreachable, `RETURNING` through the facade (including Oracle OUT-binds and MySQL `LAST_INSERT_ID()`-emulation) |
| [`test_integration_select.cpp`](../tests/test_integration_select.cpp) | The entire `SELECT`-functionality of `QcSqlQuery`, built through the public fluent-API and actually executed: all WHERE comparators and their AND/OR/parenthesized combinations, all 5 JOIN types (including self-join and `JOIN` as subquery), `fromSubQuery`/`isIn(subQuery)`/correlated `EXISTS`/`NOT EXISTS`, `WITH`-CTE, all 4 set-operations, `GROUP BY`/`HAVING`/aggregates, `ORDER BY` (including `NULLS FIRST`/`NULLS LAST`, natively on PostgreSQL/SQLite/Oracle and emulated on MySQL/MSSQL), `LIMIT`/`OFFSET` (including page-by-page traversal of the entire table, verifying no gaps/duplicates between pages), `DISTINCT`/`DISTINCT ON`, all `QcSqlQueryValue` functions, JSON operators via `addFreeText`/separate dialect branches (three "historical" tests, still alive as a raw-SQL-path demo), **all 18 `isXxxJson...()`-comparators and all 3 `jsonExtract...()`** against real `qc_bt_employees.metadata` data — identical C++ without dialect branches, plus separate tests for multi-level paths (`"a.b[1].c"`) via JSON literals, `addFreeText`, unicode/apostrophes in values, `executeNamed()` |
| [`test_integration_dml.cpp`](../tests/test_integration_dml.cpp) | `INSERT`/`UPDATE`/`DELETE` via `QcSqlInsert`/`QcSqlUpdate`/`QcSqlDelete`: `NULL` into a nullable column and back, `NOT NULL`/`UNIQUE` violations, `BEGIN`/`COMMIT`/`ROLLBACK`, large (megabyte-scale) text/binary data with byte-by-byte verification, binary data with embedded `\0`, JSON modifications, SQL injection as a parameter (not as concatenation — the table is unharmed), `returning()`/`executeReturningNamed()`, end-to-end insert→update→delete on many rows |

Both integration files (like `test_qcconnectionpool.cpp`) themselves
`GTEST_SKIP()` the entire suite in `SetUpTestSuite()` if the DB from
`testDbConfigOrDefault()` is unreachable — they don't fail a run on a machine/CI
without a local DB for the specific driver.

[`tests/testintegrationsupport.h`/`.cpp`](../tests/testintegrationsupport.h)
— common utilities for both integration files: `asDouble`/`asInt64`/
`asBool`/`asBytes` (normalize what different drivers return differently
— see [architecture.md](architecture.md#connection-pool-and-thread-safety)
for per-driver cell typing), `placeholderList()` (builds a placeholder
list via the same `QcSqlDialect::placeholder()` that the builder itself uses —
for raw DML fragments that the builder cannot express), `generateRandomText`/
`generateRandomBytes` (deterministic by seed).

## How integration test data is generated

`SetUpTestSuite()` in `test_integration_select.cpp` builds the schema
(`qc_bt_departments`/`qc_bt_employees`/`qc_bt_notes` — foreign keys,
`manager_id` self-reference for self-join, numeric/date/JSON/nullable
columns) and seeds it with a hybrid dataset: a few manually specified
"anchor" rows with precisely known values (for tests needing an
exact number/string) plus a substantially larger procedurally generated
set with a fixed seed (`std::mt19937_64`, reproducible on failure) —
for tests that need volume (pagination, `GROUP BY`, large `IN`-list).
Each test compares the SQL query result not to a hardcoded number, but to
the same predicate/aggregate computed on the same in-memory dataset in
pure C++ — so the test remains correct regardless of what the RNG
generated, and a failure genuinely means a bug in SQL, not a
random (mis)match with hardcoded values.

## Dialect limitations to watch for when writing tests

Real, verified against live servers differences between drivers —
the source of most bugs found specifically by the integration run (not
by unit tests or compilation). When adding a test that touches one
of the following mechanisms, verify behavior on all compiled-in
drivers, not just PostgreSQL:

- **`RETURNING`/`OUTPUT`** — absent on MySQL in any form entirely (except
  `INSERT`-emulation via `QueryCreator`, see
  [architecture.md](architecture.md#returning-on-oracle-and-mysql)). On
  Oracle — only for statements actually affecting one row
  (scalar OUT-bind); the multi-row case honestly returns `nullopt`,
  not committing the change.
- **`FULL JOIN`** — not supported on MySQL in any form, fails at
  execution.
- **`DISTINCT ON`** — exists only on PostgreSQL, silently
  degrades to `DISTINCT` on the other four.
- **`LIMIT ... WITH TIES`** — has no equivalent on SQLite, fails at
  execution (documented, not silent degradation).
- **`IS DISTINCT FROM`** — natively only on PostgreSQL/SQLite; on Oracle/
  MSSQL manually expanded via `=`/`IS NULL`, MySQL — via `<=>`; on
  MSSQL the operand-value is bound multiple times for one logical
  value (positional `?` is not reused, unlike named
  `:N` on Oracle).
- **Identifier quoting** — Oracle quoting is case-preserving,
  like PostgreSQL/SQLite, but Oracle folds *unquoted* identifiers to
  uppercase at creation time (PostgreSQL/SQLite — to lowercase) — DDL
  for the test schema on Oracle must itself be quoted in the same case
  as the generated SQL, otherwise `ORA-00942`/`ORA-00904`.
- **Modulo** — `%` is not an arithmetic operator in Oracle SQL, need `MOD(a,
  b)`.
- **Transactions** — literal `"BEGIN"` does not mean the same thing on all
  five: PostgreSQL/SQLite/MySQL accept it as-is (MySQL — only
  via the legacy text protocol, not prepared statement), MSSQL requires
  `"BEGIN TRANSACTION"`, Oracle does not accept it at all (a transaction is
  always implicitly open — the literal is intercepted at the driver level as
  a flag, not sent to the server).
- **`utf8mb4`/`AL32UTF8`** — both MySQL and Oracle require explicit
  client charset setup at connection time, otherwise multibyte text is silently
  corrupted regardless of the DB's own charset; PostgreSQL/SQLite/MSSQL do
  not have this problem.

Full breakdown of each driver — in the ["Driver
PostgreSQL"](architecture.md#postgresql-driver) sections and onward in
[architecture.md](architecture.md).

## Other things to watch for

- **Evaluation order when accumulating bind parameters.** Any expression
  of the form `bind(a) + " AND " + bind(b)` in a single statement is dangerous —
  evaluation order of `+` operands/function arguments in C++ is unspecified, and
  `bind()` mutates the shared `params` as a side effect. Every `bind()`
  must receive its own string variable before assembling the
  result. Tests verifying such places must check not only
  the final SQL text, but also the actual values in `params` — only this
  reliably catches swapped operand order.
- **`SetUp()`/`SetUpTestSuite()` for connections to shared-cache in-memory
  SQLite.** Such a DB (`file:...?mode=memory&cache=shared`) is destroyed
  as soon as the last open connection to it is closed — a temporary
  connection living only inside `SetUp()` (not `SetUpTestSuite()`)
  will take the just-created schema with it before the test body has a chance
  to open its own. On PostgreSQL/MSSQL/Oracle/MySQL (the DB lives on a server, not
  in process memory) this does not manifest — noticeable only on SQLite.
- **`target_compile_definitions` for dialect macros — `PUBLIC`, not
  `PRIVATE`.** `QC_DB_HAS_*` must be visible in translation units
  of `tests/`, otherwise code branching on them at compile time will either not
  build correctly, or (for `#if/#elif` without `#else`) silently
  produce an empty test body — GoogleTest trivially "passes" a test without
  a single `EXPECT_*`. Already configured in the root `CMakeLists.txt`
  (`target_compile_definitions(queryCreatorLib PUBLIC ...)`) — important not
  to break on build refactoring.
- **Regression — not just new tests.** A full `ctest` run on each
  affected driver — a mandatory part of verification for changes in common
  code (`QcSqlDialect`, `qcnativeconnection.cpp`, `QcConnectionPool`), not
  just for explicitly new tests of a specific feature.
