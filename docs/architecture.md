# queryCreator — Architecture

Description of the current project architecture: layers, DB driver design,
internal design of the query builder and SQL generation. For a practical
API guide — [usage.md](usage.md); for a line-by-line breakdown of each
file — [components.md](components.md); for the test bench design —
[testing.md](testing.md).

## Stack and build

- **Pure C++20**, no external dependencies in the library itself — the entire API
  is built on STL (`std::string`, `std::vector`, `std::variant`, `std::regex`
  etc.). `src/main.cpp` — a demo example, without GUI or event loop.
- Build via CMake (`cmake -S . -B build && cmake --build build`):
  - `queryCreatorLib` — a static library with the query building engine
    and all drivers (`src/query/*.cpp`).
  - `queryCreator` — executable (`src/main.cpp`), linked with
    `queryCreatorLib`.
  - `tests/` — `queryCreatorTests` binary on GoogleTest, pulled in via
    `FetchContent` (the only external dependency of the project, and only for
    tests), run via `ctest` from the build directory.
- Common type aliases and enums — in [`qcsqlbase.h`](../src/query/qcsqlbase.h):
  - `QcSqlBase::QcVariant` = `std::variant<std::monostate, long long, double,
    std::string, std::vector<std::byte>>` (`std::monostate` — NULL;
    `vector<std::byte>` — BLOB/bytea parameters and columns).
  - `QcVariantList` = `std::vector<QcVariant>`, `QcStringList` =
    `std::vector<std::string>`.
  - `compareTypes`/`functionTypes`/`dataTypes`/`joinTypes`/
    `setOperationTypes`/`datePartTypes`/`jsonValueKind` — the query
    builder's vocabulary; see [usage.md](usage.md) for the full list of values and
    which methods use them.
  - `QcSqlStatement{sql, params, returningColumnCount,
    mysqlReturningSelectSql, returningColumnNames}` — the result of `toSql()`
    for any builder; the last three fields are meaningful only for `returning()` on
    Oracle/MySQL (see ["RETURNING on Oracle and MySQL"](#returning-on-oracle-and-mysql)
    below).

## Project layers

```mermaid
classDiagram
    class QcSqlBase {
        <<enums: compareTypes, functionTypes, dataTypes, joinTypes, setOperationTypes, datePartTypes, jsonValueKind>>
        <<using QcVariant/QcVariantList/QcStringList>>
    }
    class QcSqlQueryValue {
        +concat()/cast()/round()/upperCase()/lowerCase()
        +count()/sum()/avg()/min()/max()
        +coalesce()/nullIf()/trim()/substring()/length()/replace()/case_()
        +extract()/dateAdd()
        +jsonExtract()/jsonExtractNumber()/jsonExtractRaw()
        +toSql() string
    }
    class QcSqlQueryElement {
        +isEqualTo()/isLike()/isIn()/isBetween()/isNull()/exists()/... bool
        +isEqualToJsonNumber()/isEqualToJsonText()/isLikeJsonText()/isLikeJsonArrayAsText()/... bool
        +toSql(params&) string
        +renderChain(chain, params)$ string
    }
    class QcSqlQuery {
        +fromTable()/fromSubQuery()/with_()
        +addLeftJoin()/addJoin()/addRightJoin()/addFullJoin()/addCrossJoin()
        +where()/and_()/or_() QcSqlQueryElement&
        +groupBy()/having()/orderAsc()/orderDesc()/limit()
        +unionWith()/unionAllWith()/intersectWith()/exceptWith()
        +toSql() QcSqlStatement
    }
    class QcSqlInsert { +into()/set()/returning() +toSql() }
    class QcSqlUpdate { +table()/set()/returning()/where() +toSql() }
    class QcSqlDelete { +from()/returning()/where() +toSql() }
    class QcDeepPtr~T~ { <<deep-copying unique_ptr~T~>> }
    class QcSqlDialect {
        <<namespace -- src/query/qcsqldialect.h/.cpp>>
        +placeholder()$ +dataTypeName()$ +limitOffsetClause()$
        +concatExpr()$ +returningClause()$
        +quoteIdentifier()$ +quoteRef()$ +quoteTableRef()$
        +lengthExpr()$ +substringExpr()$ +extractExpr()$ +dateAddExpr()$
        +orderByEntry()$ +jsonExtractExpr()$
    }
    QcSqlBase <|-- QcSqlQuery
    QcSqlBase <|-- QcSqlQueryElement
    QcSqlBase <|-- QcSqlQueryValue
    QcSqlBase <|-- QcSqlInsert
    QcSqlBase <|-- QcSqlUpdate
    QcSqlBase <|-- QcSqlDelete
    QcSqlQuery *-- QcSqlQueryValue : m_values
    QcSqlQuery *-- QcSqlQueryElement : m_whereElements/m_havingElements
    QcSqlUpdate *-- QcSqlQueryElement : m_whereElements
    QcSqlDelete *-- QcSqlQueryElement : m_whereElements
    QcSqlQuery ..> QcDeepPtr : subqueries (FROM/JOIN/CTE/set operations)
    QcSqlQueryElement ..> QcDeepPtr : subquery operand
    QcSqlDialect ..> QcSqlQuery : used by toSql()
    QcSqlDialect ..> QcSqlInsert : used by toSql()
    QcSqlDialect ..> QcSqlUpdate : used by toSql()
    QcSqlDialect ..> QcSqlDelete : used by toSql()

    class QcConnectionParams {
        +QcDbDriver driver = PostgreSQL
        +host/port/database/user/password
        +int connectTimeoutSeconds = 0
    }
    class QcNativeConnection {
        +isOpen() bool
        +isAlive() bool
        +execute(sql, params) optional~QcResultSet~
        +executeReturning(sql, params, count) optional~QcResultSet~
        +executeNamed()/executeReturningNamed() optional~QcNamedResultSet~
        +nativeDriverInfo(driver)$ string
    }
    class IQcDriverConnection {
        <<interface -- one backend per driver>>
    }
    class QcConnectionPool {
        <<Mode: Permanent | OnDemand>>
        +acquire() Lease
        +tryAcquire(timeout) optional~Lease~
    }
    class Lease {
        <<nested in QcConnectionPool, RAII>>
        +connection() QcNativeConnection&
    }
    class QueryCreator {
        <<facade, owns QcConnectionPool>>
        +execute(QcSqlQuery|Insert|Update|Delete|sql) optional~QcResultSet~
        +executeNamed(...) optional~QcNamedResultSet~
    }
    QcNativeConnection *-- IQcDriverConnection : m_impl (chosen by driver)
    QcConnectionPool o-- QcConnectionParams
    QcConnectionPool "1" *-- "size" QcNativeConnection : m_idleConnections (Permanent)
    QcConnectionPool +-- Lease : nested
    Lease *-- QcNativeConnection
    QueryCreator *-- QcConnectionPool
    QueryCreator ..> QcSqlQuery : renders via toSql()
    QueryCreator ..> QcSqlInsert : renders via toSql()
    QueryCreator ..> QcSqlUpdate : renders via toSql()
    QueryCreator ..> QcSqlDelete : renders via toSql()
```

Three independent layers, stitched together by a single call through the facade:

1. **Query builder** (`QcSqlQueryValue`/`QcSqlQueryElement`/`QcSqlQuery`/
   `QcSqlInsert`/`QcSqlUpdate`/`QcSqlDelete`) — a fluent API that renders itself into
   SQL text + bind parameters via `toSql()`. Knows nothing about
   connections/networking.
2. **`QcSqlDialect`** — the only place where SQL generation actually
   differs by driver (placeholders, `CAST` types, `LIMIT`/`OFFSET`,
   `CONCAT`, `RETURNING`/`OUTPUT`, identifier quoting, `LENGTH`/
   `SUBSTRING`/`EXTRACT`/`DATEADD`). Pure functions over strings, no external
   dependencies.
3. **Lower level** (`QcNativeConnection` + `QcConnectionPool`) —
   physical DB connections through native client libraries, without
   framework abstractions on top of them.
4. **`QueryCreator`** — a facade connecting 1 and 3 in a single call
   (`qc.execute(query)`), with the dialect (2) taken automatically from
   `QcConnectionParams`.

## Naming convention

- Query engine classes (`src/query/`) — PascalCase with `Qc` prefix
  (`QcSqlBase`, `QcSqlQuery`, `QcSqlQueryElement`, `QcSqlQueryValue`,
  `QcSqlInsert`, `QcSqlUpdate`, `QcSqlDelete`, `QcConnectionPool`,
  `QcNativeConnection`) or just PascalCase for the top-level facade
  (`QueryCreator`).
- Private fields — `m_` prefix + camelCase, no Hungarian notation by type.

## Native DB drivers: two independent decisions

The library (query builder) has no third-party dependencies at all. The project as
a whole links with native DB clients, and the selection is split into two
independent stages:

1. **Compile time** — the CMake option `QC_DB_DRIVERS` (semicolon-separated list, or
   `All`) decides which of the five native clients are even linked into this
   binary — and, accordingly, whose dev dependencies are needed on the build machine.
   Each compiled-in driver defines its own `QC_DB_HAS_<DRIVER>`
   (e.g. `QC_DB_HAS_POSTGRESQL`); these macros are not mutually exclusive — with
   `QC_DB_DRIVERS=All` all five are defined at once.
2. **Runtime** — `QcConnectionParams::driver` (type `QcDbDriver`, see
   [`qcdbdriver.h`](../src/query/qcdbdriver.h)) decides which of the
   compiled-in drivers to use for a particular connection.
   The `QcNativeConnection` constructor throws `std::runtime_error` if the
   requested driver is not included in the `QC_DB_DRIVERS` of this build.

### Bridge/Strategy: `qcnativeconnection.cpp` as a thin dispatcher

`QcNativeConnection` — a thin, always-compiled dispatcher (`switch
(params.driver)`) that selects a concrete implementation via factory functions
`create*Connection()` ([`qcdriverfactory.h`](../src/query/qcdriverfactory.h),
each declared under its own `#ifdef QC_DB_HAS_*`). Each driver's implementation
lives in its own file (`qcdriverpostgresql.cpp`/`qcdriveroracle.cpp`/
`qcdrivermysql.cpp`/`qcdriversqlite.cpp`/`qcdrivermssql.cpp`), in its own
class implementing the common interface `IQcDriverConnection`
([`qcdriverconnection.h`](../src/query/qcdriverconnection.h)). Each file is a
separate translation unit with its own `#include <libpq-fe.h>`/`<oci.h>`/...
and its own anonymous namespace, so same-named helper functions of different
drivers (`kPingQuery`, `bindParam`, `fetchRows`, ...) have no linker conflict.

`qcsqldialect.cpp` is structured differently: SQL text generation has no external
dependencies (just string manipulation), so all five dialect branches
compile **always**, regardless of `QC_DB_DRIVERS` — the selection is not done by
`#ifdef` but by an ordinary `switch (driver)` inside each function that takes
`QcDbDriver driver` as an explicit parameter (defaulting to `PostgreSQL`). The builders
(`QcSqlQuery`/`QcSqlInsert`/`QcSqlUpdate`/`QcSqlDelete`/`QcSqlQueryValue`/
`QcSqlQueryElement`) store their driver as a field (`m_driver`, set once
via `useDriver(QcDbDriver)`, defaulting to `PostgreSQL`) — `toSql()` with no
argument renders under it, while `toSql(QcDbDriver)` renders under the explicitly
specified one (used recursively for subqueries — the parent always
passes its own driver down, not reading the child's `m_driver`,
to avoid mixing dialects within a single statement).
`QueryCreator` stores the `QcDbDriver` copied from the `QcConnectionParams`
it was created with, and passes it to all internal `toSql(m_driver)` calls —
caller code through the facade never needs to specify anything explicitly.

### How dependencies are pulled in (Linux and Windows)

- **PostgreSQL/MySQL** — via [vcpkg manifest mode](../vcpkg.json),
  identical on both platforms. `MySQL` →
  `find_package(unofficial-libmysql CONFIG)`. `PostgreSQL` is found
  by CMake's built-in `FindPostgreSQL` module, which works both through the vcpkg
  toolchain and through the system `libpq-dev`. [`CMakePresets.json`](../CMakePresets.json)
  provides one preset per driver; for `postgresql`/`mysql` it injects the vcpkg
  toolchain and `VCPKG_MANIFEST_FEATURES` itself (requires `VCPKG_ROOT`).
- **MSSQL** — bare `find_package(ODBC)` (CMake built-in module) on both
  platforms: on Windows it finds the OS system driver manager, on
  Linux/macOS — unixODBC via pkg-config (`odbc.pc`, from `unixodbc-dev`, not
  from vcpkg). Does not require `VCPKG_ROOT`. Provides only the ODBC driver manager —
  the actual SQL Server ODBC driver (Microsoft `msodbcsql18` or
  open-source FreeTDS `tdsodbc`) is registered in `odbcinst.ini` separately,
  as a runtime, not link-time, dependency.
- **SQLite** — amalgamation (`sqlite3.c`/`.h`/`sqlite3ext.h`) is vendored
  directly in the project tree, [`third_party/sqlite/`](../third_party/sqlite/),
  compiled as an ordinary translation unit. Works without network and without vcpkg.
- **Oracle** — the only driver that cannot be pulled automatically
  via vcpkg (licensing) or via a system package manager: Instant
  Client (Basic + SDK) is installed manually; the path is passed via
  `OCI_ROOT`/`ORACLE_HOME`. Discovery — custom module
  [`cmake/FindOracleOCI.cmake`](../cmake/FindOracleOCI.cmake).
- Smoke check for actual linking (not just successful
  `find_package`) — `QcNativeConnection::nativeDriverInfo(QcDbDriver)`:
  makes a real call into the native client (`PQlibVersion`/
  `OCIClientVersion`/`mysql_get_client_info`/`sqlite3_libversion`/
  `SQLAllocHandle`) and passes regardless of which drivers are selected in
  `QC_DB_DRIVERS`.
- CI ([`.github/workflows/ci.yml`](../.github/workflows/ci.yml)) builds and
  runs tests on a Linux×Windows matrix for `postgresql`/`sqlite`/`mssql`;
  `mysql` (heavy vcpkg port with Boost/OpenSSL) — via `workflow_dispatch`,
  `oracle` is not included in CI (requires manual Instant Client).

## Connection pool and thread safety

The lower level — three classes in `src/query/`:

- [`QcConnectionParams`](../src/query/qcconnectionparams.h) — POD with
  connection parameters (`driver`/`host`/`port`/`database`/`user`/
  `password`/`connectTimeoutSeconds`). Not all fields are meaningful for every
  driver: SQLite reads only `database` as the file path; Oracle
  uses `host`/`port`/`database` together as an Easy Connect string
  `"host:port/database"` for `OCILogon2` (`database` — service name, not TNS);
  MSSQL uses `host`/`port` as the ODBC connection string's
  `SERVER=host,port`; MySQL uses all five fields as-is.
  `connectTimeoutSeconds` — `0` means "not set, the driver decides on its own".
- [`QcNativeConnection`](../src/query/qcnativeconnection.h) — RAII wrapper
  for **one** physical connection. The constructor either opens a working
  connection or throws `std::runtime_error`. Not thread-safe on its
  own — used by only one thread at a time; the pool enforces this.
  - **`execute(sql, params)`** — input: raw SQL text with native
    positional placeholders + `QcVariantList` of data to be substituted into them. Real
    parameter binding (not string concatenation) — protection against SQL
    injection out of the box; the path without parameters goes through a
    separate call, allowing multiple `;`-delimited commands at once.
    Result — `std::optional<QcResultSet>` (`QcResultSet =
    vector<vector<QcVariant>>`): `nullopt` — error, empty row set —
    command executed but returned no rows, non-empty — rows from
    `SELECT`/`RETURNING`/... Cell typing differs by driver: in
    PostgreSQL and Oracle all non-empty cells are `std::string` "as-is" from
    the server (textual wire protocol / `SQLT_STR`), except Oracle
    `CLOB`/`BLOB`, which are read through the LOB locator as real
    `std::string`/`std::vector<std::byte>`; in SQLite/MSSQL/MySQL typing
    is real, based on column metadata (`long long`/`double`/`std::string`/
    `std::vector<std::byte>`).
  - **`isAlive()`** — active check with a real query (`SELECT 1`,
    `SELECT 1 FROM DUAL` for Oracle), not just reading local status:
    it has been verified that `PQstatus()` alone does not notice a connection killed on
    the server until something is actually attempted on it. Called
    only at hand-out from the pool (`acquire()` in `Permanent` mode), not before
    every `execute()` — otherwise every call would double the round-trip to
    guard against the relatively rare case of a disconnect mid-lease.
- [`QcConnectionPool`](../src/query/qcconnectionpool.h) — a pool with two
  modes (`QcConnectionPool::Mode`):
  - **`Permanent`** — when the pool is created, `size` connections are opened immediately;
    `acquire()` hands out a free one and blocks (`std::condition_variable`)
    if all are busy; on return the connection is not closed but goes back
    into the pool. Before handout, an idle connection is checked with `isAlive()` —
    a dead one is transparently reopened. If reopening fails (DB
    unavailable) — the slot is not lost: a
    `nullptr` placeholder is placed in `m_idleConnections`, and the exception is thrown to the caller; the next
    `acquire()` for that slot will retry.
  - **`OnDemand`** — connections are not held between uses:
    `acquire()` opens a new one, `release()` (via RAII) closes it.
    `size` in this mode is the concurrency ceiling (how many connections
    are open simultaneously), not the pool size.
  - Both modes under one `std::mutex` + `std::condition_variable`; both
    public connection-acquisition methods go through one private
    `acquireImpl()`, differing only in whether they `wait()` (no deadline)
    or `wait_for()` (with a deadline).
  - **`acquire()`** — blocks indefinitely. **`tryAcquire(timeout)`** —
    same but with a deadline; `std::nullopt` if not acquired in time.
  - `acquire()`/`tryAcquire()` return `QcConnectionPool::Lease` —
    an RAII handle: the connection is automatically returned to the pool (`Permanent`)
    or closed (`OnDemand`) in `Lease`'s destructor, including exception
    paths. No raw `QcNativeConnection*`/reference handouts without an RAII wrapper
    exist anywhere in the public API.
  - The pool destructor asserts a program invariant: all
    outstanding `Lease` objects must be released before the pool is destroyed.

Intentionally omitted: a timeout on query execution itself
(`statement_timeout`) — only on connection and queue wait; explicit
`shrink`/`grow` of the pool at runtime; automatic retry of an SQL command
that failed due to a connection break *during* use (rather than at handout from
the pool) — unsafe for non-idempotent commands, left to the caller.

## PostgreSQL driver

Via `PQconnectdb`/`PQfinish`/`PQexec`/`PQexecParams`. `connectTimeoutSeconds`
goes into `conninfo` as `connect_timeout=N`. The only driver without
its own column metadata in this project — all non-empty cells
arrive as `std::string`.

## SQLite driver

`sqlite3_open_v2` (with `SQLITE_OPEN_CREATE` — the connection also
creates the file if it does not exist) + `PRAGMA foreign_keys = ON` at connect,
binding via `sqlite3_bind_*`, reading via `sqlite3_column_type()` — types
come for free, without a stringification stage. `connectTimeoutSeconds`
reused for `sqlite3_busy_timeout()` (timeout waiting on another writer's
lock — SQLite has no network, hence no TCP connection
timeout).

## Oracle driver (OCI)

Via OCI (Oracle Call Interface — the "bare" C API, not OCCI).

**One struct for three handles.** `QcNativeConnection::m_handle` points to
`OracleConnection` (anonymous namespace `qcdriveroracle.cpp`): three
simultaneously live handles (`OCIEnv* envhp`, `OCIError* errhp`, `OCISvcCtx*
svchp`) plus `bool inExplicitTransaction` (see "Autocommit" below).

**Connection** — `OCIEnvNlsCreate(&envhp, OCI_THREADED, ..., 873, 873)`
instead of `OCIEnvCreate`: the last two arguments fix the client charset
and ncharset explicitly (`873` = Oracle charset id for `AL32UTF8`), without which
multi-byte text gets mangled into `?` placeholders regardless of the database's own
charset. Login — `OCILogon2(envhp, errhp, &svchp, user, pass,
connectString, OCI_DEFAULT)` with an Easy Connect string `"host:port/service"` —
one call instead of the `OCIServerAttach`+`OCIHandleAlloc(session)`+
`OCISessionBegin` chain. Immediately after login — `ALTER SESSION SET
NLS_DATE_FORMAT='YYYY-MM-DD' NLS_TIMESTAMP_FORMAT='YYYY-MM-DD HH24:MI:SS'`,
so that implicit string↔`DATE`/`TIMESTAMP` conversion matches the project's ISO text.
`connectTimeoutSeconds` is accepted but ignored —
`OCILogon2`'s one-line connect flow provides no place to insert it.

**Binding — without LOB locators at all**, even for multi-megabyte values:
plain `OCIBindByPos` with `SQLT_STR`/`SQLT_BIN` accepts values up to
several megabytes without errors. `long long`/`double`/`std::string` →
`SQLT_STR`, `std::vector<std::byte>` → `SQLT_BIN`, `std::monostate` →
indicator `OCI_IND_NULL`.

**Fetch — LOB locators are needed only for `CLOB`/`BLOB` columns**:
`OCIStmtExecute(iters=0)` (implicit describe) → `OCIParamGet`+
`OCIAttrGet(OCI_ATTR_DATA_TYPE)` per column → `SQLT_CLOB`/`SQLT_BLOB`
get an `OCIDescriptorAlloc`-locator via `OCIDefineByPos`, everything else —
a plain `SQLT_STR` buffer (4001 bytes) → `OCIStmtFetch2` in a loop →
LOB columns are read via `OCILobGetLength2`+`OCILobRead2`. `OCILobRead2`
must receive **both** `byte_amtp` and `char_amtp` at once — with only
`byte_amtp` the length is incorrect as soon as the client charset
is UTF-8-aware (even for purely ASCII content).

**Autocommit emulation** — Oracle has no per-statement autocommit; a transaction
is always implicitly open. `OracleConnection::inExplicitTransaction`: by
default `false`, each `executeStatement()` goes with `OCI_COMMIT_ON_SUCCESS`;
the literal `"BEGIN"` is intercepted as a special case before `OCIStmtPrepare`
(Oracle itself rejects it — `PLS-00103`, parsed as the start of a PL/SQL block),
sets the flag to `true`, and subsequent statements go with `OCI_DEFAULT`;
`"COMMIT"`/`"ROLLBACK"` reset the flag to `false`.

**Table/subquery alias without `AS`** — Oracle rejects `FROM dual AS d`
(`ORA-00933`), accepting only `FROM dual d`; for a column alias in `SELECT`
and for CTEs `AS` works everywhere, including Oracle. `QcSqlQuery::toSql()`
uses the private `tableAliasSeparator()` (`" AS "` everywhere, `" "` only
on Oracle).

**`RETURNING ... INTO` — scalar, not array, OUT bind**: see
["RETURNING on Oracle and MySQL"](#returning-on-oracle-and-mysql) below.

Other Oracle dialect restrictions: `"DROP TABLE IF EXISTS"` is not
supported (wrapped in an anonymous PL/SQL block with `EXCEPTION WHEN
OTHERS THEN NULL`); `%` is not an arithmetic operator (`MOD(a, b)` instead of
`a % b`); identifier quoting — regular case-preserving ANSI, as in
PostgreSQL/SQLite (no Oracle-specific condition in
`quoteIdentifier()`) — the only consequence: Oracle folds *unquoted*
identifiers to uppercase at creation time (PostgreSQL/SQLite — to lowercase),
so a schema created by unquoted DDL must match the case of
what quoted SQL generates.

## MSSQL driver (ODBC)

Unlike PostgreSQL/MySQL/Oracle/SQLite, MSSQL has no single vendor
client library — ODBC is used: `queryCreatorLib` links with the
driver manager (unixODBC on Linux/macOS, system ODBC on Windows,
through the same `find_package(ODBC)`), and the concrete SQL Server
ODBC driver (Microsoft `msodbcsql18` or open-source FreeTDS `tdsodbc`)
is registered separately in `odbcinst.ini`.

**Connection** — `SQLDriverConnect` with a connection string
(`DRIVER={ODBC Driver 18 for SQL Server};SERVER=host,port;DATABASE=...;
UID=...;PWD=...;Encrypt=yes;TrustServerCertificate=yes;`).
`TrustServerCertificate=yes` is required: Driver 18 demands TLS by default,
and SQL Server's self-signed certificate is rejected without this flag.
Functions are called **without** the `A` suffix (`SQLDriverConnect`, not
`SQLDriverConnectA`) — on unixODBC suffixed names simply do not exist.

**Unicode — `SQLWCHAR`/UTF-16 mandatory**: the narrow ODBC path (`SQL_C_CHAR`)
is codepage-dependent and does not guarantee UTF-8, so parameter/column
binding and fetching always go through the wide path (`SQL_C_WCHAR`), with manual
UTF-8 ⇄ UTF-16 encoding/decoding (handling surrogate pairs), without
`<codecvt>`. `static_assert(sizeof(SQLWCHAR) == 2, ...)` explicitly
pins the assumption.

**Binding by string length** — strings up to `NVARCHAR(4000)` are bound as
`SQL_WVARCHAR` (ordinary "convertible" parameter type), longer ones —
`SQL_WLONGVARCHAR` (LOB class). Binding **any** string as LOB is incompatible
with non-string columns: MSSQL does not implicitly cast a LOB-class parameter
to a non-string column (e.g. `DATE`), even when the source value is
a short ISO date.

**Transactions** — the ODBC connection is already in autocommit mode by default, like
PostgreSQL/SQLite; the only adaptation — the literal `"BEGIN"` (not
valid T-SQL on its own, parsed as a `BEGIN...END` block) is substituted with
`"BEGIN TRANSACTION"`; `"COMMIT"`/`"ROLLBACK"` — unchanged.

**Fetch — typed columns via `SQLDescribeCol`**, four categories
(integers, including `BIT` → `long long`; fractional/`DECIMAL`/`NUMERIC` → `double`;
`(VAR)BINARY`/`LONGVARBINARY` → `vector<byte>`; everything else → text),
arbitrary length — via chunked `SQLGetData` in a loop (not one
fixed buffer) — `NVARCHAR(MAX)`/`VARBINARY(MAX)` columns have no
upper bound. The distinction "more data yet" vs "this is the last chunk" — by
return code (`SQL_SUCCESS_WITH_INFO` vs `SQL_SUCCESS`), not only by
the length indicator (which may be `SQL_NO_TOTAL`).

## MySQL driver (libmysqlclient, prepared statements)

Via MySQL C API prepared statements (`mysql_stmt_init`/`_prepare`/
`_bind_param`/`_execute`/`_bind_result`/`_fetch`), not the deprecated
text protocol — the binary protocol provides real result typing and
safe binding without string concatenation. The only exception —
the literal `"BEGIN"`/`"START TRANSACTION"`, which `mysql_stmt_prepare()`
refuses to prepare in principle (`"This command is not supported in the
prepared statement protocol yet"`; `COMMIT`/`ROLLBACK` prepare fine)
— is intercepted as a special case and goes through the legacy `mysql_query()`.

**`utf8mb4` is not inherited from the database** — the client connection by default
uses `latin1` regardless of the DB's charset/collation, so
the connection explicitly sets `mysql_options(conn, MYSQL_SET_CHARSET_NAME,
"utf8mb4")` before `mysql_real_connect()`.

**Fetch typing** — `categorize()` separates the `TEXT`/`BLOB` family not by
type name (both use the same `enum_field_types` at the
wire protocol level), but by `field.charsetnr == 63` (charset `binary`). Buffers for
`TEXT`/`BLOB` columns are allocated exactly to `field.max_length`, computed by
`mysql_stmt_store_result()` after `mysql_stmt_attr_set(stmt,
STMT_ATTR_UPDATE_MAX_LENGTH, ...)` (disabled by default "for speed").

**MySQL dialect restrictions**: `FULL JOIN`/`FULL OUTER JOIN` is not
supported in any form at all (`addFullJoin()` renders SQL
that will fail at execution — the builder does not emulate it via `LEFT JOIN
UNION RIGHT JOIN`); inline column-level `REFERENCES other(id)` is parsed
but does not create a real FK on InnoDB (requires the explicit table-level form
`FOREIGN KEY (col) REFERENCES other(col)`); `(? + ?)` at the `PREPARE`
stage is typed as `DOUBLE`, not `BIGINT` (unlike SQLite/MSSQL); `TEXT`/
`BLOB` are limited to 64 KiB — `LONGTEXT`/`LONGBLOB` are needed for larger values.

## Query builder

`QcSqlQueryValue`/`QcSqlQueryElement`/`QcSqlQuery`/`QcSqlInsert`/
`QcSqlUpdate`/`QcSqlDelete` — the full public API is described line-by-line in
[usage.md](usage.md) and [components.md](components.md). Here — overarching
architectural decisions.

### Subquery ownership — `QcDeepPtr<T>`

Pointers to subqueries (`QcSqlQuery::m_fromSubquery`, `JoinClause::query`,
`CteClause::query`, `SetOperation::query`, `QcSqlQueryElement::m_subQuery`)
— [`QcDeepPtr<T>`](../src/query/qcdeepptr.h), a small template wrapper over
`std::unique_ptr<T>`: copying a `QcDeepPtr` **deep-clones** `T` (rather than
sharing ownership or shallow-copying the pointer); move and destruction —
free via `unique_ptr`.

- **`QcSqlQuery`, `QcSqlQueryElement` — fully (deeply) copyable
  types**: copying a `QcSqlQuery` means copying every subquery it
  references — the FROM subquery, every JOIN, every CTE, every
  set operation, every subquery within WHERE/HAVING.
- None of them has a hand-written destructor/copy-ctor — all
  special member functions are implicit, correct simply because all
  members (including `QcDeepPtr<QcSqlQuery>`) correctly copy themselves. This
  works even for the self-referential case (`QcSqlQuery`, holding
  `QcDeepPtr<QcSqlQuery>`) for the same reason that `unique_ptr<T>` as a
  member of its own class is a standard pattern.
- `QcSqlQueryValue::m_subQuery` remains a raw `QcSqlQuery * = nullptr` —
  no public method of `QcSqlQueryValue` sets it.
- Any translation unit that actually constructs/destroys
  `QcSqlQueryElement` (not just holds a pointer/reference) must include
  `qcsqlquery.h`, not just `qcsqlqueryelement.h` — the latter only
  forward-declares `QcSqlQuery` to avoid a circular `#include`.
  `qcsqlupdate.h`/`qcsqldelete.h` include `qcsqlquery.h` in full for the same
  reason.

### White boxes for tests instead of public getters

None of the six classes exposes public getter methods for
internal state — where the public `toSql()` does not provide enough
observability, each class declares exactly one `friend class
…WhiteBoxTest;` (without pulling `<gtest/gtest_prod.h>` into production headers
— `queryCreatorLib` must compile even without GTest, e.g. when
`-DBUILD_TESTING=OFF`), and the test file defines a fixture with that name and
static accessor methods.

### Key semantic decisions

- **`where`/`and_`/`or_`/`having`/`and_Having`/`or_Having`** append
  an element to the chain as `{connector, element(column)}` and return a reference
  to the newly inserted element — comparators mutate this same object in
  place. The connector is a private `enum LogicalConnector { _and_, _or_ }`
  inside the owning class (not a public enum in `qcsqlbase.h`).
- **Parentheses in WHERE** — `_openParenthesis_`/`_closeParenthesis_` from
  `compareTypes` are used as the compareType of synthetic
  "marker" elements that lie in the same chain as real conditions.
  `where_OpenParenthesis`/`and_OpenParenthesis`/`or_OpenParenthesis` push
  such a marker and a condition element in a single call. `openParenthesis()`/
  `closeParenthesis()` (without a column) work only with the WHERE chain, not
  HAVING; balance is tracked by the private `m_whereParenDepth`;
  `closeParenthesis()` returns `false`, leaving the vector untouched, if there is nothing
  to close.
- **`fromSubQuery(alias, query)`** reuses `m_fromTable` as the field for
  the subquery alias — FROM provides exactly one source (a table **or**
  a subquery, never both). `fromTable()` symmetrically resets
  `m_fromSubquery` to null.
- **`exists`/`notExists`** leave `m_columnName` as-is (do not touch
  it) — it is meaningless for `EXISTS (subquery)`, not rendered.
- **`cast(from, to, val|subQuery)`** is treated as "compare `CAST(column
  AS to)` for equality with `val`/subquery" — sets both
  `m_functionType=_cast_` and `m_compareType=_isEqualTo_`.
- **`isIn`/`isNotIn` (list) and `isBetween`/`isNotBetween`** use
  a separate field `QcVariantList m_values` (not `m_functionParams`, which
  remains only for `cast` parameters).
- **Every comparator calls `resetOperands()`** (resets
  `m_value`/`m_values`/`m_subQuery`) before setting its own — otherwise
  reusing one `QcSqlQueryElement` for multiple `is*()`-calls
  in a row would leave old operands hanging alongside new ones.
- **`addReturnValues`/`addReturnValue`** build `QcSqlQueryValue` via
  the constructor `QcSqlQueryValue(const std::string & name)`, which parses
  the convention `"col <alias>"` (`trim()` + `regex "<(.+)>"`).

### `toSql()` — generation internals

- **`QcSqlQuery::toSql(params)`** — recursive assembly: `WITH` → `SELECT
  [DISTINCT[ ON]]` → `FROM`/`JOIN` chain → `WHERE` (conditions + `addFreeText`)
  → `GROUP BY` → `HAVING` → `ORDER BY` → `LIMIT`/`OFFSET` → set operations.
  The public `toSql()` with no arguments — a thin wrapper: creates a fresh
  `QcSqlStatement{sql, params}` and delegates to `toSql(params)`; the
  `toSql(params)` itself is called recursively for any nested `QcSqlQuery`,
  **appending** its bind values to the **shared** list of the caller — this is what
  makes `$N`/`:N` numbering globally correct throughout the assembled text.
- **`QcSqlQueryElement::renderChain()`** — public static, shared logic
  for WHERE and HAVING (and for WHERE of `QcSqlUpdate`/`QcSqlDelete`): the connector
  (`AND`/`OR`) is not rendered before the very first element of the chain and immediately
  after `(` — via `isOpenParenthesisMarker()`/`isCloseParenthesisMarker()`.
- **`addFreeText`** — AND-appended to the end of WHERE. Raw text is written with
  a generic `'?'` for each value (not the native placeholder of the specific
  driver) — `toSql()` substitutes each `?` with the count-correct
  native placeholder during assembly.
- **JOIN with `asSubQuery=false`** reads the **table name** directly from
  `joinQuery.m_fromTable` (does not build a subquery) — `addLeftJoin`/`addJoin`/
  `addRightJoin`/`addFullJoin` always accept `QcSqlQuery`, even for
  a plain table join.
- **`DISTINCT ON`** is rendered only on PostgreSQL (the only dialect
  where it exists); on others `distinctOn()` degrades to plain
  `DISTINCT`, losing the "distinguish by these columns" semantics.
- **Set operations** — PostgreSQL/MySQL wrap both sides in parentheses
  (`(main) UNION (rhs)`, protection against differing operator precedence and
  allowing each side to keep its own `ORDER BY`/`LIMIT`
  unambiguously); SQLite/MSSQL/Oracle do not accept parentheses around a member
  of a compound select by their grammar — there both sides are simply
  concatenated without parentheses (`main UNION rhs`). Caveat for all: MSSQL does not
  allow `ORDER BY` in a non-last `SELECT` of the compound — not accommodated.

## SQL generation — `QcSqlDialect`

[`qcsqldialect.h`](../src/query/qcsqldialect.h)/`.cpp` — the only place that
centralizes dialect differences. Each function accepts `QcDbDriver
driver` as an explicit parameter (defaulting to `PostgreSQL`).

- **`placeholder(index)`** — `$N` (PostgreSQL), `:N` (Oracle), `?`
  (MySQL/SQLite/MSSQL).
- **`dataTypeName(dataType)`** — type name for `CAST(expr AS ...)`. In MySQL
  `CAST()` accepts a fixed narrow list of target types
  (`SIGNED`/`CHAR`/`DATE`/`DATETIME`/`DECIMAL`/`DOUBLE`/`JSON`/... — neither
  `VARCHAR`, nor `BOOLEAN`, nor `TEXT`/`BLOB` exist as cast targets,
  nearest analogues substituted); Oracle has no `BOOLEAN` in SQL (only in
  PL/SQL) — `NUMBER(1)` by convention; MSSQL/Oracle have no separate
  `JSON` type — `NVARCHAR(MAX)`/`CLOB`.
- **`limitOffsetClause(rowsCount, startRow, withTies)`** — PostgreSQL:
  `LIMIT n OFFSET m`, the only driver accepting bare `OFFSET` without
  `LIMIT`. MySQL/SQLite: same form, but require `LIMIT` alongside `OFFSET` —
  when only `startRow` is requested, MySQL substitutes `LIMIT
  18446744073709551615` (the documented 2⁶⁴−1 sentinel), SQLite — `LIMIT
  -1` (the documented "no limit" idiom). MSSQL/Oracle — ANSI
  `OFFSET m ROWS FETCH NEXT n ROWS ONLY`, the only supported form.
  `withTies` always switches to the ANSI form (`... WITH TIES` instead of `...
  ONLY`) on any driver; SQLite has no `WITH TIES` in any form — when
  requested, the ANSI syntax is still rendered, which will fail at execution
  (documented limitation, not silent degradation).
- **`concatExpr(operands)`** — PostgreSQL/MySQL/MSSQL: variadic
  `CONCAT(...)`. Oracle: `CONCAT()` accepts exactly two arguments; SQLite does not
  have `CONCAT()` at all before version 3.44 (2023) — for both, the
  `||` operator is used.
- **`returningClause(columns, mssqlRowKeyword, oracleFirstPlaceholderIndex)`**
  — see ["RETURNING on Oracle and MySQL"](#returning-on-oracle-and-mysql) below.
  The fragment's position within the statement — the concern of each builder
  (`qcsqlinsert.cpp`/`qcsqlupdate.cpp`/`qcsqldelete.cpp`), not the
  helper itself: `RETURNING`/`RETURNING...INTO` always at the end, MSSQL `OUTPUT` —
  between the column list/SET and `VALUES`/`WHERE`.
- **`quoteIdentifier(name)`** — quotes a single identifier segment
  (no dots), doubling the embedded quote character for escaping.
  PostgreSQL/SQLite/Oracle: ANSI double quotes, case preserved (`"a""b"`).
  Oracle's quoted identifiers are case-sensitive exactly as in
  PostgreSQL — the only consequence in the opposite direction: Oracle folds
  *unquoted* identifiers to UPPERCASE at creation time
  (PostgreSQL/SQLite — to lowercase), so a schema created by unquoted DDL
  must match the case of the generated SQL. MySQL: backticks
  (`` `a``b` ``). MSSQL: brackets (`[a]]b]`). Empty string and `"*"`
  returned as-is.
- **`quoteRef(name)`** — a reference, possibly dotted
  (`table.column`, `schema.table.column`): each segment independently through
  `quoteIdentifier()`. Quotes only if *all* segments
  look like simple identifiers (`[A-Za-z_][A-Za-z0-9_$]*`, or `*`) —
  otherwise returns `name` unchanged. This is a necessity, not
  extra caution: the "column name" parameters throughout the API
  (`where()`/`groupBy()`/`orderAsc()`/`addReturnValue()`/`having()`/...) —
  are also the only way to reach SQL for which the builder has no
  dedicated method (aggregate expressions as a single string, JSON operators) —
  unconditional quoting would interpret such a string as a
  single bizarre identifier and correct it incorrectly.
- **`quoteTableRef(name)`** — a FROM/JOIN reference with a possible inline
  alias (`fromTable()` has no separate alias parameter, unlike
  `fromSubQuery()`/`addXJoin()`): splits by the first space — part before —
  `quoteRef()`, part after — `quoteIdentifier()`.
- **`lengthExpr`/`substringExpr`** — MSSQL is the only one without `LENGTH()`/
  `SUBSTR()`, only `LEN()`/`SUBSTRING()`; `LEN()` additionally trims
  trailing spaces before counting (`LEN('ab  ')` = 2, not 4).
- **`extractExpr`/`dateAddExpr`** — the most dialect-diverging functions.
  `extractExpr`: PostgreSQL/MySQL/Oracle — native `EXTRACT(YEAR FROM
  expr)`; MSSQL — `DATEPART(year, expr)`; SQLite — `CAST(strftime('%Y',
  expr) AS INTEGER)`. `dateAddExpr`: PostgreSQL — `expr + INTERVAL 'N
  unit'`; MySQL — `DATE_ADD(expr, INTERVAL N unit)`; MSSQL — `DATEADD(unit,
  N, expr)`; SQLite — `datetime(expr, '+N unit')` (explicit sign mandatory);
  Oracle — `expr + INTERVAL 'N' unit`, which Oracle (unlike pure
  ANSI) additionally accepts for MONTH/YEAR as well.
- **`orderByEntry(column, descending, nulls)`** — one `ORDER BY` element
  (`column` already passed through `quoteRef()` by the caller). `nulls ==
  _nullsDefault_` — just `column ASC`/`DESC`, without an explicit `NULLS` clause
  (each driver then follows its *own* default, and it is not
  the same even without a single line of this code: PostgreSQL/Oracle
  sort `NULL` as "greater" than any value — with `ASC` it ends up
  last; MySQL/SQLite/MSSQL — as "less", it ends up first).
  PostgreSQL/SQLite (3.30+)/Oracle — native trailing `NULLS
  FIRST`/`NULLS LAST`. MySQL/MSSQL have no such syntax at all —
  emulated via a leading tiebreaker before the column itself: `CASE WHEN
  column IS NULL THEN 0 ELSE 1 END, column ASC/DESC` (NULLS FIRST) or
  `CASE WHEN column IS NULL THEN 1 ELSE 0 END, column ASC/DESC` (NULLS
  LAST) — the tiebreaker itself is always ascending; the direction of the real
  column does not affect it. `QcSqlQuery::orderAsc()/orderDesc()` take
  `nulls` (`QcSqlBase::nullsPosition`) as an optional second parameter,
  common to all columns of a single call — to mix different `NULLS` positions
  across columns in one `ORDER BY`,
  `orderAsc()`/`orderDesc()` is called once per column.

**`IS DISTINCT FROM`/`IS NOT DISTINCT FROM`** — implemented directly in
`qcsqlqueryelement.cpp`, not as a `QcSqlDialect` function: it needs access to
`bind()` (which may bind a value twice on MSSQL), not just to ready-made
strings. PostgreSQL — native. SQLite — native, but mirrored semantically
(`IS`/`IS NOT` in SQLite are already NULL-safe, only swapped around). MySQL —
the `<=>` operator (NULL-safe equal) and its negation. Oracle/MSSQL — spelled out
manually: `(a IS NULL AND b IS NULL) OR (a IS NOT NULL AND b IS NOT NULL AND
a = b)` (the guarded form — the naive `a = b OR (a IS NULL AND b IS NULL)` loses
rows under SQL's three-valued NULL logic). The only difference between Oracle and
MSSQL here — not in the formula, but in the cost of re-mentioning the operand:
Oracle's `:N` placeholder can be used in the text multiple times, referring
to the same bound value (`bind()` is called once);
MSSQL's positional `?` cannot — the operand is bound separately per textual
occurrence (three times: both `IS NULL` checks plus `=`).

Quoting is applied everywhere an identifier actually ends up in the text:
`QcSqlQuery::toSql()` (FROM/JOIN table/alias, GROUP BY/ORDER BY/DISTINCT
ON columns, CTE name), `QcSqlQueryElement::toSql()` (WHERE/HAVING column,
including inside `CAST(...)`), `QcSqlQueryValue::toSql()` (SELECT column and
alias, plus operands of `concat()`/`coalesce()`/`nullIf()`/`replace()`/
`case_()` via `quoteRef()`), `QcSqlInsert`/`QcSqlUpdate`/
`QcSqlDelete::toSql()` (table, SET/column-list columns),
`QcSqlDialect::returningClause()` (RETURNING/OUTPUT columns). Never
quoted (documented raw SQL escape hatch): JOIN's
`onCondition`, `addFreeText`'s text.

## JSON queries — `QcSqlDialect::jsonExtractExpr()`

Before this mechanism, the only way to reach a value inside a
JSON column was raw SQL text through `addFreeText()`/`where()`/
`addReturnValue()` — working (these parameters are not unconditionally quoted
anyway, see above), but requiring a separate branch for each of the five
drivers at **every** call site (example — `JsonFieldExtractionReturnsSeededValue`
in `tests/test_integration_select.cpp`, still alive as a demonstration of the raw-SQL
path). `QcSqlQueryElement::isXxxJson...()` (WHERE/HAVING section,
[usage.md §1.9](usage.md#19-json-fields-isxxxjson--jsonextract))
and `QcSqlQueryValue::jsonExtract()`/`jsonExtractNumber()`/`jsonExtractRaw()`
(SELECT list) remove this branch from the call site entirely — all dialect
analysis is pulled into one function, `QcSqlDialect::jsonExtractExpr(expr,
jsonSearchPath, kind, driver)`.

**`jsonSearchPath`** — a simplified path without the root marker (`$`):
`.` separates nested object keys, `[N]` — array element index
(`"a.b[2].c"`). This is the only function that parses it — all
call sites (builders) simply pass the string straight through.

**`kind`** (`QcSqlBase::jsonValueKind`) — how the extracted value should
be returned, three variants:

- **`_jsonAsNumber_`** — cast to a numeric SQL type (numeric comparison,
  not lexicographic).
- **`_jsonAsText_`** — scalar as unquoted text (JSON string `"admin"`
  → `admin`).
- **`_jsonAsRaw_`** — the value's own JSON representation, without stripping
  the wrapper (array/object remains with its brackets/quotes) — needed
  for `isLikeJsonArrayAsText()`, which needs to search the entire
  serialized array, not a single unwrapped element.

Exactly the same `kind` vocabulary works both on the WHERE side
(`QcSqlQueryElement`) and on the SELECT side (`QcSqlQueryValue`) — it is one
shared function, not two parallel implementations.

**Internals of `QcSqlQueryElement::isXxxJson...()`**: does not introduce new
`compareTypes` — each method calls the existing comparator
(`_isEqualTo_`/`_isGreaterThan_`/`_isLike_`/...) and concurrently sets three
private fields (`m_isJsonComparison`/`m_jsonValueKind`/`m_jsonSearchPath`).
`toSql()` when `m_isJsonComparison == true` substitutes the left-hand side of the expression
(usually `quoteRef(column)`) with `jsonExtractExpr(quoteRef(column),
jsonSearchPath, kind, driver)` **before** it enters the already
existing `switch (m_compareType)` — the switch itself is untouched, so
the dialect degradation of `_isILike_`/`_isNotILike_` (PostgreSQL — native
`ILIKE`, others — `LOWER(...)  LIKE LOWER(...)`, see above) works "as
is" on JSON comparators too: `LOWER()` simply wraps the already-ready
`jsonExtractExpr(...)` instead of the bare column, without a single separate line
of code for this interaction (pinned by test
`IlikeJsonTextDegradesToLowerOnNonPostgresDrivers`,
`tests/test_qcsqlqueryelement.cpp`).

**Internals of `QcSqlQueryValue::jsonExtract...()`**: a regular write into the function
chain (`m_functions`, like `cast()`/`round()`/...) with the new
`functionTypes::_jsonExtract_`; `kind` and `jsonSearchPath` are encoded together
in the chain entry's parameters (`{kind, jsonSearchPath}`). Like any other chain
function, it wraps the current expression rather than replacing it (unlike
`case_()`) — meaning after `jsonExtract...()` the chain can continue
further (`jsonExtractNumber("rating").round(1)`).

**Dialect analysis in `jsonExtractExpr()`** (the full text of the function and its
internal helpers — `qcsqldialect.cpp`):

- **PostgreSQL** — the only driver whose extraction functions
  (`jsonb_extract_path()`/`jsonb_extract_path_text()`) do not accept a
  JSONPath string — they want the path already split into separate
  text arguments, so only for this branch is `jsonSearchPath`
  parsed into segments (`"a.b[2].c"` → `'a', 'b', '2', 'c'`); a numeric
  segment (from `[N]`) is passed as ordinary text — the same
  `text[]` function argument decides on its own whether to use it as an array
  index or an object key, depending on what actually lies
  at that path in the document. `_jsonAsText_` —
  `jsonb_extract_path_text(expr::jsonb, ...)`; `_jsonAsNumber_` — same,
  wrapped in `(...)::numeric`; `_jsonAsRaw_` —
  `jsonb_extract_path(expr::jsonb, ...)::text` (preserves quotes/brackets).
  `expr::jsonb` tolerates both `json`- and `jsonb`-columns.
- **SQLite** — `json_extract(expr, '$.a.b[2].c')` identical for all three
  `kind`: `json_extract()` already returns the scalar fully unwrapped
  (with real numeric affinity for numbers), and for an object/array —
  their serialized JSON text, so no additional wrapper is
  needed for any of the three cases.
- **MySQL** — `_jsonAsText_`: `JSON_UNQUOTE(JSON_EXTRACT(expr, path))`
  (equivalent of `->>'...'`). `_jsonAsNumber_`: `(JSON_EXTRACT(expr, path) +
  0)` — the MySQL idiom for forcing numeric context when comparing
  an extracted JSON scalar. `_jsonAsRaw_`: bare `JSON_EXTRACT(expr,
  path)`.
- **MSSQL/Oracle** — both distinguish `JSON_VALUE` (scalar only, `NULL` on
  an object/array path) and `JSON_QUERY` (object/array-fragment only,
  `NULL` on a scalar path) — `_jsonAsText_`/`_jsonAsNumber_` go through
  `JSON_VALUE`, `_jsonAsRaw_` — through `JSON_QUERY`. Numeric coercion
  differs by mechanism: MSSQL — `CAST(JSON_VALUE(...) AS FLOAT)` externally;
  Oracle — `JSON_VALUE(..., path RETURNING NUMBER)` — part of
  `JSON_VALUE` itself, without an external `CAST`.

Live verification (not just unit tests on `toSql()` text) — the same technique
as the rest of the query builder: `tests/test_integration_select.cpp`
runs the same C++ (without a single dialect branch at the call site,
unlike the three old raw-SQL JSON tests in the same file) against a real database,
including multi-level paths (`"a.b[1].c"`) through a JSON literal — verified
live on all five drivers (PostgreSQL/SQLite/MySQL/MSSQL/Oracle).

## INSERT/UPDATE/DELETE

`QcSqlInsert`/`QcSqlUpdate`/`QcSqlDelete` — the same form as `QcSqlQuery`
(inherit `QcSqlBase`, friend `…WhiteBoxTest`, `toSql()`/
`toSql(QcVariantList&)`), each builds exactly one statement (without CTE/JOIN/
HAVING/`addFreeText` — not typical for single-row DML, no scenario
required them):

- **`QcSqlInsert`** — `into(table)` + `set(column, value)` →
  `INSERT INTO table (c1, c2) VALUES ($1, $2)`. Single-row — multi-row
  batch insert is not supported; for multiple rows call `toSql()`/
  `execute()` in a loop. Repeated `set()` on an already-set column
  overwrites the value in place (preserving the position in the column list), rather
  than adding a duplicate column.
- **`QcSqlUpdate`** — `table(name)` + `set(column, value)` (same
  overwrite-in-place) → `UPDATE table SET c1 = $1 [WHERE ...]`. The WHERE side
  — direct reuse of `QcSqlQueryElement`: `where`/`and_`/`or_`
  return `QcSqlQueryElement&`, the same comparator vocabulary and the same
  parentheses sugar group as `QcSqlQuery`.
- **`QcSqlDelete`** — `from(table)` + the same WHERE set as
  `QcSqlUpdate`, → `DELETE FROM table [WHERE ...]`.

`where`/`and_`/`or_`/`*_OpenParenthesis`/`openParenthesis`/
`closeParenthesis` in `QcSqlUpdate`/`QcSqlDelete` — mechanical
push_back-mutation of each class's own `m_whereElements`/
`m_whereParenDepth` (not extracted into a common mixin — ~30 lines of trivial code
per class are not worth the complexity). Both headers (`qcsqlupdate.h`/
`qcsqldelete.h`) include `qcsqlquery.h` in full (not only
`qcsqlqueryelement.h`) — see "Subquery ownership" above.

## RETURNING on Oracle and MySQL

`returning(columns)` is implemented genuinely on four out of five drivers
(PostgreSQL/SQLite/MSSQL/Oracle), each with its own mechanism where needed —
the drivers' limitations differ in nature.

### Oracle — `RETURNING ... INTO`, without a PL/SQL block wrapper

Bare DML with `RETURNING ... INTO :N` works as a regular
`OCI_STMT_INSERT`/`UPDATE`/`DELETE`, without a `BEGIN...END` wrapper.
`QcNativeConnection::executeReturning(sql, params, returningColumnCount)` —
a sibling method of `execute()`. On all drivers except Oracle — identically
`execute(sql, params)` (`returningColumnCount` is ignored — RETURNING/
OUTPUT comes in as ordinary result rows there anyway). On Oracle —
the private `executeReturningCore()` does the common work (IN/OUT parameter
binding, `OCIStmtExecute`, commit/rollback decision) and returns raw
OUT buffers; two thin calling methods (`executeReturningStatement()` —
positional string, `executeReturningNamedStatement()` — named,
keys from `QcSqlStatement::returningColumnNames`) build the final
result from them — common code is not duplicated, because the commit/rollback logic
here is subtle (see below).

**Never `OCI_COMMIT_ON_SUCCESS` for the RETURNING path, only explicit
`COMMIT`/`ROLLBACK`** after checking the actual outcome of `OCIStmtExecute`
(skipped only inside an explicit foreign transaction) — otherwise a multi-row
`UPDATE` that failed only on the RETURNING materialization side (`ORA-24369:
required callbacks not registered for one or more bind handles`) would still
have been applied and committed, while the caller would receive `nullopt` and
reasonably assume nothing happened.

**Limitation — scalar, not array, OUT bind**: exactly one
OUT buffer is bound per column, designed for a statement that actually touches one row
(the common case for `INSERT`, and for `UPDATE`/`DELETE` with a PK-like
`WHERE`). Multi-row `RETURNING INTO` requires `OCI_DATA_AT_EXEC` +
`OCIBindDynamic()` callbacks — a fundamentally different, piecewise/dynamic
mechanism, not implemented; on multi-row DML with `returning()` Oracle
falls through with `nullopt` (`ORA-24369`) cleanly, without side effects.

### MySQL — `INSERT ... RETURNING`, emulated via `LAST_INSERT_ID()`

MySQL has no `RETURNING`/`OUTPUT` in any form at all. The solution —
qualitatively different: not read values from the same statement, but execute
the `INSERT`, then a separate `SELECT`, both on a single leased connection,
wrapped in an explicit transaction:

```
BEGIN
<plain INSERT>
SELECT LAST_INSERT_ID()
<follow-up SELECT, WHERE autoIncrementColumn = the just-obtained id>
COMMIT
```

`QcSqlInsert::returning(columns, autoIncrementColumn)` — the second overload,
only for MySQL: does not change the `INSERT` text itself, instead populates
`QcSqlStatement::mysqlReturningSelectSql` (the follow-up `SELECT` text).
`QueryCreator::execute(const QcSqlInsert&)` — the only place that
notices this non-empty field and, instead of the normal path, runs the described
sequence on the same connection (important —
`LAST_INSERT_ID()` is per-connection). `autoIncrementColumn` is mandatory —
it is never inferred which column of the table is `AUTO_INCREMENT`.

Scope — intentionally only `INSERT`: `LAST_INSERT_ID()`
is meaningful only for a row just inserted by autoincrement; for
`UPDATE`/`DELETE` there is no natural anchor, and `returning()` on MySQL
there has no effect. Not safe to call if the caller itself already
holds its own explicit transaction on this same connection — MySQL has no nested
transactions; the inner `BEGIN` would implicitly commit what was opened
outside.

## Named result access

`executeNamed()`/`executeReturningNamed()` — a parallel, purely additive
API: the same rows as `execute()`/`executeReturning()`, but as
`QcNamedRow = std::map<std::string, QcVariant>` (column → value), not
a positional `vector`. `std::map`, not `unordered_map` — result
sets are always small (a few columns), and ordered iteration matters more
than hash-table speed at such small cardinality.

The common driver-agnostic piece — `toNamedResultSet(columnNames, rows)`: takes
already-ready positional rows and a parallel name list, builds
`QcNamedResultSet` from them. "How to get names" — the only thing that is actually
different per driver: `PQfname`/`sqlite3_column_name`/`SQLDescribeCol`/
`MYSQL_FIELD::name`/`OCIAttrGet(..., OCI_ATTR_NAME, ...)` — via an
optional output parameter `QcStringList * outColumnNames = nullptr`,
added to the already-existing private fetch functions of each driver.

**Oracle `RETURNING ... INTO` (OUT bind)** — a separate case: a scalar
OUT bind is not described (not `describe`-d) as a normal result set, so names cannot come from
metadata. The source — `QcSqlStatement::returningColumnNames`
(populated unconditionally, for all five drivers, as a copy of
`m_returning`), actually read only on Oracle.

**Duplicate column names — intentionally not resolved.** A self-join or
ambiguous `JOIN` without aliases produces two columns with the same name —
`std::map::operator[]` in `toNamedResultSet()` silently keeps only the
last one as ordered by the `SELECT` list, without error. The same principle already
applied to `quoteRef()`/`addFreeText` — if columns need to be distinguished,
alias them yourself.

## `QueryCreator` facade

The top-level facade, connecting the query builder and `QcConnectionPool` in
one call. Owns `QcConnectionPool` (built from `QcConnectionParams` in the
constructor, defaulting to `Permanent`/`size=1`) — does not take it by
reference: the facade's purpose — "configure once, then one call per query".
The manual path through `QcConnectionPool`/`lease.connection().execute(...)`
remains available alongside, for cases where explicit control over a
specific `Lease` matters (e.g., multiple operations within one connection
lease, or a transaction spanning several different statements).

`execute()`/`executeNamed()` — five explicit overloads for concrete
builder types (`QcSqlQuery`/`QcSqlInsert`/`QcSqlUpdate`/`QcSqlDelete`) plus
`(sql, params)`, not a single template method — the set of types with `toSql() const ->
QcSqlStatement` is closed and known in advance; a template here would not remove
duplication. `execute(insert/update/delete)` go through a common private
`executeStatement()` calling `lease.connection().executeReturning(...)` —
which lets Oracle's `RETURNING ... INTO` work transparently through the facade,
with no behavioral difference on the other four drivers.
`execute(const QcSqlInsert&)` additionally checks
`statement.mysqlReturningSelectSql` and, if non-empty, goes through the
MySQL emulation (see above) instead of the normal path. `executeNamed(...)` —
a mirrored set via `executeStatementNamed()`/`executeReturningNamed()`.
