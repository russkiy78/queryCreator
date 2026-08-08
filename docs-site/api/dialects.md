# SQL Dialects (`QcSqlDialect`)

`QcSqlDialect` is a namespace of free functions that centralize every
place SQL generation differs by driver. Each function takes an explicit
`QcDbDriver driver` parameter (defaults to `QcDbDriver::PostgreSQL`).

Unlike `QcNativeConnection`, nothing here has an external dependency —
it's all pure string rendering, so every driver's branch is always compiled
regardless of `QC_DB_DRIVERS`.

## Placeholders

```cpp
std::string placeholder(std::size_t index, QcDbDriver driver);
```

| Driver | Format | Example |
|--------|--------|---------|
| PostgreSQL | `$N` | `$1`, `$2`, `$3` |
| Oracle | `:N` | `:1`, `:2`, `:3` |
| MySQL | `?` | `?`, `?`, `?` |
| SQLite | `?` | `?`, `?`, `?` |
| MSSQL | `?` | `?`, `?`, `?` |

The `index` is 1-based (the global position in the entire statement, across
all subqueries, WHERE fragments, etc.). Callers pass `params.size()` right
after appending the value.

## Data Type Names

```cpp
std::string dataTypeName(int dataType, QcDbDriver driver);
```

Maps `QcSqlBase::dataTypes` enum values to SQL type names for
`CAST(expr AS <type>)`. Approximations exist for drivers with limited
CAST target lists:

- MySQL `CAST()` accepts only `SIGNED`/`CHAR`/`DATE`/`DATETIME`/`DECIMAL`/
  `DOUBLE`/`JSON` — no `VARCHAR`, `BOOLEAN`, `TEXT`, or `BLOB` as cast targets
- Oracle has no SQL `BOOLEAN` — substitutes `NUMBER(1)`
- MSSQL/Oracle have no `JSON` type — substitutes `NVARCHAR(MAX)`/`CLOB`

## LIMIT / OFFSET

```cpp
std::string limitOffsetClause(int rowsCount, int startRow, bool withTies, QcDbDriver driver);
```

| Driver | Syntax |
|--------|--------|
| PostgreSQL | `LIMIT n OFFSET m` (accepts bare `OFFSET` without `LIMIT`) |
| MySQL/SQLite | `LIMIT n OFFSET m` (require `LIMIT` alongside `OFFSET`) |
| MSSQL/Oracle | `OFFSET m ROWS FETCH NEXT n ROWS ONLY` (ANSI, only form supported) |

`withTies` always selects ANSI form (`... WITH TIES` instead of `... ONLY`).
SQLite has no `WITH TIES` — requesting it emits ANSI syntax that fails at
execution (documented limitation).

MySQL fills a bare-OFFSET with `LIMIT 18446744073709551615` (documented
2⁶⁴−1 sentinel); SQLite uses `LIMIT -1` ("no limit" idiom).

## CONCAT

```cpp
std::string concatExpr(const std::vector<std::string> & operands, QcDbDriver driver);
```

| Driver | Form |
|--------|------|
| PostgreSQL/MySQL/MSSQL | `CONCAT(op1, op2, ...)` (variadic) |
| Oracle/SQLite | `op1 \|\| op2 \|\| ...` (operator, binary `CONCAT` / no `CONCAT`) |

Oracle `CONCAT()` takes exactly two arguments; SQLite before 3.44 (2023)
has no `CONCAT()` at all. Both use the `||` operator.

## RETURNING / OUTPUT

```cpp
std::string returningClause(const QcStringList & columns, const std::string & mssqlRowKeyword,
                             std::size_t oracleFirstPlaceholderIndex, QcDbDriver driver);
```

| Driver | Clause | Position |
|--------|--------|----------|
| PostgreSQL/SQLite | `RETURNING "col1", "col2"` | End of statement |
| MSSQL | `OUTPUT inserted."col1", inserted."col2"` | Between columns/SET and VALUES/WHERE |
| Oracle | `RETURNING "col1", "col2" INTO :N, :N+1` | End of statement |
| MySQL | *(empty — no equivalent)* | N/A |

- `mssqlRowKeyword`: `"inserted"` for INSERT/UPDATE, `"deleted"` for DELETE
- `oracleFirstPlaceholderIndex`: `params.size() + 1` — start of OUT-bind numbering

## Identifier Quoting

### quoteIdentifier(name)

Quotes a single identifier segment (no dots):

