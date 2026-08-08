#include <mysql.h>

#include <cstring>
#include <type_traits>
#include <variant>

#include "qcdriverconnection.h"
#include "qcdriverfactory.h"

namespace {

// "Ping" query for the liveness probe below -- see the identically-named
// constant in qcdriverpostgresql.cpp for why a real round-trip is needed
// rather than a purely local status check.
constexpr const char * kPingQuery = "SELECT 1";

// Backing storage for one bound parameter -- must outlive mysql_stmt_execute()
// (the client library reads from these addresses at execute time, the same
// contract as OCI's/ODBC's BoundParam in qcdriveroracle.cpp/qcdrivermssql.cpp),
// so callers pre-size a std::vector<BoundParam> once (never push_back
// mid-loop), same reasoning as those backends.
struct BoundParam
{
    long long intValue = 0;
    double doubleValue = 0.0;
    std::string text;
    std::vector<std::byte> bytes;
    unsigned long length = 0;
    bool isNull = false;
};

void bindParam(MYSQL_BIND & bind, const QcSqlBase::QcVariant & value, BoundParam & storage)
{
    static const std::byte emptyByte{};

    std::memset(&bind, 0, sizeof(bind));
    std::visit([&](const auto & alt) {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            storage.isNull = true;
            bind.buffer_type = MYSQL_TYPE_NULL;
            bind.is_null = &storage.isNull;
        } else if constexpr (std::is_same_v<T, long long>) {
            storage.intValue = alt;
            bind.buffer_type = MYSQL_TYPE_LONGLONG;
            bind.buffer = &storage.intValue;
        } else if constexpr (std::is_same_v<T, double>) {
            storage.doubleValue = alt;
            bind.buffer_type = MYSQL_TYPE_DOUBLE;
            bind.buffer = &storage.doubleValue;
        } else if constexpr (std::is_same_v<T, std::string>) {
            storage.text = alt;
            storage.length = static_cast<unsigned long>(storage.text.size());
            bind.buffer_type = MYSQL_TYPE_STRING;
            bind.buffer = storage.text.empty() ? const_cast<char *>("") : storage.text.data();
            bind.buffer_length = storage.length;
            bind.length = &storage.length;
        } else { // std::vector<std::byte>
            storage.bytes = alt;
            storage.length = static_cast<unsigned long>(storage.bytes.size());
            bind.buffer_type = MYSQL_TYPE_BLOB;
            bind.buffer = storage.bytes.empty() ? const_cast<std::byte *>(&emptyByte) : storage.bytes.data();
            bind.buffer_length = storage.length;
            bind.length = &storage.length;
        }
    }, value);
}

// Column value categories -- decides both the C type mysql_stmt_bind_result()
// below fetches with and how the raw bytes it returns become a QcVariant.
// Mirrors ColumnCategory in qcdrivermssql.cpp.
enum class ColumnCategory { Integer, Floating, Binary, Text };

ColumnCategory categorize(const MYSQL_FIELD & field)
{
    switch (field.type) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_YEAR:
            return ColumnCategory::Integer;
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
            return ColumnCategory::Floating;
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_STRING:
            // TEXT/BLOB (and VARCHAR/VARBINARY, CHAR/BINARY) share the same
            // wire type per size class -- charsetnr 63 (the "binary"
            // collation) is what actually distinguishes a binary column from
            // a text one on the wire (verified directly against this
            // project's own BLOB/TEXT test columns).
            return (field.charsetnr == 63) ? ColumnCategory::Binary : ColumnCategory::Text;
        default:
            // JSON, DATE/TIME/DATETIME/TIMESTAMP, BIT, ... -- fetched as
            // their default text representation, same fallback every other
            // driver's categorize()-equivalent uses.
            return ColumnCategory::Text;
    }
}

// Backing storage for one fetched column -- see ColumnCategory above for how
// each alternative gets populated. `data` is sized exactly to that column's
// real max_length across the whole (already-buffered, see fetchRows())
// result set, not a guessed fixed cap, so this project's own multi-megabyte
// Huge text/binary values round-trip without MYSQL_DATA_TRUNCATED.
struct ColumnBuffer
{
    long long intValue = 0;
    double doubleValue = 0.0;
    std::vector<char> data;
    unsigned long length = 0;
    bool isNull = false;
};

