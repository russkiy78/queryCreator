#ifdef _WIN32
// The Windows SDK's sql.h/sqltypes.h use SAL annotations (_Out_, _In_reads_,
// ...) and GUID, both only defined once windows.h has been included first;
// unixODBC's headers on Linux don't have that ordering requirement.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>
#include <sqlucode.h> // SQL_WVARCHAR/SQL_WLONGVARCHAR/SQL_C_WCHAR -- the wide-character type codes, not in sql.h/sqlext.h on unixODBC

#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <variant>

#include "qcdriverconnection.h"
#include "qcdriverfactory.h"

namespace {

// SQLWCHAR is expected to be a 2-byte UTF-16 code unit here (true for
// Microsoft's driver on Windows, and for unixODBC 2.3.1+ built with
// -DWITH_UNIXODBC_CONF / the standard Ubuntu unixodbc-dev package this
// project builds against on Linux) -- the UTF-16 conversion helpers below
// reinterpret buffers of it as char16_t, which would silently corrupt
// non-ASCII text if some ODBC implementation ever defined it as 4 bytes
// (as historic iODBC did).
static_assert(sizeof(SQLWCHAR) == 2, "QcDrivermssql's text handling assumes UTF-16 SQLWCHAR");

// One physical ODBC connection needs both its environment and connection
// handles kept alive together -- bundled into one struct, same reasoning as
// OracleConnection in qcdriveroracle.cpp.
struct MssqlConnection
{
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
};

bool odbcOk(SQLRETURN rc)
{
    return rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;
}

std::string odbcErrorText(SQLSMALLINT handleType, SQLHANDLE handle)
{
    std::string message;
    SQLSMALLINT record = 1;
    SQLCHAR sqlState[6];
    SQLINTEGER nativeError = 0;
    SQLCHAR text[1024];
    SQLSMALLINT textLen = 0;
    while (SQLGetDiagRec(handleType, handle, record, sqlState, &nativeError, text, sizeof(text), &textLen) == SQL_SUCCESS) {
        if (!message.empty()) {
            message += " | ";
        }
        message += "[" + std::string(reinterpret_cast<char *>(sqlState)) + "] "
            + std::string(reinterpret_cast<char *>(text), static_cast<std::size_t>(textLen));
        ++record;
    }
    return message.empty() ? "unknown ODBC error" : message;
}

// "Ping" query -- see the identically-named constant in
// qcdriverpostgresql.cpp for why a real round-trip is needed rather than a
// purely local status check. Every driver here (MSSQL included) accepts a
// bare "SELECT 1".
constexpr const char * kPingQuery = "SELECT 1";

// Encodes `utf8` (this project's QcVariant string representation) as UTF-16
// code units for SQLBindParameter's SQL_C_WCHAR buffers -- MSSQL's narrow
// SQLCHAR path is codepage-dependent (not guaranteed UTF-8, unlike
// PostgreSQL/MySQL/SQLite's text protocols), so round-tripping arbitrary
// Unicode (this project's own test data includes Japanese text and
// non-BMP emoji) needs the wide API on both Windows and Linux, not just a
// locale-dependent narrow one. Invalid UTF-8 bytes map to U+FFFD rather than
// failing outright -- callers only ever pass well-formed UTF-8 (this
// project's own QcVariant strings), so this is purely a defensive fallback.
std::vector<SQLWCHAR> utf8ToUtf16(const std::string & utf8)
{
    std::vector<SQLWCHAR> out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c0 = static_cast<unsigned char>(utf8[i]);
        char32_t codepoint;
        std::size_t len;
        if (c0 < 0x80) {
            codepoint = c0;
            len = 1;
        } else if ((c0 & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            codepoint = (static_cast<char32_t>(c0 & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            len = 2;
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            codepoint = (static_cast<char32_t>(c0 & 0x0F) << 12)
                | (static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6)
                | (static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
            len = 3;
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            codepoint = (static_cast<char32_t>(c0 & 0x07) << 18)
                | (static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 12)
                | (static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 2]) & 0x3F) << 6)
                | (static_cast<unsigned char>(utf8[i + 3]) & 0x3F);
            len = 4;
        } else {
            codepoint = 0xFFFD;
            len = 1;
        }
        i += len;
        if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<SQLWCHAR>(codepoint));
        } else {
            codepoint -= 0x10000;
            out.push_back(static_cast<SQLWCHAR>(0xD800 + (codepoint >> 10)));
            out.push_back(static_cast<SQLWCHAR>(0xDC00 + (codepoint & 0x3FF)));
        }
    }
    return out;
}

