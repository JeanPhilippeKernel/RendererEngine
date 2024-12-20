#pragma once
#include <IntrusivePtr.h>
#include <cassert>
#include <shared_mutex>
#include <span>
#include <vector>

namespace ZEngine::Helpers
{
    template <typename T>
    struct Handle;

    template <typename T>
    class HandleManager;

    template <typename T>
    struct Handle : public Helpers::RefCounted
    {
        int Index = -1;

        bool Valid() const
        {
            return Index != -1 && m_counter != -1;
        }

        operator bool() const
        {
            return this->Valid();
        }

    private:
        int m_counter = -1;
        friend class HandleManager<T>;
    };

    template <typename T>
    class HandleManager : public Helpers::RefCounted
    {
        struct ArrayData
        {
            int Counter{-1};
            T   Data;
        };

        int32_t                   m_counter{-1};
        uint32_t                  m_count{0};
        std::vector<ArrayData>    m_data;
        mutable std::shared_mutex m_mutex;

    public:
        HandleManager(uint32_t count = 0) : m_data(count), m_count(count) {}

        T& operator[](const Handle<T>& handle)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            assert(handle.Index < m_count);
            return m_data[handle.Index].Data;
        }

        std::span<ArrayData> Data() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_data;
        }

        std::vector<ArrayData>& Data()
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_data;
        }

        Handle<T> Create()
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            Handle<T>                           handle;
            ArrayData                           data = {.Counter = ++m_counter};

            for (int i = 0; i < m_count; ++i)
            {
                if (m_data[i].Counter == -1)
                {
                    m_data[i]        = data;
                    handle.Index     = i;
                    handle.m_counter = data.Counter;
                    break;
                }
            }

            return handle;
        }

        Handle<T> Add(const T& value)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            Handle<T>                           handle;
            ArrayData                           data = {.Counter = ++m_counter, .Data = value};

            for (int i = 0; i < m_count; ++i)
            {
                if (m_data[i].Counter == -1)
                {
                    m_data[i]        = data;
                    handle.Index     = i;
                    handle.m_counter = data.Counter;
                    break;
                }
            }

            return handle;
        }

        Handle<T> Add(T&& value)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            Handle<T>                           handle;
            ArrayData                           data = {.Counter = ++m_counter, .Data = std::move(value)};

            for (int i = 0; i < m_count; ++i)
            {
                if (m_data[i].Counter == -1)
                {
                    m_data[i]        = data;
                    handle.Index     = i;
                    handle.m_counter = data.Counter;
                    break;
                }
            }

            return handle;
        }

        Handle<T> ConvertToHandle(uint32_t index)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            Handle<T>                           handle{};
            assert(index < m_count);

            ArrayData data   = m_data[index];
            handle.Index     = index;
            handle.m_counter = data.Counter;
            return handle;
        }

        void Update(Handle<T>& handle, T& data)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if ((handle) && (m_data[handle.Index].Counter == handle.m_counter) && (handle.Index < m_count))
            {
                m_data[handle.Index].Data = data;
            }
        }

        void Update(Handle<T>& handle, T&& data)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if ((handle) && (m_data[handle.Index].Counter == handle.m_counter) && (handle.Index < m_count))
            {
                m_data[handle.Index].Data = std::move(data);
            }
        }

        void Remove(Handle<T>& handle)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (!handle)
            {
                return;
            }

            if ((handle.Index < m_count) && (m_data[handle.Index].Counter == handle.m_counter))
            {
                m_data[handle.Index] = ArrayData{};
                handle               = Handle<T>{};
            }
        }

        size_t Size() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_count;
        }

        void Dispose() {}
    };
} // namespace ZEngine::Helpers
