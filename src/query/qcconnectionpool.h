#ifndef QCCONNECTIONPOOL_H
#define QCCONNECTIONPOOL_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "qcconnectionparams.h"
#include "qcnativeconnection.h"

// Thread-safe pool of native database connections. Two modes:
//  - Permanent: `size` connections are opened once, up front, and reused —
//    acquire() hands out whichever is idle and blocks if all are checked
//    out. A pooled connection that died since it was last used is detected
//    (QcNativeConnection::isAlive()) and transparently replaced on acquire().
//  - OnDemand: no connections are kept open between uses; each acquire()
//    opens a fresh one and it's closed on release. `size` still bounds how
//    many may be open at once, so acquire() blocks past that limit too.
class QcConnectionPool
{
public:
    enum class Mode
    {
        Permanent,
        OnDemand
    };

    // RAII handle for one checked-out connection. Returns the connection to
    // the pool (Permanent) or closes it (OnDemand) when destroyed.
    class Lease
    {
    public:
        Lease(Lease && other) noexcept;
        Lease & operator=(Lease && other) noexcept;
        Lease(const Lease &) = delete;
        Lease & operator=(const Lease &) = delete;
        ~Lease();

        QcNativeConnection & connection();

    private:
        friend class QcConnectionPool;
        Lease(QcConnectionPool * pool, std::unique_ptr<QcNativeConnection> connection);

        QcConnectionPool * m_pool;
        std::unique_ptr<QcNativeConnection> m_connection;
    };

    QcConnectionPool(QcConnectionParams params, Mode mode, std::size_t size);
    ~QcConnectionPool();

    QcConnectionPool(const QcConnectionPool &) = delete;
    QcConnectionPool & operator=(const QcConnectionPool &) = delete;

    // Blocks until a connection is available.
    Lease acquire();

    // As above, but gives up and returns nullopt after `timeout` instead of
    // blocking forever.
    std::optional<Lease> tryAcquire(std::chrono::milliseconds timeout);

private:
    std::optional<Lease> acquireImpl(std::optional<std::chrono::milliseconds> timeout);
    void release(std::unique_ptr<QcNativeConnection> connection);

    QcConnectionParams m_params;
    Mode m_mode;
    std::size_t m_size;

    std::mutex m_mutex;
    std::condition_variable m_available;
    // Permanent mode only. A nullptr entry is a slot whose connection died
    // and couldn't be replaced yet (the DB was unreachable at the time) —
    // still counts towards m_size, retried on the next acquire().
    std::vector<std::unique_ptr<QcNativeConnection>> m_idleConnections;
    std::size_t m_leasedCount = 0; // OnDemand mode: connections currently in flight
};

#endif // QCCONNECTIONPOOL_H