// Inverse of utf8ToUtf16() above -- decodes `count` UTF-16 code units
// (surrogate pairs included) fetched via SQL_C_WCHAR back into this
// project's UTF-8 QcVariant string representation.
std::string utf16ToUtf8(const char16_t * data, std::size_t count)
{
    std::string out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        char32_t codepoint = data[i];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < count) {
            const char32_t low = data[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }
    return out;
}

MssqlConnection * openConnection(const QcConnectionParams & params)
{
    auto * conn = new MssqlConnection();

    if (!odbcOk(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &conn->henv))) {
        delete conn;
        throw std::runtime_error("QcNativeConnection: failed to allocate ODBC environment handle");
    }
    SQLSetEnvAttr(conn->henv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);

    if (!odbcOk(SQLAllocHandle(SQL_HANDLE_DBC, conn->henv, &conn->hdbc))) {
        SQLFreeHandle(SQL_HANDLE_ENV, conn->henv);
        delete conn;
        throw std::runtime_error("QcNativeConnection: failed to allocate ODBC connection handle");
    }

    if (params.connectTimeoutSeconds > 0) {
        SQLSetConnectAttr(conn->hdbc, SQL_ATTR_LOGIN_TIMEOUT,
                           reinterpret_cast<SQLPOINTER>(static_cast<std::intptr_t>(params.connectTimeoutSeconds)), 0);
    }

    std::string server = params.host;
    if (!params.port.empty()) {
        server += "," + params.port; // ODBC Driver 18's SERVER keyword takes "host,port", not "host:port"
    }
    // TrustServerCertificate=yes: ODBC Driver 18 defaults Encrypt=yes (unlike
    // 17), which without this rejects the self-signed certificate SQL
    // Server generates on first startup unless one was provisioned
    // separately -- acceptable for the local/test instances this connects
    // to, not a hardened production default.
    const std::string connStr = "DRIVER={ODBC Driver 18 for SQL Server};SERVER=" + server
        + ";DATABASE=" + params.database
        + ";UID=" + params.user
        + ";PWD=" + params.password
        + ";Encrypt=yes;TrustServerCertificate=yes;";

    const SQLRETURN rc = SQLDriverConnect(conn->hdbc, nullptr,
        reinterpret_cast<SQLCHAR *>(const_cast<char *>(connStr.c_str())), SQL_NTS,
        nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
    if (!odbcOk(rc)) {
        const std::string error = odbcErrorText(SQL_HANDLE_DBC, conn->hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, conn->hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, conn->henv);
        delete conn;
        throw std::runtime_error("QcNativeConnection: " + error);
    }

    return conn;
}

void closeConnection(MssqlConnection * conn)
{
    if (conn->hdbc != SQL_NULL_HDBC) {
        SQLDisconnect(conn->hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, conn->hdbc);
    }
    if (conn->henv != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, conn->henv);
    }
    delete conn;
}

bool checkAlive(MssqlConnection * conn)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    if (!odbcOk(SQLAllocHandle(SQL_HANDLE_STMT, conn->hdbc, &hstmt))) {
        return false;
    }
    const SQLRETURN rc = SQLExecDirect(hstmt, reinterpret_cast<SQLCHAR *>(const_cast<char *>(kPingQuery)), SQL_NTS);
    const bool ok = odbcOk(rc) && odbcOk(SQLFetch(hstmt));
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return ok;
}