std::optional<QcResultSet> fetchRows(MYSQL_STMT * stmt, unsigned int fieldCount, QcSqlBase::QcStringList * outColumnNames = nullptr)
{
    // Buffers the whole result set client-side and (via
    // STMT_ATTR_UPDATE_MAX_LENGTH, set before mysql_stmt_execute() below)
    // computes each column's real max_length across every row -- the
    // standard, documented way to size TEXT/BLOB fetch buffers exactly
    // instead of guessing a fixed cap.
    if (mysql_stmt_store_result(stmt) != 0) {
        return std::nullopt;
    }

    MYSQL_RES * metadata = mysql_stmt_result_metadata(stmt);
    if (!metadata) {
        return std::nullopt;
    }
    MYSQL_FIELD * fields = mysql_fetch_fields(metadata);

    std::vector<ColumnCategory> categories(fieldCount);
    std::vector<ColumnBuffer> columns(fieldCount);
    std::vector<MYSQL_BIND> binds(fieldCount);
    if (outColumnNames) {
        outColumnNames->reserve(fieldCount);
    }
    for (unsigned int c = 0; c < fieldCount; ++c) {
        categories[c] = categorize(fields[c]);
        if (outColumnNames) {
            outColumnNames->emplace_back(fields[c].name, fields[c].name_length);
        }
        MYSQL_BIND & bind = binds[c];
        std::memset(&bind, 0, sizeof(bind));
        bind.is_null = &columns[c].isNull;
        bind.length = &columns[c].length;
        switch (categories[c]) {
            case ColumnCategory::Integer:
                bind.buffer_type = MYSQL_TYPE_LONGLONG;
                bind.buffer = &columns[c].intValue;
                break;
            case ColumnCategory::Floating:
                bind.buffer_type = MYSQL_TYPE_DOUBLE;
                bind.buffer = &columns[c].doubleValue;
                break;
            case ColumnCategory::Binary:
            case ColumnCategory::Text:
                columns[c].data.resize(std::max<unsigned long>(fields[c].max_length, 1));
                bind.buffer_type = MYSQL_TYPE_STRING;
                bind.buffer = columns[c].data.data();
                bind.buffer_length = static_cast<unsigned long>(columns[c].data.size());
                break;
        }
    }
    mysql_free_result(metadata);

    if (mysql_stmt_bind_result(stmt, binds.data())) {
        return std::nullopt;
    }

    QcResultSet rows;
    int fetchStatus;
    while ((fetchStatus = mysql_stmt_fetch(stmt)) == 0) {
        QcResultRow row;
        row.reserve(fieldCount);
        for (unsigned int c = 0; c < fieldCount; ++c) {
            if (columns[c].isNull) {
                row.emplace_back(std::monostate{});
                continue;
            }
            switch (categories[c]) {
                case ColumnCategory::Integer:
                    row.emplace_back(columns[c].intValue);
                    break;
                case ColumnCategory::Floating:
                    row.emplace_back(columns[c].doubleValue);
                    break;
                case ColumnCategory::Binary:
                    row.emplace_back(std::vector<std::byte>(
                        reinterpret_cast<const std::byte *>(columns[c].data.data()),
                        reinterpret_cast<const std::byte *>(columns[c].data.data()) + columns[c].length));
                    break;
                case ColumnCategory::Text:
                    row.emplace_back(std::string(columns[c].data.data(), columns[c].length));
                    break;
            }
        }
        rows.push_back(std::move(row));
    }

    if (fetchStatus != MYSQL_NO_DATA) {
        return std::nullopt;
    }
    return rows;
}

