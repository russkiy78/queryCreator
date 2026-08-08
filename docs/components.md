# queryCreator — components

The public API of each file in `src/query/`, in two or three sentences plus
signatures. For the rationale behind architectural decisions —
[architecture.md](architecture.md); for practical usage examples —
[usage.md](usage.md).

## Common types

### `qcsqlbase.h` — `QcSqlBase`

Common base class (inherited by all six query builder classes) with shared
type aliases and API vocabulary enums:

```cpp
using QcVariant = std::variant<std::monostate, long long, double, std::string, std::vector<std::byte>>;
using QcVariantList = std::vector<QcVariant>;
using QcStringList = std::vector<std::string>;

enum compareTypes { _isLike_, _isILike_, _isNotLike_, _isNotILike_, _isEqualTo_, _isNotEqualTo_,
                 _isGreaterThan_, _isGreaterThanOrEqualTo_, _isLessThan_, _isLessThanOrEqualTo_,
                 _isNull_, _isNotNull_, _isIn_, _isNotIn_, _isBetween_, _isNotBetween_,
                 _isDistinctFrom_, _isNotDistinctFrom_, _exists_, _notExists_,
                 _openParenthesis_, _closeParenthesis_ };
enum functionTypes { _no_function_, _cast_, _concat_, _round_, _upperCase_, _lowerCase_,
                 _count_, _sum_, _avg_, _min_, _max_,
                 _coalesce_, _nullIf_, _trim_, _substring_, _length_, _replace_, _case_,
                 _extract_, _dateAdd_, _jsonExtract_ };
enum dataTypes { _int_, _float_, _string_, _date_, _bool_, _decimal_, _datetime_, _text_, _blob_, _json_ };
enum joinTypes { _innerJoin_, _leftJoin_, _rightJoin_, _fullJoin_, _crossJoin_ };
enum setOperationTypes { _union_, _unionAll_, _intersect_, _except_ };
enum datePartTypes { _year_, _month_, _day_, _hour_, _minute_, _second_ }; // extract()/dateAdd()
enum jsonValueKind { _jsonAsText_, _jsonAsNumber_, _jsonAsRaw_ }; // JSON query API, see usage.md §1.9
```