// Backing storage for one bound parameter -- must outlive SQLExecute() (ODBC
// reads from these addresses at execute time, same contract as OCI's
// BoundParam in qcdriveroracle.cpp), so callers pre-size a
// std::vector<BoundParam> once (never push_back mid-loop) exactly like the
// Oracle backend does.
struct BoundParam
{
    std::vector<SQLWCHAR> wtext; // backing storage for string binds (UTF-16)
    std::vector<std::byte> bytes; // backing storage for blob binds
    long long intValue = 0;
    double doubleValue = 0.0;
    SQLLEN indicator = 0; // byte length, or SQL_NULL_DATA
};

void bindParam(SQLHSTMT hstmt, SQLUSMALLINT position, const QcSqlBase::QcVariant & value, BoundParam & storage)
{
    static const SQLWCHAR emptyWtext[1] = {0};
    static const std::byte emptyBytes[1] = {std::byte{0}};

    std::visit([&](const auto & alt) {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            storage.indicator = SQL_NULL_DATA;
            SQLBindParameter(hstmt, position, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 1, 0,
                              const_cast<SQLWCHAR *>(emptyWtext), 0, &storage.indicator);
        } else if constexpr (std::is_same_v<T, long long>) {
            storage.intValue = alt;
            storage.indicator = sizeof(storage.intValue);
            SQLBindParameter(hstmt, position, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
                              &storage.intValue, 0, &storage.indicator);
        } else if constexpr (std::is_same_v<T, double>) {
            storage.doubleValue = alt;
            storage.indicator = sizeof(storage.doubleValue);
            SQLBindParameter(hstmt, position, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0,
                              &storage.doubleValue, 0, &storage.indicator);
        } else if constexpr (std::is_same_v<T, std::string>) {
            storage.wtext = utf8ToUtf16(alt);
            const SQLLEN byteLen = static_cast<SQLLEN>(storage.wtext.size() * sizeof(SQLWCHAR));
            storage.indicator = byteLen;
            const SQLULEN columnSize = storage.wtext.empty() ? 1 : storage.wtext.size();
            // SQL_WVARCHAR up to the classic NVARCHAR(4000) ceiling, only
            // escalating to the "large object" SQL_WLONGVARCHAR beyond it --
            // verified directly that binding *every* string parameter as
            // SQL_WLONGVARCHAR (regardless of length) makes SQL Server
            // reject the statement with "Operand type clash: ntext is
            // incompatible with date" the moment a short string binds
            // against a non-string column (e.g. a "YYYY-MM-DD" value going
            // into a DATE column): the wide/LOB parameter classes can't
            // implicitly convert to non-string target types, unlike
            // ordinary (N)VARCHAR. Binary has no equivalent split -- every
            // VARBINARY target column in this project's schemas is already
            // VARBINARY(MAX), so SQL_LONGVARBINARY never hits this.
            constexpr std::size_t kShortStringLimit = 4000;
            const SQLSMALLINT paramType = (storage.wtext.size() <= kShortStringLimit) ? SQL_WVARCHAR : SQL_WLONGVARCHAR;
            SQLBindParameter(hstmt, position, SQL_PARAM_INPUT, SQL_C_WCHAR, paramType, columnSize, 0,
                              storage.wtext.empty() ? const_cast<SQLWCHAR *>(emptyWtext) : storage.wtext.data(),
                              byteLen, &storage.indicator);
        } else { // std::vector<std::byte>
            storage.bytes = alt;
            const SQLLEN byteLen = static_cast<SQLLEN>(storage.bytes.size());
            storage.indicator = byteLen;
            const SQLULEN columnSize = storage.bytes.empty() ? 1 : storage.bytes.size();
            SQLBindParameter(hstmt, position, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, columnSize, 0,
                              storage.bytes.empty() ? const_cast<std::byte *>(emptyBytes) : storage.bytes.data(),
                              byteLen, &storage.indicator);
        }
    }, value);
}

// Column value categories -- decides both the C type SQLGetData() below
// fetches with and how the raw bytes it returns become a QcVariant.
enum class ColumnCategory { Integer, Floating, Binary, Text };

