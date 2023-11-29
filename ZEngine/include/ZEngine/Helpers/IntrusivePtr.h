#pragma once
#include <atomic>
#include <concepts>

namespace ZEngine::Helpers
{
    class RefCounted
    {
    protected:
        virtual ~RefCounted() = default;

    public:
        RefCounted() : m_count(0), m_weak_count(0) {}
        RefCounted(const RefCounted&)
        {
            /* like  = deleted, but aims to support deferenced value swapping*/
        }
        RefCounted& operator=(const RefCounted&)
        {
            /* like = deleted, but aims to support deferenced value swapping*/
            return *this;
        }

        static void IncrementRefCount(RefCounted* ptr) noexcept
        {
            if (ptr)
            {
                ++(ptr->m_count);
            }
        }

        static void DecrementRefCount(RefCounted* ptr) noexcept
        {
            if (ptr)
            {
                auto result = --(ptr->m_count);
                if (result == 0)
                {
                    if (ptr->m_weak_count == 0)
                    {
                        delete ptr;
                        ptr = nullptr;
                    }
                }
            }
        }

        static void IncrementWeakRefCount(RefCounted* ptr) noexcept
        {
            if (ptr)
            {
                ++(ptr->m_weak_count);
            }
        }

        static void DecrementWeakRefCount(RefCounted* ptr) noexcept
        {
            if (ptr)
            {
                auto result = --(ptr->m_weak_count);
                if (result == 0 && ptr->m_count == 0)
                {
                    delete ptr;
                }
            }
        }

        static int StrongRefCount(RefCounted* ptr)
        {
            return (ptr) ? ptr->m_count.load() : -1234;
        }

        static int WeakRefCount(RefCounted* ptr)
        {
            return (ptr) ? ptr->m_weak_count.load() : -1234;
        }

        int RefCount() const
        {
            return m_count.load();
        }

    private:
        std::atomic<int> m_count;
        std::atomic<int> m_weak_count;
    };

    template <typename T>
    class IntrusivePtr;

    template <class T>
    class IntrusiveWeakPtr;

    template <typename T>
    class IntrusivePtr
    {
    public:
        IntrusivePtr() noexcept = default;
        IntrusivePtr(std::nullptr_t) noexcept {}
        IntrusivePtr(T* ptr) noexcept(noexcept(T::IncrementRefCount(m_ptr))) : m_ptr(ptr)
        {
            T::IncrementRefCount(m_ptr);
        }

        IntrusivePtr(const IntrusivePtr& other) noexcept(noexcept(T::IncrementRefCount(m_ptr))) : m_ptr(other.m_ptr)
        {
            T::IncrementRefCount(m_ptr);
        }

        IntrusivePtr(IntrusivePtr&& other) noexcept : m_ptr(other.detach()) {}

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusivePtr(const IntrusivePtr<U>& other) noexcept(noexcept(T::IncrementRefCount(m_ptr))) : m_ptr(other.get())
        {
            T::IncrementRefCount(m_ptr);
        }

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusivePtr(IntrusivePtr<U>&& other) noexcept : m_ptr(other.detach())
        {
        }

        IntrusivePtr& operator=(const IntrusivePtr& other) noexcept
        {
            if (this != &other)
            {
                T* old_ptr = m_ptr;
                m_ptr      = other.m_ptr;

                T::IncrementRefCount(m_ptr);
                T::DecrementRefCount(old_ptr);
            }
            return *this;
        }

        IntrusivePtr& operator=(IntrusivePtr&& other) noexcept
        {
            if (this != &other)
            {
                T* old_ptr = m_ptr;
                m_ptr      = other.detach();
                T::DecrementRefCount(old_ptr);
            }
            return *this;
        }

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusivePtr& operator=(const IntrusivePtr<U>& other) noexcept
        {
            IntrusivePtr<T> p(other);
            p.swap(*this);
            return *this;
        }

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusivePtr& operator=(IntrusivePtr<U>&& other) noexcept
        {
            if (this != &other)
            {
                T* old_ptr = m_ptr;
                m_ptr      = other.detach();
                T::DecrementRefCount(old_ptr);
            }
            return *this;
        }

