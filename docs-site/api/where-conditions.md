# WHERE Conditions (`QcSqlQueryElement`)

`QcSqlQueryElement` represents a single WHERE or HAVING condition:
a column, a comparison operator, and a value (or subquery).

`where()` / `and_()` / `or_()` return `QcSqlQueryElement &`, on which you
call exactly one comparator. Comparators return `bool` (success of setting
the condition), not `&` — they cannot be chained back onto the owning query.

## Basic Comparisons

```cpp
query.where("id").isEqualTo(5LL);
query.and_("name").isLike("%test%");
query.or_("status").isNotEqualTo(0LL);
```

## All Comparators

### Equality & Inequality

```cpp
element.isEqualTo(val);           // column = ?
element.isNotEqualTo(val);        // column != ?

// With subquery
element.isEqualTo(subQuery);      // column = (SELECT ...)
element.isNotEqualTo(subQuery);   // column != (SELECT ...)
```

### Order Comparisons

```cpp
element.isGreaterThan(val);
element.isGreaterThanOrEqualTo(val);
element.isLessThan(val);
element.isLessThanOrEqualTo(val);
```

### Pattern Matching (LIKE/ILIKE)

```cpp
element.isLike("%pat%");
element.isIlike("%pat%");        // Case-insensitive LIKE
element.isNotLike("%pat%");
element.isNotILike("%pat%");
```

ILIKE is native on PostgreSQL only. On other drivers it degrades to
`LOWER(column) LIKE LOWER(?)`.

### NULL Checks

```cpp
element.isNull();
element.isNotNull();
```

### IN / NOT IN

```cpp
// Value list
element.isIn({1LL, 2LL, 3LL});
element.isNotIn({1LL, 2LL});

// Subquery
element.isIn(subQuery);
element.isNotIn(subQuery);
```

### BETWEEN / NOT BETWEEN

```cpp
element.isBetween(1LL, 10LL);
element.isNotBetween(1LL, 10LL);
```

### NULL-Safe Comparison

```cpp
element.isDistinctFrom(val);     // IS DISTINCT FROM
element.isNotDistinctFrom(val);  // IS NOT DISTINCT FROM
```

Native on PostgreSQL (`IS [NOT] DISTINCT FROM`) and emulated via `IS`/`IS NOT`
on SQLite (SQLite's `IS`/`IS NOT` are already NULL-safe, just spelled the
other way around). On MySQL, `isNotDistinctFrom` renders `a <=> b`
(NULL-safe equality) and `isDistinctFrom` renders `NOT (a <=> b)`.

On Oracle/MSSQL there is no native operator, so the condition is spelled
out as a NULL-safe expression — **not** the naive `a = b OR (a IS NULL AND
b IS NULL)` form, which is a three-valued-logic trap (when exactly one side
is NULL, `a = b` itself evaluates to `NULL`, not `FALSE`, silently dropping
rows that should match). The actual rendering guards the equality so it only
runs once both sides are known non-NULL:

```sql
-- isNotDistinctFrom, Oracle/MSSQL:
((a IS NULL AND b IS NULL) OR (a IS NOT NULL AND b IS NOT NULL AND a = b))
-- isDistinctFrom, Oracle/MSSQL: the same expression wrapped in NOT (...)
```

On MSSQL specifically, because placeholders are positional (`?`), a value
operand (not a subquery) is bound once per textual occurrence of `b` above
(three times).

### EXISTS / NOT EXISTS

```cpp
element.exists(subQuery);
element.notExists(subQuery);
```

EXISTS leaves `m_columnName` untouched (it's meaningless for
`EXISTS (subquery)` and is not rendered).

### CAST Comparison

```cpp
element.cast(
    QcSqlBase::dataTypes::_string_,
    QcSqlBase::dataTypes::_int_,
    5LL
);
// WHERE CAST("column" AS INTEGER) = $1   (PostgreSQL; type name is driver-specific,
                                            // e.g. "INT" on MSSQL — see SQL Dialects)
```

Also accepts a subquery on the right side:
```cpp
element.cast(_string_, _int_, subQuery);
// WHERE CAST("column" AS INTEGER) = (SELECT ...)
```

## AND / OR / Parentheses

```cpp
query.where("a").isEqualTo(1LL);
query.and_OpenParenthesis("b").isEqualTo(2LL);  // AND (b = ? OR c = ?)
query.or_("c").isEqualTo(3LL);
query.closeParenthesis();

// Same without sugar:
query.and_("b");
query.openParenthesis();
// ... conditions ...
query.closeParenthesis();
```

`openParenthesis()` / `closeParenthesis()` work only on the WHERE chain, not HAVING.
The balance is tracked internally — `closeParenthesis()` returns `false` if there
is nothing to close.

An unclosed parenthesis group, or a `where()`/`and_()`/`or_()` call whose
element never got a comparator (like the "Basic Comparisons" example above,
minus the `.isEqualTo(...)` part), is caught by the owning query's `toSql()`
— see [Query Validation](/api/select-queries#query-validation).

## HAVING Conditions

`having()`, `and_Having()`, `or_Having()` accept the same `QcSqlQueryElement`
comparator vocabulary, applied after `GROUP BY`:

```cpp
query.addReturnValue("department_id");
query.addReturnValue("* <cnt>").count();
query.groupBy({"department_id"});
query.having("COUNT(*)").isGreaterThan(5LL);
```

Column names that do not look like simple identifiers (e.g. `"COUNT(*)"`)
pass through quoting untouched — use this for aggregate expressions.

## State Reset

Every comparator calls `resetOperands()` before setting its own — previous
`m_value`, `m_values`, and `m_subQuery` are cleared. This prevents stale
state from leaking across consecutive `is*()` calls on the same element.
