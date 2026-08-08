#include <sqlite3.h>

#include <stdexcept>
#include <type_traits>
#include <variant>

#include "qcdriverconnection.h"
#include "qcdriverfactory.h"

namespace {

// "Ping" query for the liveness probe below -- see the identically-named
// constant in qcdriverpostgresql.cpp for why a real round-trip is needed
// rather than a purely local status check. SQLite has no server process to
// lose a connection to, but the same probe still catches e.g. the backing
// file having been deleted out from under an open handle.
constexpr const char * kPingQuery = "SELECT 1";

void bindParam(sqlite3_stmt * stmt, int index, const QcSqlBase::QcVariant & value)
{
    std::visit([stmt, index](const auto & alt) {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            sqlite3_bind_null(stmt, index);
        } else if constexpr (std::is_same_v<T, long long>) {
            sqlite3_bind_int64(stmt, index, alt);
        } else if constexpr (std::is_same_v<T, double>) {
            sqlite3_bind_double(stmt, index, alt);
        } else if constexpr (std::is_same_v<T, std::string>) {
            sqlite3_bind_text(stmt, index, alt.data(), static_cast<int>(alt.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
            const void * data = alt.empty() ? nullptr : static_cast<const void *>(alt.data());
            sqlite3_bind_blob(stmt, index, data, static_cast<int>(alt.size()), SQLITE_TRANSIENT);
        }
    }, value);
}

class QcSqliteConnection : public IQcDriverConnection
{
public:
    explicit QcSqliteConnection(const QcConnectionParams & params)
    {
        // Only `database` is meaningful for SQLite (see qcconnectionparams.h) --
        // host/port/user/password come from the same struct as every other
        // driver but don't apply to a local file. SQLITE_OPEN_CREATE makes
        // "connect" double as "provision a fresh database" for a path that
        // doesn't exist yet, which is normal SQLite usage (there is no separate
        // server-side CREATE DATABASE step); SQLITE_OPEN_URI additionally allows
        // "file::memory:?cache=shared" and "file:...?mode=ro" style URIs for
        // callers that want them, with plain filesystem paths behaving exactly
        // as before.
        const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI;
        const int rc = sqlite3_open_v2(params.database.c_str(), &m_db, flags, nullptr);
        if (rc != SQLITE_OK) {
            const std::string error = m_db ? sqlite3_errmsg(m_db) : sqlite3_errstr(rc);
            if (m_db) {
                sqlite3_close(m_db);
                m_db = nullptr;
            }
            throw std::runtime_error("QcNativeConnection: " + error);
        }

        if (params.connectTimeoutSeconds > 0) {
            // SQLite has no "connect" timeout (opening a local file doesn't
            // block on the network) -- the closest equivalent is how long to
            // wait on another connection's write lock before giving up, which is
            // what busy_timeout controls.
            sqlite3_busy_timeout(m_db, params.connectTimeoutSeconds * 1000);
        }

        // Off by default per-connection in SQLite (for backwards compatibility
        // with pre-3.6.19 databases) -- on unconditionally here so a schema that
        // declares FK constraints actually enforces them, matching every other
        // driver this project supports.
        sqlite3_exec(m_db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
    }

    ~QcSqliteConnection() override
    {
        if (m_db) {
            sqlite3_close_v2(m_db);
        }
    }

    bool isAlive() override
    {
        sqlite3_stmt * stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, kPingQuery, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        const bool ok = sqlite3_step(stmt) == SQLITE_ROW;
        sqlite3_finalize(stmt);
        return ok;
    }

    void * nativeHandle() const override
    {
        return m_db;
    }

    std::optional<QcResultSet> execute(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                        QcSqlBase::QcStringList * outColumnNames) override
    {
        sqlite3_stmt * stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return std::nullopt;
        }

        for (std::size_t i = 0; i < params.size(); ++i) {
            bindParam(stmt, static_cast<int>(i) + 1, params[i]);
        }

        QcResultSet rows;
        const int columnCount = sqlite3_column_count(stmt);
        if (outColumnNames) {
            outColumnNames->reserve(static_cast<std::size_t>(columnCount));
            for (int c = 0; c < columnCount; ++c) {
                const char * name = sqlite3_column_name(stmt, c);
                outColumnNames->emplace_back(name ? name : "");
            }
        }
        int stepStatus;
        while ((stepStatus = sqlite3_step(stmt)) == SQLITE_ROW) {
            QcResultRow row;
            row.reserve(static_cast<std::size_t>(columnCount));
            for (int c = 0; c < columnCount; ++c) {
                switch (sqlite3_column_type(stmt, c)) {
                    case SQLITE_NULL:
                        row.emplace_back(std::monostate{});
                        break;
                    case SQLITE_INTEGER:
                        row.emplace_back(static_cast<long long>(sqlite3_column_int64(stmt, c)));
                        break;
                    case SQLITE_FLOAT:
                        row.emplace_back(sqlite3_column_double(stmt, c));
                        break;
                    case SQLITE_BLOB: {
                        const int size = sqlite3_column_bytes(stmt, c);
                        if (size > 0) {
                            const auto * data = static_cast<const std::byte *>(sqlite3_column_blob(stmt, c));
                            row.emplace_back(std::vector<std::byte>(data, data + size));
                        } else {
                            row.emplace_back(std::vector<std::byte>{});
                        }
                        break;
                    }
                    case SQLITE_TEXT:
                    default: {
                        const auto * text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, c));
                        const int size = sqlite3_column_bytes(stmt, c);
                        row.emplace_back(std::string(text, static_cast<std::size_t>(size)));
                        break;
                    }
                }
            }
            rows.push_back(std::move(row));
        }

        const bool ok = (stepStatus == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (!ok) {
            return std::nullopt;
        }
        return rows;
    }

private:
    sqlite3 * m_db = nullptr;
};

} // namespace

std::unique_ptr<IQcDriverConnection> createSqliteConnection(const QcConnectionParams & params)
{
    return std::make_unique<QcSqliteConnection>(params);
}

std::string sqliteDriverInfo()
{
    return std::string("SQLite (vendored amalgamation) library version ") + sqlite3_libversion();
}
