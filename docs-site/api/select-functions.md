# SELECT Functions (`QcSqlQueryValue`)

`QcSqlQueryValue` represents one value in the SELECT list: a column
(optionally with alias via `"col <alias>"` convention), wrapped by
a chain of functions. All functions return `QcSqlQueryValue &` for
fluent chaining.

```cpp
query.addReturnValue("id");                        // plain column
query.addReturnValue("name <display_name>");       // column + alias
query.addReturnValue("balance <balance_display>")  // column + alias + chain
    .upperCase()
    .cast(QcSqlBase::dataTypes::_string_, QcSqlBase::dataTypes::_float_)
    .round(2);
```

`QcSqlQueryValue` **never** produces bind parameters — all function operands
are rendered as literal SQL text. The `toSql()` signatures take no `params` argument.

## Available Functions

### cast(from, to)

```cpp
value.cast(QcSqlBase::dataTypes::_string_, QcSqlBase::dataTypes::_int_);
// CAST(expr AS INTEGER) on PostgreSQL/SQLite (default driver: PostgreSQL)
// CAST(expr AS INT) on MSSQL
// CAST(expr AS SIGNED) on MySQL
// CAST(expr AS NUMBER) on Oracle
```

`from` and `to` are `QcSqlBase::dataTypes` enum values: `_int_`, `_float_`,
`_string_`, `_date_`, `_bool_`, `_decimal_`, `_datetime_`, `_text_`, `_blob_`,
`_json_`.

Type names are driver-specific (centralized in `QcSqlDialect::dataTypeName()`):
- MySQL `CAST()` accepts a narrow target list (`SIGNED`, `CHAR`, `DATE`, etc.)
- Oracle has no SQL `BOOLEAN` — substitutes `NUMBER(1)`
- MSSQL/Oracle have no `JSON` type — substitutes `NVARCHAR(MAX)`/`CLOB`

### round(precision)

```cpp
value.round(2);
// ROUND(expr, 2)
```

### upperCase() / lowerCase()

```cpp
value.upperCase();  // UPPER(expr)
value.lowerCase();  // LOWER(expr)
```

### concat({operand1, operand2, ...})

```cpp
value.concat({"first_name", "' '", "last_name"});
// CONCAT(first_name, ' ', last_name) on PostgreSQL/MySQL/MSSQL
// first_name || ' ' || last_name on Oracle/SQLite
```

Each operand is either a column name (auto-quoted if it looks like an
identifier) or raw SQL text/literal left as-is. Oracle's `CONCAT()` only
takes two arguments; SQLite before 3.44 has no `CONCAT()` — both fall back
to the `||` operator.

### Aggregates: count / sum / avg / min / max

```cpp
value.count();           // COUNT(expr)
value.count(true);       // COUNT(DISTINCT expr)
value.sum();             // SUM(expr)
value.avg();             // AVG(expr)
value.min();             // MIN(expr)
value.max();             // MAX(expr)
```

`COUNT(*)` — use `addReturnValue("* <alias>").count()`. The `"*"` wildcard
is never quoted.

### coalesce({fallback1, fallback2, ...})

```cpp
value.coalesce({"'N/A'"});
// COALESCE(expr, 'N/A')
```

Same raw-expression operand convention as `concat()`.

### nullIf(compareExpr)

```cpp
value.nullIf("0");
// NULLIF(expr, 0)
```

### trim()

```cpp
value.trim();
// TRIM(expr)  (ANSI — both sides, identical on all five drivers)
```

### substring(start, length)

```cpp
value.substring(1, 10);
// SUBSTR(expr, 1, 10) on PostgreSQL/SQLite/Oracle/MySQL
// SUBSTRING(expr, 1, 10) on MSSQL
```

1-based start position.

### length()

```cpp
value.length();
// LENGTH(expr) on PostgreSQL/SQLite/Oracle/MySQL
// LEN(expr) on MSSQL (also trims trailing spaces)
```

### replace(search, replacement)

```cpp
value.replace("'old'", "'new'");
// REPLACE(expr, 'old', 'new')
```

### case_({whenThenPairs}, elseResult)

Searched `CASE WHEN`:

```cpp
value.case_({
    {"salary > 100000", "'High'"},
    {"salary > 50000",  "'Medium'"}
}, "'Low'");
// CASE WHEN salary > 100000 THEN 'High'
//      WHEN salary > 50000  THEN 'Medium'
//      ELSE 'Low' END
```

**Unlike** other functions in this list, `case_()` does **not** wrap the
current chain expression — it discards it and builds a new expression.
Intended as the first (or only) call in a chain.

### extract(part) / dateAdd(part, amount)

```cpp
value.extract(QcSqlBase::datePartTypes::_year_);
// EXTRACT(YEAR FROM expr) on PostgreSQL/MySQL/Oracle
// DATEPART(year, expr) on MSSQL
// CAST(strftime('%Y', expr) AS INTEGER) on SQLite

value.dateAdd(QcSqlBase::datePartTypes::_day_, 7);
// expr + INTERVAL '7 DAY' on PostgreSQL          (unit keyword uppercase)
// DATE_ADD(expr, INTERVAL 7 DAY) on MySQL         (unit keyword uppercase)
// DATEADD(day, 7, expr) on MSSQL                  (unit keyword lowercase)
// datetime(expr, '+7 days') on SQLite             (unit name lowercase, plural)
// expr + INTERVAL '7' DAY on Oracle               (unit keyword uppercase)
```

`datePartTypes` vocabulary: `_year_`, `_month_`, `_day_`, `_hour_`, `_minute_`,
`_second_`.

Negative `amount` subtracts. `dateAdd()` has the most dialect-divergent
rendering among all functions.

## JSON Extraction Functions

See the dedicated [JSON Fields](/api/json-fields) page for
`jsonExtract()`, `jsonExtractNumber()`, and `jsonExtractRaw()`.

## Chain Composition

Each function wraps the result of the previous one in call order:

```cpp
query.addReturnValue("balance <rounded>")
    .cast(_string_, _float_)
    .round(2);
// ROUND(CAST("balance" AS DOUBLE PRECISION), 2) AS "rounded"   (PostgreSQL; "FLOAT" on MSSQL)
```