        IntrusivePtr& operator=(T* raw_ptr) noexcept
        {
            if (m_ptr != raw_ptr)
            {
                T* old_ptr = m_ptr;
                m_ptr      = raw_ptr;
                T::IncrementRefCount(m_ptr);
                T::DecrementRefCount(old_ptr);
            }
            return *this;
        }

        void reset(T* newPtr = nullptr) noexcept
        {
            if (m_ptr != newPtr)
            {
                T* old_ptr = m_ptr;
                m_ptr      = newPtr;
                T::IncrementRefCount(m_ptr);
                T::DecrementRefCount(old_ptr);
            }
        }

        void swap(IntrusivePtr& other) noexcept
        {
            T* temp     = m_ptr;
            m_ptr       = other.m_ptr;
            other.m_ptr = temp;
        }

        void swapValue(IntrusivePtr& other) noexcept
        {
            T temp         = *m_ptr;
            *m_ptr         = *(other.m_ptr);
            *(other.m_ptr) = temp;
        }

        T* detach() noexcept
        {
            T* current = m_ptr;
            m_ptr      = nullptr;
            return current;
        }

        void attach(T* ptr) noexcept(noexcept(IntrusivePtr<T>(ptr)))
        {
            IntrusivePtr<T> p(ptr);
            T::DecrementRefCount(p.m_ptr); // reset the count back to original since IntrusivePtr will increment it
            p.swap(*this);
        }

        T* get() const noexcept
        {
            return m_ptr;
        }

        T* operator->() const noexcept
        {
            return m_ptr;
        }

        T& operator*() const noexcept
        {
            return *m_ptr;
        }

        bool operator!() const noexcept
        {
            return !m_ptr;
        }

        explicit operator bool() const noexcept
        {
            return m_ptr != nullptr;
        }

        bool operator==(std::nullptr_t) const noexcept
        {
            return m_ptr == nullptr;
        }

        bool operator!=(std::nullptr_t) const noexcept
        {
            return m_ptr != nullptr;
        }

        bool operator==(const T* raw_ptr) const noexcept
        {
            return m_ptr == raw_ptr;
        }

        bool operator!=(const T* raw_ptr) const noexcept
        {
            return m_ptr != raw_ptr;
        }

        bool operator==(const IntrusivePtr& other) const noexcept
        {
            return m_ptr == other.m_ptr;
        }

        bool operator!=(const IntrusivePtr& other) const noexcept
        {
            return m_ptr != other.m_ptr;
        }

        bool operator<(const IntrusivePtr& other) const noexcept
        {
            return m_ptr < other.m_ptr;
        }

        bool operator>(const IntrusivePtr& other) const noexcept
        {
            return m_ptr > other.m_ptr;
        }

        bool operator<=(const IntrusivePtr& other) const noexcept
        {
            return m_ptr <= other.m_ptr;
        }

        bool operator>=(const IntrusivePtr& other) const noexcept
        {
            return m_ptr >= other.m_ptr;
        }

        IntrusiveWeakPtr<T> Weak() const noexcept
        {
            IntrusiveWeakPtr<T> weak_ptr;
            weak_ptr.m_ptr = m_ptr;
            T::IncrementWeakRefCount(m_ptr);
            return weak_ptr;
        }

        long count() const noexcept
        {
            return T::StrongRefCount(m_ptr);
        }

        ~IntrusivePtr()
        {
            T::DecrementRefCount(m_ptr);
        }

    private:
        T* m_ptr = nullptr;
    };

    template <typename T>
    void swap(IntrusivePtr<T>& lhs, IntrusivePtr<T>& rhs) noexcept
    {
        lhs.swap(rhs);
    }

