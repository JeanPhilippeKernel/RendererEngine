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

        void attach(T* ptr) noexcept(noexcept(IntrusivePtr<T>(ptr)))
        {
            IntrusivePtr<T> p(ptr);
            T::DecrementRefCount(p.m_ptr);  // reset the count back to original since IntrusivePtr will increment it
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

    class WeakRefCounted;

    class ControlBlock : public RefCounted
    {
    public:
        ControlBlock(WeakRefCounted* weakref) noexcept : m_strong_count(0), m_weakref(weakref) {}

        void StrongAddRef() noexcept
        {
            ++(m_strong_count);
        }

        int StrongRelease() noexcept
        {
            int result = --(m_strong_count);
            if (result == 0)
            {
                m_weakref  = nullptr;
            }
            return result;
        }

        WeakRefCounted* Resolve() noexcept
        {
            int val = m_strong_count.load();
            if (val <= 0)
            {
                return nullptr;
            }
            return m_weakref;
        }

        int RefCount() noexcept
        {
            return m_strong_count.load();
        }

    private:
        std::atomic<int> m_strong_count;
        WeakRefCounted*  m_weakref;
    };

    class WeakRefCounted
    {
        template <typename T>
        friend class IntrusiveWeakPtr;

        WeakRefCounted& operator=(RefCounted const&) = delete;
        WeakRefCounted(RefCounted const&)            = delete;

    public:
        WeakRefCounted() noexcept : m_weakref(IntrusivePtr<ControlBlock>(new ControlBlock(this))) {}

        virtual ~WeakRefCounted() {}

        static void IncrementRefCount(WeakRefCounted* ptr) noexcept
        {
            if (ptr)
            {
                ptr->m_weakref->StrongAddRef();
            }
        }

        static void DecrementRefCount(WeakRefCounted* ptr) noexcept
        {
            if (ptr)
            {
                auto result = ptr->m_weakref->StrongRelease();
                if (result == 0)
                {
                    delete ptr;
                }
            }
        }

    private:
        IntrusivePtr<ControlBlock> m_weakref;
    };

    template <class T>
    class IntrusiveWeakPtr
    {
    public:
        constexpr IntrusiveWeakPtr() noexcept {}
        ~IntrusiveWeakPtr() = default;
        IntrusiveWeakPtr(const IntrusiveWeakPtr& other) noexcept : m_weakref(other.m_weakref) {}

        template <class Y>
        IntrusiveWeakPtr(const IntrusivePtr<Y>& other) noexcept
        {
            auto ptr = other.get();
            if (ptr != nullptr)
            {
                m_weakref = ptr->m_weakref;
            }
        }

        IntrusiveWeakPtr(IntrusiveWeakPtr&& other) noexcept
        {
            std::swap(m_weakref, other.m_weakref);
        }

        template <class Y>
        IntrusiveWeakPtr(IntrusiveWeakPtr<Y>&& other) noexcept
        {
            std::swap(m_weakref, other.m_weakref);
        }

        IntrusiveWeakPtr& operator=(const IntrusiveWeakPtr& other) noexcept
        {
            m_weakref =other.m_weakref;
            return *this;
        }

        template <class Y>
        IntrusiveWeakPtr& operator=(const IntrusiveWeakPtr<Y>& other) noexcept
        {
            m_weakref = other.m_weakref;
            return *this;
        }

        template <class Y>
        IntrusiveWeakPtr& operator=(const IntrusivePtr<Y>& other) noexcept
        {
            m_weakref = other->m_weakref;
            return *this;
        }

        IntrusiveWeakPtr& operator=(IntrusiveWeakPtr&& other) noexcept
        {
            m_weakref = std::move(other.m_weakref);
            return *this;
        }

        template <class Y>
        IntrusiveWeakPtr& operator=(IntrusiveWeakPtr<Y>&& other) noexcept
        {
            m_weakref = std::move(other.m_weakref);
            return *this;
        }

        void reset() noexcept
        {
            m_weakref.reset();
        }

        void swap(IntrusiveWeakPtr& other) noexcept
        {
            std::swap(m_weakref, other.m_weakref);
        }

        bool expired() const noexcept
        {
            return lock() == nullptr;
        }

        IntrusivePtr<T> lock() const noexcept
        {
            if (m_weakref.get() == nullptr)
            {
                return nullptr;
            }
            T* ptr = reinterpret_cast<T*>(m_weakref->Resolve());
            return IntrusivePtr<T>(ptr);
        }

    private:
        IntrusivePtr<ControlBlock> m_weakref;
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
