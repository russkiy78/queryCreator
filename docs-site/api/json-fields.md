# JSON Fields

The query builder provides a cross-driver JSON API for both WHERE conditions
and SELECT expressions. One method call renders correctly for all five
drivers — no per-driver branching needed at the call site.

## `jsonSearchPath` Convention

The path format uses `.` for nested object keys and `[N]` for array indices,
with **no leading root marker**:

| Path | Meaning |
|------|---------|
| `"level"` | The `level` key at the root |
| `"a.b"` | Object `a` → nested key `b` |
| `"skills[0]"` | Array `skills` → first element |
| `"a.b[2].c"` | Object `a` → nested object `b` → 3rd array element → key `c` |

## WHERE-Side: `isXxxJson...()` Comparators

Compare a value extracted from a JSON column instead of the column itself.
All are on `QcSqlQueryElement` and follow the same `where("column").isXxxJson...(value, path)` pattern.

```cpp
query.fromTable("employees");
query.where("metadata").isEqualToJsonNumber(5LL, "level");
```

### Numeric Comparisons (6 comparators)

The extracted value is coerced to a numeric SQL type — `10 > 9` (numeric),
not `"10" > "9"` (lexicographic, which would be false):

```cpp
element.isEqualToJsonNumber(val, path);
element.isNotEqualToJsonNumber(val, path);
element.isGreaterThanJsonNumber(val, path);
element.isGreaterThanOrEqualToJsonNumber(val, path);
element.isLessThanJsonNumber(val, path);
element.isLessThanOrEqualToJsonNumber(val, path);
```

### Text Comparisons (10 comparators)

The extracted scalar is returned as unwrapped text — a JSON string
`"admin"` becomes bare `admin`:

```cpp
element.isEqualToJsonText(val, path);
element.isNotEqualToJsonText(val, path);
element.isGreaterThanJsonText(val, path);
element.isGreaterThanOrEqualToJsonText(val, path);
element.isLessThanJsonText(val, path);
element.isLessThanOrEqualToJsonText(val, path);

// Pattern matching
element.isLikeJsonText("c%", path);
element.isIlikeJsonText("c%", path);
element.isNotLikeJsonText("c%", path);
element.isNotILikeJsonText("c%", path);
```

ILIKE variants are native on PostgreSQL only; other drivers degrade to
`LOWER(json_extract(...)) LIKE LOWER(?)`.

### Array-as-Text Comparisons (2 comparators)

Match a LIKE pattern against the **raw serialized JSON** at the path
(e.g. an entire array, still bracketed: `["c++","sql","python"]`). This
searches across every element at once, unlike the single-element `[N]`-indexed
comparators above:

```cpp
element.isLikeJsonArrayAsText("%\"python\"%", "skills");
// Finds "python" anywhere in the skills array

element.isIlikeJsonArrayAsText("%\"python\"%", "skills");
// Case-insensitive variant
```

## SELECT-Side: `jsonExtract...()` Functions

Three methods on `QcSqlQueryValue` mirror the three value kinds, wrapping
the current chain expression (like any other SELECT function):

```cpp
query.addReturnValue("metadata <level>").jsonExtract("level");
// Scalar text, unquoted — common case

query.addReturnValue("metadata <rating>").jsonExtractNumber("rating");
// Coerced to numeric type — for ordering/arithmetic

query.addReturnValue("metadata <skills>").jsonExtractRaw("skills");
// Raw JSON fragment — array/object keeps its brackets/braces
```

### Chaining Further Functions

`jsonExtract...()` wraps the current expression like any other function:

```cpp
query.addReturnValue("metadata <rating>")
    .jsonExtractNumber("rating")
    .round(1);
// ROUND(<json-extract-as-number>(metadata, 'rating'), 1)
```

## Per-Driver Rendering

`QcSqlDialect::jsonExtractExpr()` — the single place with per-driver
differences:

| Driver | Text (`_jsonAsText_`) | Numeric (`_jsonAsNumber_`) | Raw (`_jsonAsRaw_`) |
|--------|----------------------|---------------------------|---------------------|
| **PostgreSQL** | `jsonb_extract_path_text(col::jsonb, 'a', 'b')` | `(...)::numeric` | `jsonb_extract_path(...)::text` |
| **SQLite** | `json_extract(col, '$.a.b')` | identical | identical |
| **MySQL** | `JSON_UNQUOTE(JSON_EXTRACT(col, '$.a.b'))` | `(JSON_EXTRACT(...) + 0)` | `JSON_EXTRACT(...)` |
| **MSSQL** | `JSON_VALUE(col, '$.a.b')` | `CAST(JSON_VALUE(...) AS FLOAT)` | `JSON_QUERY(col, '$.a.b')` |
| **Oracle** | `JSON_VALUE(col, '$.a.b')` | `JSON_VALUE(... RETURNING NUMBER)` | `JSON_QUERY(col, '$.a.b')` |

## JSON Value Kinds

| Kind | `QcSqlBase::jsonValueKind` | Method Suffix | Extracts As |
|------|---------------------------|---------------|-------------|
| Text | `_jsonAsText_` | `JsonText` / `jsonExtract()` | Scalar, unwrapped (no quotes) |
| Number | `_jsonAsNumber_` | `JsonNumber` / `jsonExtractNumber()` | SQL numeric type |
| Raw | `_jsonAsRaw_` | `JsonArrayAsText` / `jsonExtractRaw()` | Raw JSON (keeps brackets/braces) |