    template <typename T>
    void swapValue(IntrusivePtr<T>& lhs, IntrusivePtr<T>& rhs) noexcept
    {
        lhs.swapValue(rhs);
    }

    template <typename T, typename... Args>
    IntrusivePtr<T> make_intrusive(Args&&... args)
    {
        return IntrusivePtr<T>(new T(std::forward<Args>(args)...));
    }

    template <class T>
    class IntrusiveWeakPtr
    {
        friend class IntrusivePtr<T>;

    public:
        IntrusiveWeakPtr() noexcept = default;
        IntrusiveWeakPtr(std::nullptr_t) noexcept {}
        IntrusiveWeakPtr(const IntrusiveWeakPtr& other) noexcept(noexcept(T::IncrementWeakRefCount(m_ptr))) : m_ptr(other.m_ptr)
        {
            T::IncrementWeakRefCount(m_ptr);
        }
        IntrusiveWeakPtr(const IntrusivePtr<T>& other) noexcept
        {
            IntrusiveWeakPtr<T> weak_ptr = other.Weak();
            weak_ptr.swap(*this);
        }
        ~IntrusiveWeakPtr()
        {
            T::DecrementWeakRefCount(m_ptr);
        }

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusiveWeakPtr(const IntrusivePtr<U>& other) noexcept
        {
            IntrusiveWeakPtr<T> weak_ptr = other.Weak();
            weak_ptr.swap(*this);
        }

        IntrusiveWeakPtr(IntrusiveWeakPtr&& other) noexcept
        {
            std::swap(m_ptr, other.m_ptr);
        }

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusiveWeakPtr(IntrusiveWeakPtr<U>&& other) noexcept
        {
            std::swap(m_ptr, other.m_ptr);
        }

        IntrusiveWeakPtr& operator=(const IntrusiveWeakPtr& other) noexcept
        {
            if (this != &other)
            {
                T::DecrementWeakRefCount(m_ptr);
                m_ptr = other.m_ptr;
                T::IncrementWeakRefCount(m_ptr);
            }
            return *this;
        }

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusiveWeakPtr& operator=(const IntrusiveWeakPtr<U>& other) noexcept
        {
            if (this != &other)
            {
                T::DecrementWeakRefCount(m_ptr);
                m_ptr = other.m_ptr;
                T::IncrementWeakRefCount(m_ptr);
            }
            return *this;
        }

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusiveWeakPtr& operator=(const IntrusivePtr<U>& other) noexcept
        {
            IntrusiveWeakPtr weak_ptr = other.Weak();
            weak_ptr.swap(*this);
            return *this;
        }

        IntrusiveWeakPtr& operator=(IntrusiveWeakPtr&& other) noexcept
        {
            other.swap(*this);
            return *this;
        }

        template <class U, typename = std::enable_if_t<std::convertible_to<U*, T*>>>
        IntrusiveWeakPtr& operator=(IntrusiveWeakPtr<U>&& other) noexcept
        {
            other.swap(*this);
            return *this;
        }

        void reset() noexcept
        {
            IntrusiveWeakPtr().swap(*this);
        }

        void swap(IntrusiveWeakPtr& other) noexcept
        {
            std::swap(m_ptr, other.m_ptr);
        }

        bool expired() const noexcept
        {
            return lock() == nullptr;
        }

        IntrusivePtr<T> lock() const noexcept
        {
            return (T::StrongRefCount(m_ptr) > 0) ? IntrusivePtr<T>(m_ptr) : IntrusivePtr<T>();
        }

    private:
        T* m_ptr = nullptr;
    };

} // namespace ZEngine::Helpers

namespace std
{

    template <typename T>
    struct hash<ZEngine::Helpers::IntrusivePtr<T>>
    {
        size_t operator()(const ZEngine::Helpers::IntrusivePtr<T>& ptr) const noexcept
        {
            return std::hash<T*>{}(ptr.get());
        }
    };

} // namespace std