Plus `struct QcSqlStatement { std::string sql; QcVariantList params;
std::size_t returningColumnCount = 0; std::string mysqlReturningSelectSql;
QcStringList returningColumnNames; }` — the result of `toSql()` for any builder;
the last three fields are meaningful only for `returning()` on Oracle/MySQL (see
[architecture.md](architecture.md#returning-on-oracle-and-mysql)).

### `qcdbdriver.h`

```cpp
enum class QcDbDriver { PostgreSQL, Oracle, MySQL, SQLite, MSSQL };
```

Runtime driver selection — see `QcConnectionParams::driver` and
[architecture.md](architecture.md#native-db-drivers-two-independent-decisions).

### `qcdeepptr.h` — `QcDeepPtr<T>`

Header-only template wrapper over `std::unique_ptr<T>`: copying deeply
clones `*T`, rather than sharing ownership or shallow-copying the pointer;
moves/deletion — free via `unique_ptr`. Used everywhere
the query builder stores a subquery (`QcSqlQuery::m_fromSubquery`,
`QcSqlQueryElement::m_subQuery`, ...) — see
[architecture.md](architecture.md#subquery-ownership--qcdeepptrt).

```cpp
template <typename T> class QcDeepPtr {
public:
    QcDeepPtr();
    QcDeepPtr(std::nullptr_t);
    explicit QcDeepPtr(const T & value);
    T * get() const;
    T * operator->() const;
    T & operator*() const;
    explicit operator bool() const;
};
```

## Query builder

### `qcsqlqueryvalue.h`/`.cpp` — `QcSqlQueryValue`

One value in the `SELECT`-list: a column (or `"col <alias>"`), wrapped
by a function chain. All functions return `QcSqlQueryValue &` (full
fluent chaining). The full list of functions and their SQL semantics —
[usage.md §1.8](usage.md#18-functions-in-the-select-list-qcsqlqueryvalue).

```cpp
class QcSqlQueryValue : public QcSqlBase {
public:
    QcSqlQueryValue();
    explicit QcSqlQueryValue(const std::string & name); // parses "col <alias>"

    QcSqlQueryValue & useDriver(QcDbDriver driver);

    QcSqlQueryValue & concat(const QcStringList & concateVal);
    QcSqlQueryValue & cast(const int & from, const int & to);
    QcSqlQueryValue & round(const int & precision);
    QcSqlQueryValue & upperCase();
    QcSqlQueryValue & lowerCase();
    QcSqlQueryValue & count(bool distinct = false);
    QcSqlQueryValue & sum();
    QcSqlQueryValue & avg();
    QcSqlQueryValue & min();
    QcSqlQueryValue & max();
    QcSqlQueryValue & coalesce(const QcStringList & fallbacks);
    QcSqlQueryValue & nullIf(const std::string & compareExpr);
    QcSqlQueryValue & trim();
    QcSqlQueryValue & substring(int start, int length);
    QcSqlQueryValue & length();
    QcSqlQueryValue & replace(const std::string & search, const std::string & replacement);
    QcSqlQueryValue & case_(const std::vector<std::pair<std::string, std::string>> & whenThenPairs,
                            const std::optional<std::string> & elseResult = std::nullopt);
    QcSqlQueryValue & extract(int part);      // part: datePartTypes
    QcSqlQueryValue & dateAdd(int part, int amount);

    // JSON query API, see usage.md §1.9 -- jsonSearchPath: "a.b[2].c" (no
    // leading root marker). Text: scalar, unquoted. Number: coerced to a
    // numeric SQL type. Raw: the value's own JSON-encoded text (array/object
    // keeps its brackets/braces).
    QcSqlQueryValue & jsonExtract(const std::string & jsonSearchPath);
    QcSqlQueryValue & jsonExtractNumber(const std::string & jsonSearchPath);
    QcSqlQueryValue & jsonExtractRaw(const std::string & jsonSearchPath);

    std::string toSql() const;
    std::string toSql(QcDbDriver driver) const;
};
```

Never produces bind parameters — `cast()`/`round()`'s `int`-operands
and `concat()`/`coalesce()`/... operands are always rendered as literal SQL
text, hence the absence of `params` in the `toSql()` signature.

### `qcsqlqueryelement.h`/`.cpp` — `QcSqlQueryElement`

One `WHERE`/`HAVING` condition: column + comparison operator + value or
subquery. Comparators return `bool` (success of setting the condition), not
`&` — they cannot be chained back onto the owning query in a single expression.
Full list and examples — [usage.md §1.4](usage.md#14-where-comparison-operators-andor-parentheses-raw-text).

```cpp
class QcSqlQueryElement : public QcSqlBase {
public:
    QcSqlQueryElement();
    explicit QcSqlQueryElement(const std::string & columnName);
    explicit QcSqlQueryElement(const int & compareType); // paren-marker
    QcSqlQueryElement(const int & compareType, const std::string & columnName, const QcVariant & val);
    QcSqlQueryElement(const int & compareType, const std::string & columnName, const QcSqlQuery & subQuery);

    QcSqlQueryElement & useDriver(QcDbDriver driver);

    bool isLike(const std::string & val);       bool isIlike(const std::string & val);
    bool isNotLike(const std::string & val);     bool isNotILike(const std::string & val);
    bool isEqualTo(const QcVariant & val);       bool isEqualTo(const QcSqlQuery & subQuery);
    bool isNotEqualTo(const QcVariant & val);    bool isNotEqualTo(const QcSqlQuery & subQuery);
    bool isGreaterThan(const QcVariant & val);   bool isGreaterThanOrEqualTo(const QcVariant & val);
    bool isLessThan(const QcVariant & val);      bool isLessThanOrEqualTo(const QcVariant & val);
    bool isNull();                                bool isNotNull();
    bool isIn(const QcVariantList & val);        bool isIn(const QcSqlQuery & subQuery);
    bool isNotIn(const QcVariantList & val);     bool isNotIn(const QcSqlQuery & subQuery);
    bool isBetween(const QcVariant & val1, const QcVariant & val2);
    bool isNotBetween(const QcVariant & val1, const QcVariant & val2);
    bool isDistinctFrom(const QcVariant & val);  bool isDistinctFrom(const QcSqlQuery & subQuery);
    bool isNotDistinctFrom(const QcVariant & val); bool isNotDistinctFrom(const QcSqlQuery & subQuery);
    bool exists(const QcSqlQuery & subQuery);    bool notExists(const QcSqlQuery & subQuery);
    bool cast(const int & from, const int & to, const QcVariant & val);
    bool cast(const int & from, const int & to, const QcSqlQuery & subQuery);

    // JSON query API, see usage.md §1.9 -- compares a value pulled out of a
    // JSON column at jsonSearchPath ("a.b[2].c", no leading root marker)
    // instead of comparing the column itself. Number/Text pick numeric vs.
    // scalar-text coercion (isEqualToJsonNumber/isEqualToJsonText/... —
    // 6 comparators each); isXxxJsonText (Like/Ilike/NotLike/NotILike, 4)
    // matches a scalar's own text; isXxxJsonArrayAsText (Like/Ilike, 2)
    // matches the raw serialized JSON at the path instead (e.g. a whole
    // array), for searching across every element at once.
    bool isEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isNotEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isGreaterThanJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isGreaterThanOrEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLessThanJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLessThanOrEqualToJsonNumber(const QcVariant & val, const std::string & jsonSearchPath);
    bool isEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isNotEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isGreaterThanJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isGreaterThanOrEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLessThanJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLessThanOrEqualToJsonText(const QcVariant & val, const std::string & jsonSearchPath);
    bool isLikeJsonText(const std::string & val, const std::string & jsonSearchPath);
    bool isIlikeJsonText(const std::string & val, const std::string & jsonSearchPath);
    bool isNotLikeJsonText(const std::string & val, const std::string & jsonSearchPath);
    bool isNotILikeJsonText(const std::string & val, const std::string & jsonSearchPath);
    bool isLikeJsonArrayAsText(const std::string & val, const std::string & jsonSearchPath);
    bool isIlikeJsonArrayAsText(const std::string & val, const std::string & jsonSearchPath);

    std::string toSql(QcVariantList & params) const;
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;
    bool isOpenParenthesisMarker() const;
    bool isCloseParenthesisMarker() const;

    static std::string renderChain(const std::vector<std::pair<int, QcSqlQueryElement>> & chain,
                                    QcVariantList & params, QcDbDriver driver = QcDbDriver::PostgreSQL);
};
```

`renderChain()` — common renderer for any WHERE/HAVING-chain (used by
`QcSqlQuery`, as well as `QcSqlUpdate`/`QcSqlDelete` for their WHERE).

### `qcsqlquery.h`/`.cpp` — `QcSqlQuery`

`SELECT` builder: the only one of the six classes with `FROM`/`JOIN`/CTE/`GROUP
BY`/`HAVING`/set-operations. Detailed examples for each API section —
[usage.md §1](usage.md#1-building-a-select-query-qcsqlquery).

```cpp
class QcSqlQuery : public QcSqlBase {
public:
    QcSqlQuery();
    QcSqlQuery & useDriver(QcDbDriver driver);
    QcSqlQuery & addFreeText(const std::string & text, const QcVariantList & values);

    QcSqlQuery & distinct();
    QcSqlQuery & distinctOn(const QcStringList & columns);        // PostgreSQL only, otherwise -> DISTINCT
    bool addReturnValues(const QcStringList & columns);            // "col <alias>" one string per element
    QcSqlQueryValue & addReturnValue(const std::string & name);

    QcSqlQuery & fromTable(const std::string & name);               // "table" or "table alias"
    QcSqlQuery & fromSubQuery(const std::string & relationKeyOrAlias, const QcSqlQuery & joinQuery);
    QcSqlQuery & with_(const std::string & name, const QcSqlQuery & cteQuery);

    QcSqlQuery & addLeftJoin(const std::string & alias, const QcSqlQuery & joinQuery,
                              const std::string & onCondition, const bool & asSubQuery = false);
    QcSqlQuery & addJoin(...);       // INNER, same signature
    QcSqlQuery & addRightJoin(...);
    QcSqlQuery & addFullJoin(...);   // not supported on MySQL, see architecture.md
    QcSqlQuery & addCrossJoin(const std::string & alias, const QcSqlQuery & joinQuery, const bool & asSubQuery = false);

    QcSqlQueryElement & where(const std::string & column);
    QcSqlQueryElement & where_OpenParenthesis(const std::string & column);
    QcSqlQueryElement & and_(const std::string & column);
    QcSqlQueryElement & and_OpenParenthesis(const std::string & column);
    QcSqlQueryElement & or_(const std::string & column);
    QcSqlQueryElement & or_OpenParenthesis(const std::string & column);
    bool openParenthesis();
    bool closeParenthesis();

    QcSqlQuery & orderAsc(const QcStringList & columns, int nulls = _nullsDefault_); // nulls: QcSqlBase::nullsPosition
    QcSqlQuery & orderDesc(const QcStringList & columns, int nulls = _nullsDefault_);
    QcSqlQuery & groupBy(const QcStringList & columns);
    QcSqlQuery & limit(int rowsCount, int startRow = 0, bool withTies = false);

    QcSqlQueryElement & having(const std::string & column);
    QcSqlQueryElement & and_Having(const std::string & column);
    QcSqlQueryElement & or_Having(const std::string & column);

    QcSqlQuery & unionWith(const QcSqlQuery & query);
    QcSqlQuery & unionAllWith(const QcSqlQuery & query);
    QcSqlQuery & intersectWith(const QcSqlQuery & query);
    QcSqlQuery & exceptWith(const QcSqlQuery & query);

    QcSqlStatement toSql() const;
    QcSqlStatement toSql(QcDbDriver driver) const;
    std::string toSql(QcVariantList & params) const;             // threading variant, for subqueries
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;
};
```

`QcSqlQuery` — a fully (deeply) copyable type: a copy clones every
subquery it references (see `QcDeepPtr<T>` above).

### `qcsqlinsert.h`/`.cpp` — `QcSqlInsert`

Single-row `INSERT`. `returning()` details — [usage.md §2](usage.md#2-data-modification-qcsqlinsert--qcsqlupdate--qcsqldelete).

```cpp
class QcSqlInsert : public QcSqlBase {
public:
    QcSqlInsert();
    QcSqlInsert & useDriver(QcDbDriver driver);
    QcSqlInsert & into(const std::string & table);
    QcSqlInsert & set(const std::string & column, const QcVariant & value); // overwrite-in-place on repeated set()

    QcSqlInsert & returning(const QcStringList & columns);
    QcSqlInsert & returning(const QcStringList & columns, const std::string & autoIncrementColumn); // MySQL-only

    QcSqlStatement toSql() const;
    QcSqlStatement toSql(QcDbDriver driver) const;
    std::string toSql(QcVariantList & params) const;
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;
};
```

### `qcsqlupdate.h`/`.cpp` — `QcSqlUpdate`

`UPDATE table SET ... [WHERE ...]`. The WHERE-side — the same `QcSqlQueryElement`
and the same comparator/parenthesis sugar vocabulary as `QcSqlQuery`.

```cpp
class QcSqlUpdate : public QcSqlBase {
public:
    QcSqlUpdate();
    QcSqlUpdate & useDriver(QcDbDriver driver);
    QcSqlUpdate & table(const std::string & name);
    QcSqlUpdate & set(const std::string & column, const QcVariant & value);
    QcSqlUpdate & returning(const QcStringList & columns);

    QcSqlQueryElement & where(const std::string & column);
    QcSqlQueryElement & where_OpenParenthesis(const std::string & column);
    QcSqlQueryElement & and_(const std::string & column);
    QcSqlQueryElement & and_OpenParenthesis(const std::string & column);
    QcSqlQueryElement & or_(const std::string & column);
    QcSqlQueryElement & or_OpenParenthesis(const std::string & column);
    bool openParenthesis();
    bool closeParenthesis();

    QcSqlStatement toSql() const;
    QcSqlStatement toSql(QcDbDriver driver) const;
    std::string toSql(QcVariantList & params) const;
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;
};
```

### `qcsqldelete.h`/`.cpp` — `QcSqlDelete`

`DELETE FROM table [WHERE ...]` — same WHERE mechanics as
`QcSqlUpdate`, minus the SET-list.

```cpp
class QcSqlDelete : public QcSqlBase {
public:
    QcSqlDelete();
    QcSqlDelete & useDriver(QcDbDriver driver);
    QcSqlDelete & from(const std::string & table);
    QcSqlDelete & returning(const QcStringList & columns);

    QcSqlQueryElement & where(const std::string & column);
    // ... same WHERE-set as QcSqlUpdate

    QcSqlStatement toSql() const;
    QcSqlStatement toSql(QcDbDriver driver) const;
    std::string toSql(QcVariantList & params) const;
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;
};
```

### `qcsqldialect.h`/`.cpp` — `namespace QcSqlDialect`

Free functions, each explicitly taking `QcDbDriver driver = PostgreSQL`.
The single place where SQL generation actually differs by driver — see
[architecture.md](architecture.md#sql-generation--qcsqldialect) for a full
breakdown of each function.

```cpp
namespace QcSqlDialect {
std::string placeholder(std::size_t index, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string dataTypeName(int dataType, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string limitOffsetClause(int rowsCount, int startRow, bool withTies, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string concatExpr(const std::vector<std::string> & operands, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string returningClause(const QcSqlBase::QcStringList & columns, const std::string & mssqlRowKeyword,
                             std::size_t oracleFirstPlaceholderIndex, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string quoteIdentifier(const std::string & name, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string quoteRef(const std::string & name, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string quoteTableRef(const std::string & name, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string lengthExpr(const std::string & expr, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string substringExpr(const std::string & expr, int start, int length, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string extractExpr(const std::string & expr, int part, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string dateAddExpr(const std::string & expr, int part, int amount, QcDbDriver driver = QcDbDriver::PostgreSQL);
std::string jsonExtractExpr(const std::string & expr, const std::string & jsonSearchPath, int kind,
                             QcDbDriver driver = QcDbDriver::PostgreSQL);
}
```

`IS DISTINCT FROM`/`IS NOT DISTINCT FROM` is **not** here — implemented directly
in `qcsqlqueryelement.cpp` (needs access to `bind()`), see architecture.md.

## Lower level — connections and pool

### `qcconnectionparams.h` — `QcConnectionParams`

```cpp
struct QcConnectionParams {
    QcDbDriver driver = QcDbDriver::PostgreSQL;
    std::string host = "localhost";
    std::string port;
    std::string database;    // SQLite: file path; Oracle: service name
    std::string user;
    std::string password;
    int connectTimeoutSeconds = 0; // 0 = driver decides
};
```

### `qcdriverconnection.h`/`.cpp` — `IQcDriverConnection`

Common interface implemented by each driver backend (`QcPostgreSqlConnection`,
`QcOracleConnection`, `QcMySqlConnection`, `QcSqliteConnection`,
`QcMssqlConnection` — one class per `qcdriver*.cpp`).
`QcNativeConnection` — a thin owner of one of them, selected at
runtime by `QcConnectionParams::driver`.

```cpp
class IQcDriverConnection {
public:
    virtual ~IQcDriverConnection() = default;
    virtual bool isAlive() = 0;
    virtual void * nativeHandle() const = 0;
    virtual std::optional<QcResultSet> execute(const std::string & sql, const QcVariantList & params,
                                                QcStringList * outColumnNames = nullptr) = 0;
    virtual std::optional<QcResultSet> executeReturning(const std::string & sql, const QcVariantList & params,
                                                         std::size_t returningColumnCount); // default == execute()
    virtual std::optional<QcNamedResultSet> executeReturningNamed(const std::string & sql, const QcVariantList & params,
                                                                    std::size_t returningColumnCount,
                                                                    const QcStringList & returningColumnNames);
};

// toNamedResultSet(columnNames, rows) -- common helper assembling QcNamedResultSet
// from positional rows + name list; used by QcNativeConnection::executeNamed()
// and the default implementation of executeReturningNamed() above.
QcNamedResultSet toNamedResultSet(const QcStringList & columnNames, const QcResultSet & rows);
```

### `qcdriverfactory.h`

One factory function + one `*DriverInfo()` function per driver, declared
only under their respective `#ifdef QC_DB_HAS_*`:

```cpp
#ifdef QC_DB_HAS_POSTGRESQL
std::unique_ptr<IQcDriverConnection> createPostgreSqlConnection(const QcConnectionParams & params);
std::string postgreSqlDriverInfo();
#endif
// ... same for QC_DB_HAS_ORACLE/QC_DB_HAS_MYSQL/QC_DB_HAS_SQLITE/QC_DB_HAS_MSSQL
```

`create*Connection()` throws `std::runtime_error` on connection failure.

### `qcdriverpostgresql.cpp` / `qcdriveroracle.cpp` / `qcdrivermysql.cpp` / `qcdriversqlite.cpp` / `qcdrivermssql.cpp`

`IQcDriverConnection` implementation for each driver — one translation
unit per driver, compiled only if the corresponding driver is selected in
`QC_DB_DRIVERS`. Internals of each — in
[architecture.md](architecture.md#postgresql-driver) (one section per
driver).

### `qcnativeconnection.h`/`.cpp` — `QcNativeConnection`

RAII wrapper for one physical connection — thin dispatcher over
`IQcDriverConnection`, selecting the backend via `qcdriverfactory.h` by
`QcConnectionParams::driver`.

```cpp
using QcResultRow = QcSqlBase::QcVariantList;
using QcResultSet = std::vector<QcResultRow>;
using QcNamedRow = std::map<std::string, QcSqlBase::QcVariant>;
using QcNamedResultSet = std::vector<QcNamedRow>;

class QcNativeConnection {
public:
    explicit QcNativeConnection(const QcConnectionParams & params); // throws std::runtime_error
    ~QcNativeConnection();

    bool isOpen() const;
    bool isAlive();                 // active round-trip, not just local status
    void * nativeHandle() const;

    std::optional<QcResultSet> execute(const std::string & sql, const QcVariantList & params = {});
    std::optional<QcResultSet> executeReturning(const std::string & sql, const QcVariantList & params,
                                                 std::size_t returningColumnCount);
    std::optional<QcNamedResultSet> executeNamed(const std::string & sql, const QcVariantList & params = {});
    std::optional<QcNamedResultSet> executeReturningNamed(const std::string & sql, const QcVariantList & params,
                                                           std::size_t returningColumnCount,
                                                           const QcStringList & returningColumnNames);

    static std::string nativeDriverInfo(QcDbDriver driver = QcDbDriver::PostgreSQL);
};
```

### `qcconnectionpool.h`/`.cpp` — `QcConnectionPool`

Thread-safe connection pool — internals and both modes in detail in
[architecture.md](architecture.md#connection-pool-and-thread-safety).

```cpp
class QcConnectionPool {
public:
    enum class Mode { Permanent, OnDemand };

    class Lease {
    public:
        Lease(Lease &&) noexcept;
        Lease & operator=(Lease &&) noexcept;
        ~Lease();
        QcNativeConnection & connection();
        // copying is forbidden
    };

    QcConnectionPool(QcConnectionParams params, Mode mode, std::size_t size);
    ~QcConnectionPool();

    Lease acquire();                                            // blocks indefinitely
    std::optional<Lease> tryAcquire(std::chrono::milliseconds timeout);
};
```

### `querycreator.h`/`.cpp` — `QueryCreator`

Library facade: builds a query through any of the six builders, renders it
for its driver, and executes it through its own `QcConnectionPool` in one
call. Details (MySQL RETURNING emulation, Oracle OUT-binds through the
facade) — [architecture.md](architecture.md#querycreator-facade).

```cpp
class QueryCreator {
public:
    explicit QueryCreator(const QcConnectionParams & params,
                           QcConnectionPool::Mode mode = QcConnectionPool::Mode::Permanent,
                           std::size_t poolSize = 1);

    std::optional<QcResultSet> execute(const QcSqlQuery & query);
    std::optional<QcResultSet> execute(const QcSqlInsert & insert);
    std::optional<QcResultSet> execute(const QcSqlUpdate & update);
    std::optional<QcResultSet> execute(const QcSqlDelete & del);
    std::optional<QcResultSet> execute(const std::string & sql, const QcVariantList & params = {});

    std::optional<QcNamedResultSet> executeNamed(const QcSqlQuery & query);
    std::optional<QcNamedResultSet> executeNamed(const QcSqlInsert & insert);
    std::optional<QcNamedResultSet> executeNamed(const QcSqlUpdate & update);
    std::optional<QcNamedResultSet> executeNamed(const QcSqlDelete & del);
    std::optional<QcNamedResultSet> executeNamed(const std::string & sql, const QcVariantList & params = {});
};
```

## `src/main.cpp`

Demo example (not an automated test) — builds a `SELECT` via
`QcSqlQuery`, prints `nativeDriverInfo()` and the resulting `toSql().sql`. The
same example — in [usage.md's introduction](usage.md).

## `tests/`

GoogleTest test harness — file layout, how to run, coverage map —
[testing.md](testing.md).