ColumnCategory categorize(SQLSMALLINT sqlType)
{
    switch (sqlType) {
        case SQL_TINYINT:
        case SQL_SMALLINT:
        case SQL_INTEGER:
        case SQL_BIGINT:
        case SQL_BIT:
            return ColumnCategory::Integer;
        case SQL_REAL:
        case SQL_FLOAT:
        case SQL_DOUBLE:
        case SQL_DECIMAL:
        case SQL_NUMERIC:
            return ColumnCategory::Floating;
        case SQL_BINARY:
        case SQL_VARBINARY:
        case SQL_LONGVARBINARY:
            return ColumnCategory::Binary;
        default:
            // CHAR/VARCHAR/WCHAR/WVARCHAR/(W)LONGVARCHAR, DATE/TIME/
            // TIMESTAMP/DATETIME2, GUID, ... -- fetched as their default
            // driver-formatted text representation.
            return ColumnCategory::Text;
    }
}

// Fetches column `col` of the current row as UTF-8 text, or nullopt if it's
// NULL -- SQLGetData() is called in a loop rather than relying on a single
// fixed-size buffer because MSSQL's (N)VARCHAR(MAX)/(N)TEXT columns have no
// fixed upper bound and the driver itself doesn't necessarily know the
// remaining length up front (indicated by SQL_NO_TOTAL) -- rc distinguishes
// "more data after this chunk" (SQL_SUCCESS_WITH_INFO) from "this was the
// last chunk" (SQL_SUCCESS), the documented, portable way to drain an
// unbound column of unknown length -- unlike Oracle's LOB locators, there's
// no separate deferred-read handle to manage, just repeated SQLGetData
// calls on the same column.
std::optional<std::string> fetchWideText(SQLHSTMT hstmt, SQLUSMALLINT col)
{
    constexpr SQLLEN chunkChars = 16384;
    std::vector<SQLWCHAR> buffer(static_cast<std::size_t>(chunkChars));
    std::u16string accumulated;

    while (true) {
        SQLLEN indicator = 0;
        const SQLRETURN rc = SQLGetData(hstmt, col, SQL_C_WCHAR, buffer.data(),
                                         chunkChars * static_cast<SQLLEN>(sizeof(SQLWCHAR)), &indicator);
        if (rc == SQL_NO_DATA) {
            break;
        }
        if (indicator == SQL_NULL_DATA) {
            return std::nullopt;
        }
        if (!odbcOk(rc)) {
            break;
        }

        SQLLEN bytesThisCall;
        if (rc == SQL_SUCCESS_WITH_INFO) {
            // Truncated -- the driver filled the buffer up to capacity minus
            // the null terminator it always appends; more remains.
            bytesThisCall = (chunkChars - 1) * static_cast<SQLLEN>(sizeof(SQLWCHAR));
        } else {
            // Final chunk -- indicator is the exact byte length written,
            // except when it's SQL_NO_TOTAL (the driver never learned the
            // total length either, but rc==SQL_SUCCESS still means this
            // fetch itself completed without truncation).
            bytesThisCall = (indicator >= 0 && indicator <= chunkChars * static_cast<SQLLEN>(sizeof(SQLWCHAR)))
                ? indicator : (chunkChars - 1) * static_cast<SQLLEN>(sizeof(SQLWCHAR));
        }
        accumulated.append(reinterpret_cast<const char16_t *>(buffer.data()),
                            static_cast<std::size_t>(bytesThisCall) / sizeof(SQLWCHAR));
        if (rc == SQL_SUCCESS) {
            break;
        }
    }
    return utf16ToUtf8(accumulated.data(), accumulated.size());
}

