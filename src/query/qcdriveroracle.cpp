#include <oci.h>

#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <variant>

#include "qcdriverconnection.h"
#include "qcdriverfactory.h"

namespace {

// One physical OCI connection needs three handles kept alive together
// (environment, error, service context) -- bundled into one struct so
// QcOracleConnection (below) can own them as a single pointer, the same
// shape every helper function here already expects.
struct OracleConnection
{
    OCIEnv * envhp = nullptr;
    OCIError * errhp = nullptr;
    OCISvcCtx * svchp = nullptr;
    // Oracle has no per-statement "autocommit" mode of its own -- a
    // transaction is always implicitly open, only ended by an explicit
    // COMMIT/ROLLBACK. PostgreSQL/SQLite default the other way (autocommit
    // ON: each statement commits immediately unless the caller opts into a
    // multi-statement transaction with an explicit "BEGIN"), and this
    // project's own test suite relies on exactly that default (see
    // TransactionCommitPersistsChanges/TransactionRollbackDiscardsChanges
    // in test_integration_dml.cpp -- verified directly that without this
    // flag, an insert made *before* "BEGIN" gets silently undone by a later
    // ROLLBACK too, since it was never its own committed transaction to
    // begin with). This flag simulates PostgreSQL/SQLite's autocommit
    // default on top of Oracle's always-open transaction: false (the
    // default) makes executeStatement() commit every statement immediately
    // (OCI_COMMIT_ON_SUCCESS); "BEGIN" sets it true, deferring commit until
    // the matching COMMIT/ROLLBACK sets it back to false.
    bool inExplicitTransaction = false;
};

std::string oracleErrorText(OCIError * errhp)
{
    char buf[2048] = {0};
    sb4 errcode = 0;
    OCIErrorGet(errhp, 1, nullptr, &errcode, reinterpret_cast<OraText *>(buf), sizeof(buf), OCI_HTYPE_ERROR);
    return buf;
}

bool oracleOk(sword rc)
{
    return rc == OCI_SUCCESS || rc == OCI_SUCCESS_WITH_INFO;
}

// "Ping" query -- see the identically-named constant in
// qcdriverpostgresql.cpp for why a real round-trip is needed. Oracle has no
// bare "SELECT 1" (there's no implicit one-row source table) -- DUAL is the
// documented stand-in.
constexpr const char * kPingQuery = "SELECT 1 FROM DUAL";

// One-shot helper for the handful of fire-and-forget statements the
// constructor/executeReturningCore() below need (DDL/session ALTER/COMMIT/
// ROLLBACK) that never bind parameters or return rows -- executeStatement()
// below is the real, general path used for everything QcNativeConnection::
// execute() callers actually send.
bool runSimpleStatement(OracleConnection * conn, const char * sql)
{
    OCIStmt * stmtp = nullptr;
    if (!oracleOk(OCIHandleAlloc(conn->envhp, reinterpret_cast<void **>(&stmtp), OCI_HTYPE_STMT, 0, nullptr))) {
        return false;
    }
    sword rc = OCIStmtPrepare(stmtp, conn->errhp, reinterpret_cast<const OraText *>(sql),
                               static_cast<ub4>(std::strlen(sql)), OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (oracleOk(rc)) {
        rc = OCIStmtExecute(conn->svchp, stmtp, conn->errhp, 1, 0, nullptr, nullptr, OCI_DEFAULT);
    }
    OCIHandleFree(stmtp, OCI_HTYPE_STMT);
    return oracleOk(rc);
}

OracleConnection * openConnection(const QcConnectionParams & params)
{
    auto * conn = new OracleConnection();

    // 873 = AL32UTF8's well-known, stable Oracle charset id -- forces the
    // client-side default charset explicitly rather than relying on the
    // embedding process's NLS_LANG environment variable (which this library
    // can't assume is set, or set correctly): verified directly that
    // without this, multi-byte UTF-8 text (Japanese/emoji test data) comes
    // back corrupted with '?' replacement bytes, both for plain VARCHAR2
    // binds and CLOB content, even though the database itself is AL32UTF8.
    if (!oracleOk(OCIEnvNlsCreate(&conn->envhp, OCI_THREADED, nullptr, nullptr, nullptr, nullptr, 0, nullptr, 873, 873))) {
        delete conn;
        throw std::runtime_error("QcNativeConnection: OCIEnvNlsCreate failed");
    }
    OCIHandleAlloc(conn->envhp, reinterpret_cast<void **>(&conn->errhp), OCI_HTYPE_ERROR, 0, nullptr);

    // Easy Connect string: host:port/service_name -- params.database doubles
    // as the service name here, the same way it doubles as a file path for
    // SQLite.
    std::string connectString = params.host;
    if (!params.port.empty()) {
        connectString += ":" + params.port;
    }
    connectString += "/" + params.database;

    const sword rc = OCILogon2(conn->envhp, conn->errhp, &conn->svchp,
        reinterpret_cast<const OraText *>(params.user.data()), static_cast<ub4>(params.user.size()),
        reinterpret_cast<const OraText *>(params.password.data()), static_cast<ub4>(params.password.size()),
        reinterpret_cast<const OraText *>(connectString.data()), static_cast<ub4>(connectString.size()),
        OCI_DEFAULT);
    if (!oracleOk(rc)) {
        const std::string error = oracleErrorText(conn->errhp);
        OCIHandleFree(conn->errhp, OCI_HTYPE_ERROR);
        OCIHandleFree(conn->envhp, OCI_HTYPE_ENV);
        delete conn;
        throw std::runtime_error("QcNativeConnection: " + error);
    }

    // ISO date/timestamp text both ways (bind and fetch) -- Oracle's
    // implicit string<->DATE conversion otherwise goes through the
    // session's default NLS_DATE_FORMAT (install/locale-dependent), which
    // wouldn't match the plain "YYYY-MM-DD"/"YYYY-MM-DD HH24:MI:SS" strings
    // this project (and any caller binding a date/timestamp as plain text)
    // uses. Session-scoped, so this only needs to run once per connection.
    runSimpleStatement(conn, "ALTER SESSION SET NLS_DATE_FORMAT='YYYY-MM-DD' NLS_TIMESTAMP_FORMAT='YYYY-MM-DD HH24:MI:SS'");

    // connectTimeoutSeconds: unlike PostgreSQL's connect_timeout (a libpq
    // conninfo parameter honored during the connect round-trip itself),
    // OCILogon2's simplified one-call connect flow doesn't expose a knob
    // for this before that round-trip happens -- not implemented; the value
    // is accepted (for API parity with the other drivers) but ignored here.
    (void)params.connectTimeoutSeconds;

    return conn;
}

void closeConnection(OracleConnection * conn)
{
    if (conn->svchp) {
        OCILogoff(conn->svchp, conn->errhp);
    }
    if (conn->errhp) {
        OCIHandleFree(conn->errhp, OCI_HTYPE_ERROR);
    }
    if (conn->envhp) {
        // Frees every handle/descriptor implicitly allocated under this
        // environment too (service context, statement handles left over
        // from a failure path, ...) -- the documented OCI cleanup shortcut.
        OCIHandleFree(conn->envhp, OCI_HTYPE_ENV);
    }
    delete conn;
}

bool checkAlive(OracleConnection * conn)
{
    OCIStmt * stmtp = nullptr;
    if (!oracleOk(OCIHandleAlloc(conn->envhp, reinterpret_cast<void **>(&stmtp), OCI_HTYPE_STMT, 0, nullptr))) {
        return false;
    }
    sword rc = OCIStmtPrepare(stmtp, conn->errhp, reinterpret_cast<const OraText *>(kPingQuery),
                               static_cast<ub4>(std::strlen(kPingQuery)), OCI_NTV_SYNTAX, OCI_DEFAULT);
    char buf[32] = {0};
    sb2 ind = 0;
    OCIDefine * defnp = nullptr;
    if (oracleOk(rc)) {
        rc = OCIDefineByPos(stmtp, &defnp, conn->errhp, 1, buf, sizeof(buf), SQLT_STR, &ind, nullptr, nullptr, OCI_DEFAULT);
    }
    if (oracleOk(rc)) {
        rc = OCIStmtExecute(conn->svchp, stmtp, conn->errhp, 1, 0, nullptr, nullptr, OCI_DEFAULT);
    }
    OCIHandleFree(stmtp, OCI_HTYPE_STMT);
    return oracleOk(rc);
}

// Backing storage for one bound parameter -- must outlive the
// OCIStmtExecute() call the bind was set up for, since OCI reads directly
// from these addresses at execute time (not at OCIBindByPos time). Callers
// pre-size a std::vector<BoundParam> once (never push_back mid-loop) so
// these addresses stay stable, the same reason the PostgreSQL backend
// pre-sizes paramStorage/paramValues.
struct BoundParam
{
    std::string text; // backing storage for scalar/string SQLT_STR binds
    std::vector<std::byte> bytes; // backing storage for SQLT_BIN binds
    sb2 indicator = 0; // OCI_IND_NULL (-1) or 0
};

// Binds one QcVariant at 1-based `position`. Every alternative binds
// directly (no LOB locator) -- verified directly that OCIBindByPos's
// SQLT_STR/SQLT_BIN accept values up to at least 7 MB with `value_sz` set
// correctly (matching this project's own "Huge" text/binary integration
// tests), so the classic ~4000-byte "direct bind" ceiling some OCI
// documentation warns about doesn't apply here in practice, and there is no
// LOB-locator complexity to carry on the bind side at all -- only fetching
// (below) still needs it, since OCIDefineByPos requires a fixed buffer size
// chosen before a row's actual length is known.
void bindParam(OCIStmt * stmtp, OCIError * errhp, ub4 position, const QcSqlBase::QcVariant & value, BoundParam & storage)
{
    OCIBind * bindp = nullptr;

    std::visit([&](const auto & alt) {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            storage.indicator = OCI_IND_NULL;
            OCIBindByPos(stmtp, &bindp, errhp, position, nullptr, 0, SQLT_STR, &storage.indicator, nullptr, nullptr, 0, nullptr, OCI_DEFAULT);
        } else if constexpr (std::is_same_v<T, std::string>) {
            storage.text = alt;
            OCIBindByPos(stmtp, &bindp, errhp, position, storage.text.data(), static_cast<sb4>(storage.text.size()) + 1,
                         SQLT_STR, &storage.indicator, nullptr, nullptr, 0, nullptr, OCI_DEFAULT);
        } else if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
            storage.bytes = alt;
            // A null data pointer with a non-zero size is invalid even when
            // the size is 0 -- pass a stable 1-byte dummy for the empty-blob
            // case (indicator/size both say "0 bytes", the pointer is never
            // dereferenced for a zero-length bind, but OCI still wants it non-null).
            static const std::byte dummy{};
            void * dataPtr = storage.bytes.empty() ? const_cast<std::byte *>(&dummy) : storage.bytes.data();
            OCIBindByPos(stmtp, &bindp, errhp, position, dataPtr, static_cast<sb4>(storage.bytes.size()),
                         SQLT_BIN, &storage.indicator, nullptr, nullptr, 0, nullptr, OCI_DEFAULT);
        } else {
            // long long / double
            storage.text = std::to_string(alt);
            OCIBindByPos(stmtp, &bindp, errhp, position, storage.text.data(), static_cast<sb4>(storage.text.size()) + 1,
                         SQLT_STR, &storage.indicator, nullptr, nullptr, 0, nullptr, OCI_DEFAULT);
        }
    }, value);
}

