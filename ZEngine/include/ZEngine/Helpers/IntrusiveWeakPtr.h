#pragma once
#include "IntrusivePtr.h"

namespace ZEngine::Helpers
{
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
