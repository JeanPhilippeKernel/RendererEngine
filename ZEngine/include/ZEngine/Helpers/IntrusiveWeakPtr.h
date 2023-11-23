#pragma once
#include "IntrusivePtr.h"

namespace ZEngine::Helpers
{
    class WeakRefCounted;

    class ControlBlock : public RefCounted
    {
    public:
        ControlBlock(WeakRefCounted* weakref) noexcept : m_strong_count(0), m_weakref(weakref) {}

        void strong_add_ref() noexcept
        {
            ++(m_strong_count);
        }

        int strong_release() noexcept
        {
            int result = --(m_strong_count);
            if (result == 0)
            {
                m_weakref  = nullptr;
            }
            return result;
        }

        WeakRefCounted* resolve() noexcept
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

        IntrusivePtr<WeakRefCounted> intrusive_lifetime() noexcept
        {
            return IntrusivePtr<WeakRefCounted>(this);
        }

        void increment() noexcept
        {
            m_weakref->strong_add_ref();
        }

        void decrement() noexcept
        {
            auto result = m_weakref->strong_release();
            if (result == 0)
            {
                delete this;
            }
        }

        int test_ref_count() noexcept
        {
            return m_weakref->RefCount();
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

        bool is_expired() const noexcept
        {
            return lock() == nullptr;
        }

        IntrusivePtr<T> lock() const noexcept
        {
            if (m_weakref.get() == nullptr)
            {
                return nullptr;
            }
            T* ptr = static_cast<T*>(m_weakref->resolve());
            return IntrusivePtr<T>(ptr);
        }

        long use_count() const noexcept {
            return m_weakref->RefCount();
        }

    private:
        IntrusivePtr<ControlBlock> m_weakref;
    };

} // namespace ZEngine::Helpers
