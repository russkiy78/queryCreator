#ifndef QCDEEPPTR_H
#define QCDEEPPTR_H

#include <memory>
#include <utility>

// Owning pointer to a heap-allocated T that *deep-copies* on copy, instead of
// either the shallow/aliasing copy a bare raw pointer would get wrong or the
// copy std::unique_ptr<T> refuses to do at all. Query-builder nodes
// (QcSqlQuery, QcSqlQueryElement, ...) reference subqueries by value
// semantics — copying a query must copy every subquery it references, not
// share it — while still getting automatic cleanup and move for free, and
// working with only a forward declaration of T (same incomplete-type
// tolerance as unique_ptr<T>, since the copying constructor/assignment are
// templates only instantiated where T is actually complete).
template <typename T>
class QcDeepPtr
{
public:
    QcDeepPtr() = default;
    QcDeepPtr(std::nullptr_t) {}
    explicit QcDeepPtr(const T & value) : m_ptr(std::make_unique<T>(value)) {}

    QcDeepPtr(const QcDeepPtr & other) : m_ptr(other.m_ptr ? std::make_unique<T>(*other.m_ptr) : nullptr) {}
    QcDeepPtr & operator=(const QcDeepPtr & other)
    {
        if (this != &other) {
            m_ptr = other.m_ptr ? std::make_unique<T>(*other.m_ptr) : nullptr;
        }
        return *this;
    }
    QcDeepPtr & operator=(std::nullptr_t)
    {
        m_ptr.reset();
        return *this;
    }

    QcDeepPtr(QcDeepPtr &&) noexcept = default;
    QcDeepPtr & operator=(QcDeepPtr &&) noexcept = default;
    ~QcDeepPtr() = default;

    T * get() const { return m_ptr.get(); }
    T * operator->() const { return m_ptr.get(); }
    T & operator*() const { return *m_ptr; }
    explicit operator bool() const { return static_cast<bool>(m_ptr); }

private:
    std::unique_ptr<T> m_ptr;
};

#endif // QCDEEPPTR_H