// Binary counterpart of fetchWideText() above -- same chunked-SQLGetData
// strategy, but indicator/lengths are already in bytes (no UTF-16 code-unit
// arithmetic needed).
std::optional<std::vector<std::byte>> fetchBinary(SQLHSTMT hstmt, SQLUSMALLINT col)
{
    constexpr SQLLEN chunkBytes = 65536;
    std::vector<std::byte> buffer(static_cast<std::size_t>(chunkBytes));
    std::vector<std::byte> accumulated;

    while (true) {
        SQLLEN indicator = 0;
        const SQLRETURN rc = SQLGetData(hstmt, col, SQL_C_BINARY, buffer.data(), chunkBytes, &indicator);
        if (rc == SQL_NO_DATA) {
            break;
        }
        if (indicator == SQL_NULL_DATA) {
            return std::nullopt;
        }
        if (!odbcOk(rc)) {
            break;
        }

        const SQLLEN bytesThisCall = (rc == SQL_SUCCESS_WITH_INFO)
            ? chunkBytes
            : ((indicator >= 0 && indicator <= chunkBytes) ? indicator : chunkBytes);
        accumulated.insert(accumulated.end(), buffer.begin(), buffer.begin() + bytesThisCall);
        if (rc == SQL_SUCCESS) {
            break;
        }
    }
    return accumulated;
}

