#ifndef QCDBDRIVER_H
#define QCDBDRIVER_H

// Which native database driver a QcConnectionParams/QcSqlDialect call
// targets, chosen at runtime (QcConnectionParams::driver, or an explicit
// argument to QcSqlDialect::.../toSql()) rather than baked in by a
// compile-time macro that used to select it. Not every value is necessarily usable in
// a given build -- QC_DB_DRIVERS (see CMakeLists.txt) selects which of the
// five actually get their native client library linked in
// (QC_DB_HAS_POSTGRESQL/.../QC_DB_HAS_MSSQL); constructing a
// QcNativeConnection for a driver that wasn't compiled in throws.
enum class QcDbDriver { PostgreSQL, Oracle, MySQL, SQLite, MSSQL };

#endif // QCDBDRIVER_H
