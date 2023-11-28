#pragma once
#include <atomic>

namespace ZEngine::Helpers
{
    class RefCounted
    {
    public:
        RefCounted() : m_count(0) {}
        RefCounted(const RefCounted&)
        {
            /* like  = deleted, but aims to support deferenced value swapping*/
        }
        RefCounted& operator=(const RefCounted&)
        {
            /* like = deleted, but aims to support deferenced value swapping*/
            return *this;
        }
        virtual ~RefCounted() = default;

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
                    delete ptr;
                    ptr = nullptr;
                }
            }
        }

        uint64_t RefCount() const
        {
            return m_count.load();
        }

    private:
        std::atomic<uint64_t> m_count;
    };

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

        IntrusivePtr& operator=(const IntrusivePtr& other)
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

        IntrusivePtr& operator=(T* raw_ptr)
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

        void reset(T* newPtr = nullptr)
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

        ~IntrusivePtr()
        {
            T::DecrementRefCount(m_ptr);
            m_ptr = nullptr;
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