std::optional<QcResultSet> fetchRows(SQLHSTMT hstmt, SQLSMALLINT colCount, QcSqlBase::QcStringList * outColumnNames = nullptr)
{
    std::vector<ColumnCategory> categories(static_cast<std::size_t>(colCount));
    for (SQLSMALLINT c = 0; c < colCount; ++c) {
        SQLCHAR name[256];
        SQLSMALLINT nameLen = 0;
        SQLSMALLINT dataType = 0;
        SQLULEN columnSize = 0;
        SQLSMALLINT decimalDigits = 0;
        SQLSMALLINT nullable = 0;
        SQLDescribeCol(hstmt, static_cast<SQLUSMALLINT>(c + 1), name, sizeof(name), &nameLen,
                        &dataType, &columnSize, &decimalDigits, &nullable);
        categories[static_cast<std::size_t>(c)] = categorize(dataType);
        if (outColumnNames) {
            outColumnNames->emplace_back(reinterpret_cast<char *>(name), static_cast<std::size_t>(nameLen));
        }
    }

    QcResultSet rows;
    while (true) {
        const SQLRETURN rc = SQLFetch(hstmt);
        if (rc == SQL_NO_DATA) {
            break;
        }
        if (!odbcOk(rc)) {
            return std::nullopt;
        }

        QcResultRow row;
        row.reserve(static_cast<std::size_t>(colCount));
        for (SQLSMALLINT c = 0; c < colCount; ++c) {
            const SQLUSMALLINT col = static_cast<SQLUSMALLINT>(c + 1);
            switch (categories[static_cast<std::size_t>(c)]) {
                case ColumnCategory::Integer: {
                    SQLBIGINT value = 0;
                    SQLLEN indicator = 0;
                    SQLGetData(hstmt, col, SQL_C_SBIGINT, &value, 0, &indicator);
                    if (indicator == SQL_NULL_DATA) {
                        row.emplace_back(std::monostate{});
                    } else {
                        row.emplace_back(static_cast<long long>(value));
                    }
                    break;
                }
                case ColumnCategory::Floating: {
                    double value = 0.0;
                    SQLLEN indicator = 0;
                    SQLGetData(hstmt, col, SQL_C_DOUBLE, &value, 0, &indicator);
                    if (indicator == SQL_NULL_DATA) {
                        row.emplace_back(std::monostate{});
                    } else {
                        row.emplace_back(value);
                    }
                    break;
                }
                case ColumnCategory::Binary: {
                    if (auto bytes = fetchBinary(hstmt, col)) {
                        row.emplace_back(std::move(*bytes));
                    } else {
                        row.emplace_back(std::monostate{});
                    }
                    break;
                }
                case ColumnCategory::Text: {
                    if (auto text = fetchWideText(hstmt, col)) {
                        row.emplace_back(std::move(*text));
                    } else {
                        row.emplace_back(std::monostate{});
                    }
                    break;
                }
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

class QcMssqlConnection : public IQcDriverConnection
{
public:
    explicit QcMssqlConnection(const QcConnectionParams & params)
        : m_conn(openConnection(params))
    {
    }

    ~QcMssqlConnection() override
    {
        closeConnection(m_conn);
    }

    bool isAlive() override
    {
        return checkAlive(m_conn);
    }

    void * nativeHandle() const override
    {
        return m_conn;
    }

    std::optional<QcResultSet> execute(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                        QcSqlBase::QcStringList * outColumnNames) override
    {
        // T-SQL has no bare "BEGIN" transaction-start statement -- unqualified
        // BEGIN opens a BEGIN...END block (a control-of-flow construct expecting
        // a matching END), and fails on its own (verified directly: "Msg 102,
        // Incorrect syntax near the end of the batch"). Explicit
        // "BEGIN TRANSACTION" is what actually starts one; unlike Oracle, MSSQL
        // still defaults to per-statement autocommit otherwise (verified
        // directly: Transact-SQL transaction statements interact correctly with
        // ODBC's default autocommit-on mode, temporarily suspending it for the
        // outstanding explicit transaction), so no inExplicitTransaction flag or
        // commit-mode juggling is needed here at all, unlike Oracle. Bare
        // "COMMIT"/"ROLLBACK" (no "TRANSACTION"/"TRAN" keyword) are already
        // valid T-SQL on their own, so they fall straight through to the
        // normal path below unmodified.
        const std::string effectiveSql = (sql == "BEGIN") ? "BEGIN TRANSACTION" : sql;

        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        if (!odbcOk(SQLAllocHandle(SQL_HANDLE_STMT, m_conn->hdbc, &hstmt))) {
            return std::nullopt;
        }

        SQLRETURN rc;
        if (params.empty()) {
            rc = SQLExecDirect(hstmt, reinterpret_cast<SQLCHAR *>(const_cast<char *>(effectiveSql.c_str())), SQL_NTS);
        } else {
            rc = SQLPrepare(hstmt, reinterpret_cast<SQLCHAR *>(const_cast<char *>(effectiveSql.c_str())), SQL_NTS);
            if (odbcOk(rc)) {
                // Pre-sized, never push_back'd -- see BoundParam's doc comment
                // for why (ODBC reads these addresses at execute time).
                std::vector<BoundParam> boundParams(params.size());
                for (std::size_t i = 0; i < params.size(); ++i) {
                    bindParam(hstmt, static_cast<SQLUSMALLINT>(i + 1), params[i], boundParams[i]);
                }
                rc = SQLExecute(hstmt);
            }
        }

        // SQL_NO_DATA from SQLExecute()/SQLExecDirect() is not a failure here --
        // it's how a searched UPDATE/DELETE that matched zero rows reports
        // success (verified directly), the same "zero rows affected isn't an
        // error" contract every other driver already gives this project.
        if (!odbcOk(rc) && rc != SQL_NO_DATA) {
            SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
            return std::nullopt;
        }

        std::optional<QcResultSet> result = QcResultSet{};
        if (rc != SQL_NO_DATA) {
            SQLSMALLINT colCount = 0;
            SQLNumResultCols(hstmt, &colCount);
            // colCount > 0 covers both plain SELECTs and INSERT/UPDATE/DELETE
            // with an OUTPUT clause (QcSqlDialect::returningClause()'s MSSQL
            // form) -- both produce a result set to fetch, unlike a plain DML
            // statement with neither.
            if (colCount > 0) {
                result = fetchRows(hstmt, colCount, outColumnNames);
            }
        }

        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return result;
    }

private:
    MssqlConnection * m_conn;
};

} // namespace

std::unique_ptr<IQcDriverConnection> createMssqlConnection(const QcConnectionParams & params)
{
    return std::make_unique<QcMssqlConnection>(params);
}

std::string mssqlDriverInfo()
{
    SQLHENV henv = SQL_NULL_HENV;
    const SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    const bool ok = (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
    if (ok) {
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
    return std::string("Microsoft SQL Server (ODBC) driver manager ") + (ok ? "reachable" : "unavailable");
}