class QcMySqlConnection : public IQcDriverConnection
{
public:
    explicit QcMySqlConnection(const QcConnectionParams & params)
    {
        m_conn = mysql_init(nullptr);
        if (!m_conn) {
            throw std::runtime_error("QcNativeConnection: mysql_init failed");
        }

        if (params.connectTimeoutSeconds > 0) {
            const unsigned int timeout = static_cast<unsigned int>(params.connectTimeoutSeconds);
            mysql_options(m_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        }

        // utf8mb4 explicitly -- the connection's own charset defaults to latin1
        // regardless of the schema/database's charset (this project's test
        // database is created with CHARACTER SET utf8mb4), which would silently
        // mangle multi-byte text (Japanese/emoji test data, same Unicode
        // round-trip this project's other drivers are tested against) on both
        // bind and fetch even though server-side storage is UTF-8-capable --
        // the same class of fix as OCIEnvNlsCreate's explicit AL32UTF8 in the
        // Oracle backend.
        mysql_options(m_conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        unsigned int port = 0;
        if (!params.port.empty()) {
            port = static_cast<unsigned int>(std::stoul(params.port));
        }

        if (!mysql_real_connect(m_conn, params.host.c_str(), params.user.c_str(), params.password.c_str(),
                                 params.database.c_str(), port, nullptr, 0)) {
            const std::string error = mysql_error(m_conn);
            mysql_close(m_conn);
            m_conn = nullptr;
            throw std::runtime_error("QcNativeConnection: " + error);
        }
    }

    ~QcMySqlConnection() override
    {
        if (m_conn) {
            mysql_close(m_conn);
        }
    }

    bool isAlive() override
    {
        if (mysql_query(m_conn, kPingQuery) != 0) {
            return false;
        }
        MYSQL_RES * res = mysql_store_result(m_conn);
        const bool ok = res != nullptr && mysql_num_rows(res) == 1;
        if (res) {
            mysql_free_result(res);
        }
        return ok;
    }

    void * nativeHandle() const override
    {
        return m_conn;
    }

    std::optional<QcResultSet> execute(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                        QcSqlBase::QcStringList * outColumnNames) override
    {
        // "BEGIN" (and "START TRANSACTION") cannot be prepared at all --
        // mysql_stmt_prepare() rejects both with "This command is not supported
        // in the prepared statement protocol yet" (verified directly against
        // this server; COMMIT/ROLLBACK, unlike BEGIN, prepare and execute fine
        // and fall through to the normal path below unmodified) -- runs through
        // the older text/simple-query protocol instead, the same fallback the
        // PostgreSQL backend uses for its own different reason (no params
        // to bind). Takes no parameters, so there is nothing this project's
        // `params` contract needs to express for it.
        if (sql == "BEGIN") {
            return (mysql_query(m_conn, sql.c_str()) == 0) ? std::optional<QcResultSet>(QcResultSet{}) : std::nullopt;
        }

        MYSQL_STMT * stmt = mysql_stmt_init(m_conn);
        if (!stmt) {
            return std::nullopt;
        }
        if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        // Declared in the same scope as mysql_stmt_execute() below, not nested
        // inside the `if (!params.empty())` block -- mysql_stmt_bind_param()
        // only records these buffer pointers, it doesn't read the values until
        // mysql_stmt_execute() actually runs, so boundParams/binds must still be
        // alive at that point (a narrower, block-scoped declaration here would
        // free them right after bind_param(), leaving execute() to read freed
        // memory). Pre-sized, never push_back'd -- see BoundParam's doc comment
        // for why.
        std::vector<BoundParam> boundParams(params.size());
        std::vector<MYSQL_BIND> binds(params.size());
        if (!params.empty()) {
            for (std::size_t i = 0; i < params.size(); ++i) {
                bindParam(binds[i], params[i], boundParams[i]);
            }
            if (mysql_stmt_bind_param(stmt, binds.data())) {
                mysql_stmt_close(stmt);
                return std::nullopt;
            }
        }

        // Compute field.max_length for every result column when the result set
        // gets buffered in fetchRows() below -- off by default
        // (mysql_stmt_store_result skips computing it unless asked, for speed),
        // needed to size TEXT/BLOB fetch buffers exactly rather than guessing.
        bool updateMaxLength = true;
        mysql_stmt_attr_set(stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &updateMaxLength);

        if (mysql_stmt_execute(stmt) != 0) {
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        std::optional<QcResultSet> result;
        const unsigned int fieldCount = mysql_stmt_field_count(stmt);
        if (fieldCount > 0) {
            result = fetchRows(stmt, fieldCount, outColumnNames);
        } else {
            // INSERT/UPDATE/DELETE/DDL -- no result set, matching every other
            // driver's "empty set for statements that don't produce rows".
            result = QcResultSet{};
        }

        mysql_stmt_close(stmt);
        return result;
    }

private:
    MYSQL * m_conn = nullptr;
};

} // namespace

std::unique_ptr<IQcDriverConnection> createMySqlConnection(const QcConnectionParams & params)
{
    return std::make_unique<QcMySqlConnection>(params);
}

std::string mySqlDriverInfo()
{
    return std::string("MySQL (libmysqlclient) client version ") + mysql_get_client_info();
}