// Backing state for one defined SELECT output column, chosen per-column
// once its real data type is known (see fetchRows() below) -- CLOB/BLOB
// columns fetch through a LOB locator (their length isn't known until the
// row is fetched, unlike a bind where the caller already has the exact
// value in hand), everything else fetches as plain text into a fixed
// buffer sized generously for VARCHAR2/NUMBER/DATE-as-text.
struct ColumnDef
{
    bool isClob = false;
    bool isBlob = false;
    std::vector<char> textBuf;
    OCILobLocator * lob = nullptr;
    sb2 indicator = 0;
    ub2 returnLen = 0;
};

constexpr sb4 kColumnBufferSize = 4001; // headroom over VARCHAR2(4000)/NUMBER-as-text/DATE-as-text

std::optional<QcResultSet> fetchRows(OracleConnection * conn, OCIStmt * stmtp, QcSqlBase::QcStringList * outColumnNames = nullptr)
{
    ub4 paramCount = 0;
    OCIAttrGet(stmtp, OCI_HTYPE_STMT, &paramCount, nullptr, OCI_ATTR_PARAM_COUNT, conn->errhp);

    std::vector<ColumnDef> columns(paramCount);
    std::vector<OCIDefine *> defines(paramCount, nullptr);
    if (outColumnNames) {
        outColumnNames->reserve(paramCount);
    }

    for (ub4 c = 0; c < paramCount; ++c) {
        void * parmdp = nullptr;
        if (!oracleOk(OCIParamGet(stmtp, OCI_HTYPE_STMT, conn->errhp, &parmdp, c + 1))) {
            return std::nullopt;
        }
        ub2 dataType = 0;
        OCIAttrGet(parmdp, OCI_DTYPE_PARAM, &dataType, nullptr, OCI_ATTR_DATA_TYPE, conn->errhp);
        if (outColumnNames) {
            // OraText* into memory OCI owns (not null-terminated, length in
            // nameLen) -- copied into a std::string immediately, before
            // OCIDescriptorFree(parmdp, ...) below invalidates it.
            OraText * namePtr = nullptr;
            ub4 nameLen = 0;
            OCIAttrGet(parmdp, OCI_DTYPE_PARAM, &namePtr, &nameLen, OCI_ATTR_NAME, conn->errhp);
            outColumnNames->emplace_back(reinterpret_cast<const char *>(namePtr), nameLen);
        }
        OCIDescriptorFree(parmdp, OCI_DTYPE_PARAM);

        ColumnDef & col = columns[c];
        sword rc;
        if (dataType == SQLT_CLOB) {
            col.isClob = true;
            OCIDescriptorAlloc(conn->envhp, reinterpret_cast<void **>(&col.lob), OCI_DTYPE_LOB, 0, nullptr);
            rc = OCIDefineByPos(stmtp, &defines[c], conn->errhp, c + 1, &col.lob, sizeof(col.lob), SQLT_CLOB,
                                 &col.indicator, nullptr, nullptr, OCI_DEFAULT);
        } else if (dataType == SQLT_BLOB) {
            col.isBlob = true;
            OCIDescriptorAlloc(conn->envhp, reinterpret_cast<void **>(&col.lob), OCI_DTYPE_LOB, 0, nullptr);
            rc = OCIDefineByPos(stmtp, &defines[c], conn->errhp, c + 1, &col.lob, sizeof(col.lob), SQLT_BLOB,
                                 &col.indicator, nullptr, nullptr, OCI_DEFAULT);
        } else {
            col.textBuf.assign(kColumnBufferSize, 0);
            rc = OCIDefineByPos(stmtp, &defines[c], conn->errhp, c + 1, col.textBuf.data(), kColumnBufferSize, SQLT_STR,
                                 &col.indicator, &col.returnLen, nullptr, OCI_DEFAULT);
        }
        if (!oracleOk(rc)) {
            for (ColumnDef & done : columns) {
                if (done.lob) OCIDescriptorFree(done.lob, OCI_DTYPE_LOB);
            }
            return std::nullopt;
        }
    }

    QcResultSet rows;
    while (true) {
        const sword rc = OCIStmtFetch2(stmtp, conn->errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
        if (rc == OCI_NO_DATA) {
            break;
        }
        if (!oracleOk(rc)) {
            for (ColumnDef & col : columns) {
                if (col.lob) OCIDescriptorFree(col.lob, OCI_DTYPE_LOB);
            }
            return std::nullopt;
        }

        QcResultRow row;
        row.reserve(paramCount);
        for (ColumnDef & col : columns) {
            if (col.indicator == -1) {
                row.emplace_back(std::monostate{});
                continue;
            }
            if (col.isClob) {
                oraub8 len = 0;
                OCILobGetLength2(conn->svchp, conn->errhp, col.lob, &len);
                // Sized in characters; over-allocate for AL32UTF8's worst
                // case (4 bytes/char) since the byte length isn't known
                // until after the read -- see the byte_amtp/char_amtp
                // comment on OCILobRead2 below.
                std::string text(len * 4 + 16, '\0');
                oraub8 byteAmt = 0;
                oraub8 charAmt = len;
                if (len > 0) {
                    // Passing BOTH byte_amtp and char_amtp: char_amtp
                    // carries the request (upper-bound character count),
                    // byte_amtp comes back filled with the actual byte
                    // count consumed -- exactly what std::string needs to
                    // size itself correctly. Verified directly that
                    // byte_amtp alone (char_amtp null) truncates/miscounts
                    // once the environment's charset is UTF-8-aware
                    // (OCIEnvNlsCreate above), for both ASCII and
                    // multi-byte content -- this combined form is what
                    // actually round-trips correctly.
                    OCILobRead2(conn->svchp, conn->errhp, col.lob, &byteAmt, &charAmt, 1,
                                text.data(), static_cast<oraub8>(text.size()), OCI_ONE_PIECE, nullptr, nullptr, 0, SQLCS_IMPLICIT);
                }
                text.resize(byteAmt);
                row.emplace_back(std::move(text));
            } else if (col.isBlob) {
                oraub8 len = 0;
                OCILobGetLength2(conn->svchp, conn->errhp, col.lob, &len);
                std::vector<std::byte> bytes(len);
                if (len > 0) {
                    oraub8 amt = len;
                    OCILobRead2(conn->svchp, conn->errhp, col.lob, &amt, nullptr, 1,
                                bytes.data(), static_cast<oraub8>(bytes.size()), OCI_ONE_PIECE, nullptr, nullptr, 0, SQLCS_IMPLICIT);
                }
                row.emplace_back(std::move(bytes));
            } else {
                row.emplace_back(std::string(col.textBuf.data(), col.returnLen));
            }
        }
        rows.push_back(std::move(row));
    }

    for (ColumnDef & col : columns) {
        if (col.lob) {
            OCIDescriptorFree(col.lob, OCI_DTYPE_LOB);
        }
    }
    return rows;
}

std::optional<QcResultSet> executeStatement(OracleConnection * conn, const std::string & sql, const QcSqlBase::QcVariantList & params,
                                             QcSqlBase::QcStringList * outColumnNames = nullptr)
{
    // Oracle has no "BEGIN" transaction-start statement -- a bare literal
    // BEGIN parses as the (incomplete) start of a PL/SQL block and fails
    // (verified directly: PLS-00103, "encountered symbol end-of-file").
    // Unlike PostgreSQL/SQLite, a transaction is always implicitly open
    // here, so there's nothing to actually *start* -- but see
    // OracleConnection::inExplicitTransaction for why this still needs to
    // flip a flag, not just no-op.
    if (sql == "BEGIN") {
        conn->inExplicitTransaction = true;
        return QcResultSet{};
    }
    // COMMIT/ROLLBACK, unlike BEGIN, prepare and execute here exactly as
    // ordinary SQL text (verified directly) -- falls through to the normal
    // path below, this only resets the autocommit-simulation flag.
    if (sql == "COMMIT" || sql == "ROLLBACK") {
        conn->inExplicitTransaction = false;
    }

    OCIStmt * stmtp = nullptr;
    if (!oracleOk(OCIHandleAlloc(conn->envhp, reinterpret_cast<void **>(&stmtp), OCI_HTYPE_STMT, 0, nullptr))) {
        return std::nullopt;
    }

    sword rc = OCIStmtPrepare(stmtp, conn->errhp, reinterpret_cast<const OraText *>(sql.data()),
                               static_cast<ub4>(sql.size()), OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (!oracleOk(rc)) {
        OCIHandleFree(stmtp, OCI_HTYPE_STMT);
        return std::nullopt;
    }

    // Pre-sized, never push_back'd -- see BoundParam's doc comment for why
    // (OCI reads these addresses at execute time, not at bind time).
    std::vector<BoundParam> boundParams(params.size());
    for (std::size_t i = 0; i < params.size(); ++i) {
        bindParam(stmtp, conn->errhp, static_cast<ub4>(i + 1), params[i], boundParams[i]);
    }

    ub2 stmtType = 0;
    OCIAttrGet(stmtp, OCI_HTYPE_STMT, &stmtType, nullptr, OCI_ATTR_STMT_TYPE, conn->errhp);
    const bool isSelect = (stmtType == OCI_STMT_SELECT);

    // iters=0 for SELECT: this is the standard OCI pattern for a result
    // shape not known ahead of time -- it performs an implicit describe
    // (populating the parameter/column metadata fetchRows() reads via
    // OCIParamGet) without materializing any row data yet, which then
    // happens through the OCIStmtFetch2 loop. iters=1 for DML/DDL actually
    // performs the statement.
    //
    // OCI_COMMIT_ON_SUCCESS unless a caller-managed transaction is open
    // (see OracleConnection::inExplicitTransaction) -- this is what makes
    // an ordinary INSERT/UPDATE/DELETE commit immediately by default,
    // matching PostgreSQL/SQLite's autocommit default, rather than sitting
    // uncommitted until something else happens to trigger Oracle's next
    // implicit commit boundary (e.g. the next DDL statement).
    const ub4 execMode = conn->inExplicitTransaction ? OCI_DEFAULT : OCI_COMMIT_ON_SUCCESS;
    rc = OCIStmtExecute(conn->svchp, stmtp, conn->errhp, isSelect ? 0 : 1, 0, nullptr, nullptr, execMode);

    std::optional<QcResultSet> result;
    if (oracleOk(rc)) {
        result = isSelect ? fetchRows(conn, stmtp, outColumnNames) : std::optional<QcResultSet>(QcResultSet{});
    }

    OCIHandleFree(stmtp, OCI_HTYPE_STMT);
    return result;
}

// Backing storage for one OUT bind (a "RETURNING ... INTO :N" placeholder)
// -- structurally the same idea as BoundParam above, just written to by OCI
// instead of read from. Fixed-size text buffer, the same generic-text
// convention fetchRows() already uses for untyped SELECT columns
// (kColumnBufferSize): a RETURNING column that's actually a CLOB/BLOB isn't
// supported by this path (no test in this project's suite needs it; fixing
// that would need the same LOB-locator dance fetchRows() uses, which OUT
// binds don't support the same way -- left as a documented gap, not
// implemented speculatively). `returnLen` mirrors OCIDefineByPos's rlenp:
// filled in by OCI with the actual returned byte length once
// OCIStmtExecute() completes, the exact same output-only role
// ColumnDef::returnLen plays for a SELECT column above.
struct OutBind
{
    std::vector<char> buf;
    sb2 indicator = 0;
    ub2 returnLen = 0;
};

// Oracle-only counterpart to executeStatement() above, for a statement whose
// text ends with `returningColumnCount` "RETURNING ... INTO :N, :N+1, ..."
// OUT-bind placeholders (built by QcSqlDialect::returningClause(), see
// QcSqlInsert/Update/Delete::returning()) immediately after the ordinary
// `params.size()` IN placeholders. No PL/SQL BEGIN/END block needed -- a
// bare INSERT/UPDATE/DELETE with a trailing RETURNING...INTO is valid,
// directly executable DML on its own, so this reuses the same
// OCIStmtPrepare/OCIStmtExecute flow as executeStatement(), just with extra
// OCIBindByPos calls for the OUT placeholders and no fetchRows() call
// (RETURNING...INTO is never a SELECT-shaped statement).
//
// Returns the raw OUT-bind buffers rather than an already-assembled
// QcResultRow/QcNamedRow -- shared by executeReturning() (positional) and
// executeReturningNamed() (named) below, which differ only in how they turn
// these buffers into a row (index-keyed vs. returningColumnNames-keyed).
// Caller must have already checked returningColumnCount > 0.
std::optional<std::vector<OutBind>> executeReturningCore(OracleConnection * conn, const std::string & sql,
                                                          const QcSqlBase::QcVariantList & params,
                                                          std::size_t returningColumnCount)
{
    OCIStmt * stmtp = nullptr;
    if (!oracleOk(OCIHandleAlloc(conn->envhp, reinterpret_cast<void **>(&stmtp), OCI_HTYPE_STMT, 0, nullptr))) {
        return std::nullopt;
    }

    sword rc = OCIStmtPrepare(stmtp, conn->errhp, reinterpret_cast<const OraText *>(sql.data()),
                               static_cast<ub4>(sql.size()), OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (!oracleOk(rc)) {
        OCIHandleFree(stmtp, OCI_HTYPE_STMT);
        return std::nullopt;
    }

    // IN binds -- identical to executeStatement()'s.
    std::vector<BoundParam> boundParams(params.size());
    for (std::size_t i = 0; i < params.size(); ++i) {
        bindParam(stmtp, conn->errhp, static_cast<ub4>(i + 1), params[i], boundParams[i]);
    }

    // OUT binds -- one per RETURNING column, positioned right after the IN
    // binds (matches the placeholder numbering QcSqlDialect::returningClause()
    // assigned via placeholder(), starting at params.size() + 1). Pre-sized,
    // never push_back'd -- same reasoning as boundParams above and
    // BoundParam's doc comment: OCI reads/writes these addresses at execute
    // time, not at bind time.
    std::vector<OutBind> outBinds(returningColumnCount);
    for (std::size_t i = 0; i < returningColumnCount; ++i) {
        outBinds[i].buf.assign(kColumnBufferSize, 0);
        OCIBind * bindp = nullptr;
        const ub4 position = static_cast<ub4>(params.size() + i + 1);
        rc = OCIBindByPos(stmtp, &bindp, conn->errhp, position, outBinds[i].buf.data(), kColumnBufferSize, SQLT_STR,
                           &outBinds[i].indicator, &outBinds[i].returnLen, nullptr, 0, nullptr, OCI_DEFAULT);
        if (!oracleOk(rc)) {
            OCIHandleFree(stmtp, OCI_HTYPE_STMT);
            return std::nullopt;
        }
    }

    // iters=1: this is always a DML statement (INSERT/UPDATE/DELETE), never
    // a SELECT -- RETURNING...INTO's OUT binds are populated as a
    // side-effect of running the statement itself, not fetched afterward.
    //
    // Unlike executeStatement(), NEVER OCI_COMMIT_ON_SUCCESS here -- verified
    // directly (see UpdateReturningOnMultipleRowsFailsCleanlyOnOracle in
    // test_integration_dml.cpp) that a DML affecting more rows than this
    // scalar OUT bind has room for (see OutBind's doc comment) still applies
    // its underlying changes -- and, under OCI_COMMIT_ON_SUCCESS, would
    // commit them -- before OCIStmtExecute reports failure
    // (ORA-24369, "required callbacks not registered for one or more bind
    // handles") for the RETURNING side specifically. Silently committing a
    // change the caller was just told failed (nullopt) would be a
    // correctness hazard, not merely an unsupported case. Always execute
    // with OCI_DEFAULT (no autocommit) instead, then commit or roll back
    // explicitly below based on the real outcome -- except inside a
    // caller-managed explicit transaction (OracleConnection::inExplicitTransaction,
    // set by a literal "BEGIN"), where committing or rolling back here would
    // improperly reach past the caller's own transaction boundary.
    rc = OCIStmtExecute(conn->svchp, stmtp, conn->errhp, 1, 0, nullptr, nullptr, OCI_DEFAULT);

    std::optional<std::vector<OutBind>> result;
    if (oracleOk(rc)) {
        result = std::move(outBinds);
        if (!conn->inExplicitTransaction) {
            runSimpleStatement(conn, "COMMIT");
        }
    } else if (!conn->inExplicitTransaction) {
        runSimpleStatement(conn, "ROLLBACK");
    }

    OCIHandleFree(stmtp, OCI_HTYPE_STMT);
    return result;
}

// One OUT bind's buffer as a QcVariant -- shared by both executeReturning()
// and executeReturningNamed() below to avoid repeating the
// indicator/text decoding.
QcSqlBase::QcVariant outBindToVariant(const OutBind & out)
{
    if (out.indicator == -1) {
        return QcSqlBase::QcVariant{std::monostate{}};
    }
    return QcSqlBase::QcVariant{std::string(out.buf.data(), out.returnLen)};
}

class QcOracleConnection : public IQcDriverConnection
{
public:
    explicit QcOracleConnection(const QcConnectionParams & params)
        : m_conn(openConnection(params))
    {
    }

    ~QcOracleConnection() override
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
        return executeStatement(m_conn, sql, params, outColumnNames);
    }

    std::optional<QcResultSet> executeReturning(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                                 std::size_t returningColumnCount) override
    {
        if (returningColumnCount == 0) {
            return executeStatement(m_conn, sql, params);
        }

        auto outBinds = executeReturningCore(m_conn, sql, params, returningColumnCount);
        if (!outBinds) {
            return std::nullopt;
        }

        QcResultRow row;
        row.reserve(outBinds->size());
        for (const OutBind & out : *outBinds) {
            row.push_back(outBindToVariant(out));
        }
        return QcResultSet{std::move(row)};
    }

    std::optional<QcNamedResultSet> executeReturningNamed(const std::string & sql, const QcSqlBase::QcVariantList & params,
                                                           std::size_t returningColumnCount,
                                                           const QcSqlBase::QcStringList & returningColumnNames) override
    {
        if (returningColumnCount == 0) {
            QcSqlBase::QcStringList columnNames;
            auto rows = executeStatement(m_conn, sql, params, &columnNames);
            if (!rows) {
                return std::nullopt;
            }
            return toNamedResultSet(columnNames, *rows);
        }

        auto outBinds = executeReturningCore(m_conn, sql, params, returningColumnCount);
        if (!outBinds) {
            return std::nullopt;
        }

        QcNamedRow row;
        for (std::size_t i = 0; i < outBinds->size() && i < returningColumnNames.size(); ++i) {
            row[returningColumnNames[i]] = outBindToVariant((*outBinds)[i]);
        }
        return QcNamedResultSet{std::move(row)};
    }

private:
    OracleConnection * m_conn;
};

} // namespace

std::unique_ptr<IQcDriverConnection> createOracleConnection(const QcConnectionParams & params)
{
    return std::make_unique<QcOracleConnection>(params);
}

std::string oracleDriverInfo()
{
    sword major = 0, minor = 0, update = 0, patch = 0, portUpdate = 0;
    OCIClientVersion(&major, &minor, &update, &patch, &portUpdate);
    return "Oracle (OCI) client version " + std::to_string(major) + "." + std::to_string(minor)
        + "." + std::to_string(update);
}
