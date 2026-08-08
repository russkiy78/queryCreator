#include <libpq-fe.h>

#include <type_traits>
#include <variant>

#include "qcdriverconnection.h"
#include "qcdriverfactory.h"

namespace {

// "Ping" query for the liveness probe below — needs to be valid, trivial SQL
// for whichever driver is active. Postgres/SQLite/MySQL/MSSQL all accept a
// bare "SELECT 1"; Oracle requires selecting from something, hence
// "SELECT 1 FROM DUAL" in qcdriveroracle.cpp.
constexpr const char * kPingQuery = "SELECT 1";

std::string toParamText(const QcSqlBase::QcVariant & value)
{
    return std::visit([](const auto & alt) -> std::string {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return alt;
        } else if constexpr (std::is_same_v<T, std::monostate>) {
            return {}; // unreachable: caller sends NULL params separately
        } else if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
            // bytea hex format ("\x..."), not raw bytes: paramValues below is a
            // NUL-terminated C string (paramLengths is nullptr, so PQexecParams
            // reads up to the first '\0'), and raw binary data can itself
            // contain embedded NUL bytes that would silently truncate it.
            static const char hexDigits[] = "0123456789abcdef";
            std::string hex;
            hex.reserve(2 + alt.size() * 2);
            hex += "\\x";
            for (std::byte b : alt) {
                const auto byteValue = std::to_integer<unsigned char>(b);
                hex += hexDigits[byteValue >> 4];
                hex += hexDigits[byteValue & 0x0F];
            }
            return hex;
        } else {
            return std::to_string(alt);
        }
    }, value);
}

class QcPostgreSqlConnection : public IQcDriverConnection
{
public:
    explicit QcPostgreSqlConnection(const QcConnectionParams & params)
    {
        std::string conninfo = "host=" + params.host
            + " port=" + params.port
            + " dbname=" + params.database
            + " user=" + params.user
            + " password=" + params.password;
        if (params.connectTimeoutSeconds > 0) {
            conninfo += " connect_timeout=" + std::to_string(params.connectTimeoutSeconds);
        }

        m_conn = PQconnectdb(conninfo.c_str());
        if (PQstatus(m_conn) != CONNECTION_OK) {
            const std::string error = PQerrorMessage(m_conn);
            PQfinish(m_conn);
            m_conn = nullptr;
            throw std::runtime_error("QcNativeConnection: " + error);
        }
    }

    ~QcPostgreSqlConnection() override
    {
        if (m_conn) {
            PQfinish(m_conn);
        }
    }

    bool isAlive() override
    {
        // PQstatus() alone only reflects the last *known* state — a connection
        // killed server-side still reads CONNECTION_OK locally until something
        // actually attempts I/O on it (verified empirically: pg_terminate_backend()
        // from another session doesn't change the victim's PQstatus() until it
        // tries to do something itself). A trivial real query is the probe.
        PGresult * res = PQexec(m_conn, kPingQuery);
        const bool ok = res && PQresultStatus(res) == PGRES_TUPLES_OK && PQstatus(m_conn) == CONNECTION_OK;
        if (res) {
            PQclear(res);
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
        PGresult * res;
        if (params.empty()) {
            // PQexecParams only allows a single command; keep the simple-query
            // path (which allows e.g. ";"-separated statements) when there's
            // nothing to bind.
            res = PQexec(m_conn, sql.c_str());
        } else {
            std::vector<std::string> paramStorage(params.size());
            std::vector<const char *> paramValues(params.size());
            for (std::size_t i = 0; i < params.size(); ++i) {
                if (std::holds_alternative<std::monostate>(params[i])) {
                    paramValues[i] = nullptr; // SQL NULL
                } else {
                    paramStorage[i] = toParamText(params[i]);
                    paramValues[i] = paramStorage[i].c_str();
                }
            }
            res = PQexecParams(m_conn, sql.c_str(), static_cast<int>(params.size()), nullptr,
                                paramValues.data(), nullptr, nullptr, 0);
        }

        const ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            PQclear(res);
            return std::nullopt;
        }

        QcResultSet rows;
        if (status == PGRES_TUPLES_OK) {
            const int rowCount = PQntuples(res);
            const int colCount = PQnfields(res);
            if (outColumnNames) {
                outColumnNames->reserve(static_cast<std::size_t>(colCount));
                for (int c = 0; c < colCount; ++c) {
                    outColumnNames->emplace_back(PQfname(res, c));
                }
            }
            rows.reserve(static_cast<std::size_t>(rowCount));
            for (int r = 0; r < rowCount; ++r) {
                QcResultRow row;
                row.reserve(static_cast<std::size_t>(colCount));
                for (int c = 0; c < colCount; ++c) {
                    if (PQgetisnull(res, r, c)) {
                        row.emplace_back(std::monostate{});
                    } else {
                        row.emplace_back(std::string(PQgetvalue(res, r, c), static_cast<std::size_t>(PQgetlength(res, r, c))));
                    }
                }
                rows.push_back(std::move(row));
            }
        }
        PQclear(res);
        return rows;
    }

private:
    PGconn * m_conn = nullptr;
};

} // namespace

std::unique_ptr<IQcDriverConnection> createPostgreSqlConnection(const QcConnectionParams & params)
{
    return std::make_unique<QcPostgreSqlConnection>(params);
}

std::string postgreSqlDriverInfo()
{
    return "PostgreSQL (libpq) client version " + std::to_string(PQlibVersion());
}
