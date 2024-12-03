#pragma once
#include <span>
#include <vector>
#include <IntrusivePtr.h>

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
            T   Data{nullptr};
        };

        int32_t                m_counter{-1};
        uint32_t               m_count{0};
        uint32_t               m_free_slot_index{0};
        std::vector<ArrayData> m_data;

    public:
        HandleManager(uint32_t count = 0) : m_data(count), m_count(count) {}

        T& operator[](const Handle<T>& handle)
        {
            assert(handle.Index < m_count);
            return m_data[handle.Index].Data;
        }

        std::span<ArrayData> Data() const
        {
            return m_data;
        }

        std::vector<ArrayData>& Data()
        {
            return m_data;
        }

        Handle<T> Create()
        {
            Handle<T> handle;
            ArrayData data = {.Counter = ++m_counter};

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

        Handle<T> Add(const T& texture)
        {
            Handle<T> handle;
            ArrayData data = {.Counter = ++m_counter, .Data = texture};

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

        Handle<T> Add(T&& texture)
        {
            Handle<T> handle;
            ArrayData data = {.Counter = ++m_counter, .Data = std::move(texture)};

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

        void Update(Handle<T>& handle, T& data) 
        {
            if ((handle) && (m_data[handle.Index].Counter == handle.m_counter) && (handle.Index < m_count))
            {
                m_data[handle.Index].Data = data;
            }
        }

        void Update(Handle<T>& handle, T&& data)
        {
            if ((handle) && (m_data[handle.Index].Counter == handle.m_counter) && (handle.Index < m_count))
            {
                m_data[handle.Index].Data = std::move(data);
            }
        }

        void Remove(Handle<T>& handle)
        {
            if (!handle)
            {
                return;
            }

            if ((handle.Index < m_count) && (m_data[handle].Counter == handle.m_counter))
            {
                m_data[handle] = ArrayData{};
                handle         = Handle<T>{};
            }
        }

        size_t Size() const
        {
            return m_count;
        }

        int GetUsedSlotCount() const
        {
            return m_free_slot_index;
        }

        void Dispose()
        {
        }
    };
} // namespace ZEngine::Helpers
