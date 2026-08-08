#include "qcconnectionpool.h"

#include <cassert>
#include <stdexcept>
#include <utility>

QcConnectionPool::Lease::Lease(QcConnectionPool * pool, std::unique_ptr<QcNativeConnection> connection)
    : m_pool(pool)
    , m_connection(std::move(connection))
{
}

QcConnectionPool::Lease::Lease(Lease && other) noexcept
    : m_pool(other.m_pool)
    , m_connection(std::move(other.m_connection))
{
    other.m_pool = nullptr;
}

QcConnectionPool::Lease & QcConnectionPool::Lease::operator=(Lease && other) noexcept
{
    if (this != &other) {
        if (m_pool) {
            m_pool->release(std::move(m_connection));
        }
        m_pool = other.m_pool;
        m_connection = std::move(other.m_connection);
        other.m_pool = nullptr;
    }
    return *this;
}

QcConnectionPool::Lease::~Lease()
{
    if (m_pool) {
        m_pool->release(std::move(m_connection));
    }
}

QcNativeConnection & QcConnectionPool::Lease::connection()
{
    return *m_connection;
}

QcConnectionPool::QcConnectionPool(QcConnectionParams params, Mode mode, std::size_t size)
    : m_params(std::move(params))
    , m_mode(mode)
    , m_size(size)
{
    if (m_size == 0) {
        throw std::invalid_argument("QcConnectionPool: pool size must be at least 1");
    }

    if (m_mode == Mode::Permanent) {
        m_idleConnections.reserve(m_size);
        for (std::size_t i = 0; i < m_size; ++i) {
            m_idleConnections.push_back(std::make_unique<QcNativeConnection>(m_params));
        }
    }
}

QcConnectionPool::~QcConnectionPool()
{
    // A live Lease holds a raw pointer back to its pool, so every lease
    // acquired from this pool must be released before it is destroyed.
    assert(m_mode == Mode::Permanent ? m_idleConnections.size() == m_size : m_leasedCount == 0);
}

QcConnectionPool::Lease QcConnectionPool::acquire()
{
    return acquireImpl(std::nullopt).value();
}

std::optional<QcConnectionPool::Lease> QcConnectionPool::tryAcquire(std::chrono::milliseconds timeout)
{
    return acquireImpl(timeout);
}

std::optional<QcConnectionPool::Lease> QcConnectionPool::acquireImpl(std::optional<std::chrono::milliseconds> timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    const auto ready = [this] {
        return m_mode == Mode::Permanent ? !m_idleConnections.empty() : m_leasedCount < m_size;
    };

    if (timeout) {
        if (!m_available.wait_for(lock, *timeout, ready)) {
            return std::nullopt;
        }
    } else {
        m_available.wait(lock, ready);
    }

    if (m_mode == Mode::Permanent) {
        std::unique_ptr<QcNativeConnection> connection = std::move(m_idleConnections.back());
        m_idleConnections.pop_back();
        lock.unlock();

        if (!connection || !connection->isAlive()) {
            try {
                connection = std::make_unique<QcNativeConnection>(m_params);
            } catch (...) {
                std::lock_guard<std::mutex> relock(m_mutex);
                m_idleConnections.push_back(nullptr); // slot still owed a reconnect
                m_available.notify_one();
                throw;
            }
        }
        return Lease(this, std::move(connection));
    }

    ++m_leasedCount;
    lock.unlock();

    try {
        return Lease(this, std::make_unique<QcNativeConnection>(m_params));
    } catch (...) {
        std::lock_guard<std::mutex> relock(m_mutex);
        --m_leasedCount;
        m_available.notify_one();
        throw;
    }
}

void QcConnectionPool::release(std::unique_ptr<QcNativeConnection> connection)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mode == Mode::Permanent) {
        m_idleConnections.push_back(std::move(connection));
    } else {
        connection.reset();
        --m_leasedCount;
    }
    m_available.notify_one();
}
