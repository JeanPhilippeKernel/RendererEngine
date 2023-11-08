#pragma once
#include <cstdint>
#include <functional>

namespace ZEngine::Helpers
{
    class RefCounted
    {
    public:
        RefCounted() : count_(0) {}

        void increment()
        {
            ++count_;
        }

        void decrement()
        {
            if (--count_ == 0)
            {
                delete this;
            }
        }

        uint64_t ref_count() const
        {
            return count_;
        }

    private:
        uint64_t count_;
    };

    template <typename T>
    class IntrusivePtr
    {

    public:
        //  --- Constructors ---  //

        constexpr IntrusivePtr() noexcept = default;

        IntrusivePtr(T* ptr = nullptr) : ptr_(ptr)
        {
            if (ptr_)
            {
                ptr_->increment();
            }
        }

        IntrusivePtr(const IntrusivePtr& other) : ptr_(other.ptr_)
        {
            if (ptr_)
            {
                ptr_->increment();
            }
        }

        IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_)
        {
            other.ptr_ = nullptr;
        }

        //  --- Assignment Operators ---  //

        IntrusivePtr& operator=(const IntrusivePtr& other)
        {
            if (this != &other)
            {
                T* old_ptr = ptr_;
                ptr_ = other.ptr_;
                if (ptr_)
                {
                    ptr_->increment();
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
                T* old_ptr = ptr_;
                ptr_ = other.ptr_;
                other.ptr_ = nullptr;
                if (old_ptr)
                {
                    old_ptr->decrement();
                }
            }
            return *this;
        }

        IntrusivePtr& operator=(T* rawPtr)
        {
            if (ptr_ != rawPtr)
            {
                T* old_ptr = ptr_;
                ptr_ = rawPtr;
                if (ptr_)
                {
                    ptr_->increment();
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
            if (ptr_ != newPtr)
            {
                T* old_ptr = ptr_;
                ptr_ = newPtr;
                if (ptr_)
                {
                    ptr_->increment();
                }
                if (old_ptr)
                {
                    old_ptr->decrement();
                }
            }
        }

        void swap(IntrusivePtr& other) noexcept
        {
            T* temp = ptr_;
            ptr_ = other.ptr_;
            other.ptr_ = temp;
        }

        //  --- Getter for the underlying pointer ---  //

        T* get() const noexcept
        {
            return ptr_;
        }

        T* operator->() const noexcept
        {
            return ptr_;
        }

        T& operator*() const noexcept
        {
            return *ptr_;
        }

        //  --- Comparison Operators ---  //

        bool operator!() const noexcept
        {
            return !ptr_;
        }

        explicit operator bool() const noexcept
        {
            return ptr_ != nullptr;
        }

        bool operator==(std::nullptr_t) const noexcept
        {
            return ptr_ == nullptr;
        }

        bool operator!=(std::nullptr_t) const noexcept
        {
            return ptr_ != nullptr;
        }

        bool operator==(const T* rawPtr) const noexcept
        {
            return ptr_ == rawPtr;
        }

        bool operator!=(const T* rawPtr) const noexcept
        {
            return ptr_ != rawPtr
        }

        bool operator==(const IntrusivePtr& other) const
        {
            return ptr_ == other.ptr_;
        }

        bool operator!=(const IntrusivePtr& other) const
        {
            return ptr_ != other.ptr_;
        }

        bool operator<(const IntrusivePtr& other) const
        {
            return ptr_ < other.ptr_;
        }

        bool operator>(const IntrusivePtr& other) const
        {
            return ptr_ > other.ptr_;
        }

        bool operator<=(const IntrusivePtr& other) const
        {
            return ptr_ <= other.ptr_;
        }

        bool operator>=(const IntrusivePtr& other) const
        {
            return ptr_ >= other.ptr_;
        }

        bool operator==(std::nullptr_t) const
        {
            return ptr_ == nullptr;
        }

        bool operator!=(std::nullptr_t) const
        {
            return ptr_ != nullptr;
        }

        //  --- Destructor ---  //

        ~IntrusivePtr()
        {
            if (ptr_)
            {
                ptr_->decrement()
            }
        }

    private:
        T* ptr_ = nullptr;
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
