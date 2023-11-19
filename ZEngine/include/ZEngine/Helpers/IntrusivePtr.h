#pragma once
#include <atomic>
#include <functional>

namespace ZEngine::Helpers
{
    class RefCounted
    {
    public:
        RefCounted() : m_count(0) {}

        RefCounted(const RefCounted&)            = delete;
        RefCounted& operator=(const RefCounted&) = delete;

        void increment()
        {
            ++m_count;
        }

        void decrement()
        {
            if (--m_count == 0)
            {
                delete this;
            }
        }

        uint64_t RefCount() const
        {
            return m_count;
        }

    private:
        std::atomic<uint64_t> m_count;
    };
    
    template <typename T>
    class IntrusivePtr
    {

    public:
        //  --- Constructors ---  //


        IntrusivePtr(T* ptr = nullptr) : m_ptr(ptr)
        {
            if (m_ptr)
            {
                m_ptr->increment();
            }
        }

        IntrusivePtr(const IntrusivePtr& other) : m_ptr(other.m_ptr)
        {
            if (m_ptr)
            {
                m_ptr->increment();
            }
        }

        IntrusivePtr(IntrusivePtr&& other) noexcept : m_ptr(other.m_ptr)
        {
            other.m_ptr = nullptr;
        }

        //  --- Assignment Operators ---  //

        IntrusivePtr& operator=(const IntrusivePtr& other)
        {
            if (this != &other)
            {
                T* old_ptr = m_ptr;
                m_ptr      = other.m_ptr;
                if (m_ptr)
                {
                    m_ptr->increment();
                }
                if (old_ptr)
                {
                    old_ptr->decrement();
                }
            }
            return *this;
        }

        IntrusivePtr& operator=(IntrusivePtr&& other) noexcept
        {
            if (this != &other)
            {
                T* old_ptr  = m_ptr;
                m_ptr       = other.m_ptr;
                other.m_ptr = nullptr;
                if (old_ptr)
                {
                    old_ptr->decrement();
                }
            }
            return *this;
        }

        IntrusivePtr& operator=(T* rawPtr)
        {
            if (m_ptr != rawPtr)
            {
                T* old_ptr = m_ptr;
                m_ptr      = rawPtr;
                if (m_ptr)
                {
                    m_ptr->increment();
                }
                if (old_ptr)
                {
                    old_ptr->decrement();
                }
            }
            return *this;
        }

        //  --- Modifiers ---  //

        void reset(T* newPtr = nullptr)
        {
            if (m_ptr != newPtr)
            {
                T* old_ptr = m_ptr;
                m_ptr      = newPtr;
                if (m_ptr)
                {
                    m_ptr->increment();
                }
                if (old_ptr)
                {
                    old_ptr->decrement();
                }
            }
        }

        void swap(IntrusivePtr& other) noexcept
        {
            T* temp     = m_ptr;
            m_ptr       = other.m_ptr;
            other.m_ptr = temp;
        }

        //  --- Getter for the underlying pointer ---  //

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

        //  --- Comparison Operators ---  //

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

        bool operator==(const T* rawPtr) const noexcept
        {
            return m_ptr == rawPtr;
        }

        bool operator!=(const T* rawPtr) const noexcept
        {
            return m_ptr != rawPtr;
        }

        bool operator==(const IntrusivePtr& other) const
        {
            return m_ptr == other.m_ptr;
        }

        bool operator!=(const IntrusivePtr& other) const
        {
            return m_ptr != other.m_ptr;
        }

        bool operator<(const IntrusivePtr& other) const
        {
            return m_ptr < other.m_ptr;
        }

        bool operator>(const IntrusivePtr& other) const
        {
            return m_ptr > other.m_ptr;
        }

        bool operator<=(const IntrusivePtr& other) const
        {
            return m_ptr <= other.m_ptr;
        }

        bool operator>=(const IntrusivePtr& other) const
        {
            return m_ptr >= other.m_ptr;
        }

        //  --- Destructor ---  //

        ~IntrusivePtr()
        {
            if (m_ptr)
            {
                m_ptr->decrement();
                m_ptr = nullptr;
            }
        }

    private:
        T* m_ptr = nullptr;
    };

    // Non-member swap function
    template <typename T>
    void swap(IntrusivePtr<T>& lhs, IntrusivePtr<T>& rhs) noexcept
    {
        lhs.swap(rhs);
    }

    template <typename T, typename... Args>
    IntrusivePtr<T> make_intrusive(Args&&... args)
    {
        return IntrusivePtr<T>(new T(std::forward<Args>(args)...));
    }

} // namespace ZEngine::Helpers

// Specialization of std::hash for IntrusivePtr
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