| Driver | Style | Example |
|--------|-------|---------|
| PostgreSQL | ANSI double quotes | `"a""b"` for `a"b` |
| SQLite | ANSI double quotes | `"a""b"` |
| Oracle | ANSI double quotes | `"a""b"` |
| MySQL | Backticks | `` `a``b` `` for `` a`b `` |
| MSSQL | Brackets | `[a]]b]` for `a]b` |

Empty string and `"*"` are returned unchanged.

Oracle's quoting is standard ANSI case-preserving — identical to PostgreSQL.
The one Oracle-specific consequence: Oracle folds *unquoted* identifiers to
UPPERCASE at creation (PostgreSQL/SQLite fold to lowercase), so DDL must
use quoted, case-matching identifiers when `quoteIdentifier()` is in the
rendering path.

### quoteRef(name)

Quotes a possibly dot-qualified reference (`table.column`). Each segment is
quoted individually via `quoteIdentifier()`.

Only quotes if **every** segment looks like a plain identifier
(`[A-Za-z_][A-Za-z0-9_$]*` or `*`) — otherwise returns `name` unchanged.
This is intentional: column-name parameters throughout the API
(`where()`, `groupBy()`, `orderAsc()`, `addReturnValue()`, ...) are also
this builder's escape hatch for SQL expressions with no dedicated method
(e.g. `"COUNT(*)"`, `"metadata->>'level'"`).

### quoteTableRef(name)

Quotes a FROM/JOIN table reference that may carry an inline alias
(e.g. `"employees e"`). Splits on the first whitespace: table part via
`quoteRef()`, alias part via `quoteIdentifier()`.

## LENGTH / SUBSTR / EXTRACT / DATEADD

### lengthExpr(expr)

| Driver | Function | Note |
|--------|----------|------|
| PostgreSQL/SQLite/Oracle/MySQL | `LENGTH(expr)` | |
| MSSQL | `LEN(expr)` | Also trims trailing spaces |

### substringExpr(expr, start, length)

| Driver | Function |
|--------|----------|
| PostgreSQL/SQLite/Oracle/MySQL | `SUBSTR(expr, start, length)` |
| MSSQL | `SUBSTRING(expr, start, length)` |

1-based `start`.

### extractExpr(expr, part)

| Driver | Form |
|--------|------|
| PostgreSQL/MySQL/Oracle | `EXTRACT(YEAR FROM expr)` (ANSI) |
| MSSQL | `DATEPART(year, expr)` |
| SQLite | `CAST(strftime('%Y', expr) AS INTEGER)` |

### dateAddExpr(expr, part, amount)

| Driver | Form |
|--------|------|
| PostgreSQL | `expr + INTERVAL 'N unit'` |
| MySQL | `DATE_ADD(expr, INTERVAL N unit)` |
| MSSQL | `DATEADD(unit, N, expr)` |
| SQLite | `datetime(expr, '+N unit')` |
| Oracle | `expr + INTERVAL 'N' unit` |

`part` values: `_year_`, `_month_`, `_day_`, `_hour_`, `_minute_`, `_second_`.
Negative `amount` subtracts.

## ORDER BY Entry

```cpp
std::string orderByEntry(const std::string & column, bool descending, int nulls, QcDbDriver driver);
```

| Driver | NULLS Support |
|--------|---------------|
| PostgreSQL/SQLite (3.30+)/Oracle | Native `NULLS FIRST` / `NULLS LAST` |
| MySQL/MSSQL | Emulated via `CASE WHEN col IS NULL THEN 0 ELSE 1 END` tiebreaker |

`nulls` values: `_nullsDefault_` (bare, driver's own unspecified default),
`_nullsFirst_`, `_nullsLast_`.

## JSON Extraction (jsonExtractExpr)

Full breakdown on the [JSON Fields](/api/json-fields) page. Five distinct
native path-extraction mechanisms, three value kinds
(`_jsonAsText_`/`_jsonAsNumber_`/`_jsonAsRaw_`).

## IS DISTINCT FROM

Rendered directly in `qcsqlqueryelement.cpp` (not in `QcSqlDialect`) because
it needs to call back into `bind()` for operand appending:

| Driver | Rendering |
|--------|-----------|
| PostgreSQL/SQLite | Native `IS DISTINCT FROM` |
| Oracle/MSSQL | `(a = b OR (a IS NULL AND b IS NULL))` |
| MySQL | `a <=> b` |

On MSSQL, the same operand value may be bound multiple times (positional `?`
is not reusable).
