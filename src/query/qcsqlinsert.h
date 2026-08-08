#ifndef QCSQLINSERT_H
#define QCSQLINSERT_H

#include "qcdbdriver.h"
#include "qcsqlbase.h"
#include <string>
#include <utility>
#include <vector>

// Single-row INSERT builder: `INSERT INTO table (col1, col2) VALUES (?, ?)`.
class QcSqlInsertWhiteBoxTest;
class QcSqlInsert : public QcSqlBase
{
public:
    QcSqlInsert();

    // Sets the dialect toSql() renders for once, up front -- see
    // QcSqlQuery::useDriver()'s doc comment for the full rationale.
    QcSqlInsert & useDriver(QcDbDriver driver);

    QcSqlInsert & into(const std::string & table);

    // Sets column = value for the row being built. A later set() for a
    // column already set overwrites its value in place (keeping its
    // original position in the column list) instead of appending a
    // duplicate column -- matches "keep re-assigning the same builder"
    // rather than silently emitting "col, col" twice in the generated SQL.
    QcSqlInsert & set(const std::string & column, const QcVariant & value);

    // Requests the listed columns back from the inserted row, appended to
    // toSql()'s output as `RETURNING col1, col2` (PostgreSQL/SQLite),
    // `OUTPUT inserted.col1, inserted.col2` (MSSQL), or
    // `RETURNING col1, col2 INTO :N, :N+1` (Oracle, read back via
    // QcNativeConnection::executeReturning()) -- see
    // QcSqlDialect::returningClause() for the full per-driver rundown.
    // MySQL has no equivalent at all with this overload (silently no-ops,
    // same as Oracle would without executeReturning()) -- use the
    // autoIncrementColumn overload below to get an emulated equivalent for
    // INSERT specifically.
    QcSqlInsert & returning(const QcStringList & columns);

    // MySQL-only equivalent: MySQL has no INSERT ... RETURNING syntax in any
    // form, so this doesn't change toSql()'s own output at all -- instead it
    // tells QueryCreator::execute(const QcSqlInsert&) to run a follow-up
    // `SELECT <columns> FROM table WHERE <autoIncrementColumn> = ?` keyed on
    // MySQL's LAST_INSERT_ID(), in the same transaction as the insert (see
    // QcSqlStatement::mysqlReturningSelectSql). `autoIncrementColumn` must
    // name this table's AUTO_INCREMENT primary key column -- there is no way
    // to discover it automatically from here. Not atomic in the same sense
    // real RETURNING is: the values come from a second statement reading the
    // row back, not from the INSERT itself, so a concurrent DELETE/UPDATE of
    // that exact row between the two statements outside this transaction
    // could interleave -- though never miss the row entirely, since a
    // transaction always sees its own uncommitted writes. On every other
    // driver this overload behaves exactly like the one above --
    // `autoIncrementColumn` is simply ignored.
    QcSqlInsert & returning(const QcStringList & columns, const std::string & autoIncrementColumn);

    /*SQL generation*/

    // Top-level entry point: renders this statement as a full, standalone
    // command with its own fresh parameter list, using the dialect
    // useDriver() configured. The explicit-driver overload renders for
    // `driver` instead -- see QcSqlQuery::toSql()'s doc comment for the
    // full rationale (recursion/multi-driver-rendering escape hatch).
    QcSqlStatement toSql() const;
    QcSqlStatement toSql(QcDbDriver driver) const;

    // Renders as SQL text, appending its bind values onto `params` (mirrors
    // QcSqlQuery::toSql(QcVariantList&) -- see qcsqlquery.h for why this
    // threading matters once callers embed statements in a larger shared
    // parameter list).
    std::string toSql(QcVariantList & params) const;
    std::string toSql(QcVariantList & params, QcDbDriver driver) const;

private:
    QcDbDriver m_driver = QcDbDriver::PostgreSQL;
    std::string m_table;
    std::vector<std::pair<std::string, QcVariant>> m_columns;
    QcStringList m_returning;
    std::string m_autoIncrementColumn; // MySQL-only, see the autoIncrementColumn returning() overload

    friend class QcSqlInsertWhiteBoxTest;
};

#endif // QCSQLINSERT_H
