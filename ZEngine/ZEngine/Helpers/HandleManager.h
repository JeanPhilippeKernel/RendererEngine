#pragma once
#include <IntrusivePtr.h>
#include <ZEngineDef.h>
#include <set>
#include <shared_mutex>
#include <span>
#include <vector>

#define INVALID_HANDLE_INDEX -1

namespace ZEngine::Helpers
{
    template <typename T>
    struct Handle;

    template <typename T>
    class HandleManager;

    template <typename T>
    struct Handle : public Helpers::RefCounted
    {
        int  Index = INVALID_HANDLE_INDEX;

        bool Valid() const
        {
            return Index > INVALID_HANDLE_INDEX && m_counter > INVALID_HANDLE_INDEX;
        }

        operator bool() const
        {
            return this->Valid();
        }

    private:
        int m_counter = INVALID_HANDLE_INDEX;
        friend class HandleManager<T>;
    };

    template <typename T>
    class HandleManager : public Helpers::RefCounted
    {
        struct ArrayData
        {
            int Counter{INVALID_HANDLE_INDEX};
            T   Data;
        };

        int32_t                   m_counter{INVALID_HANDLE_INDEX};
        uint32_t                  m_count{0};
        uint32_t                  m_free_indice_head{0};
        std::vector<ArrayData>    m_data;
        std::set<uint32_t>        m_free_indices;
        mutable std::shared_mutex m_mutex;

    public:
        HandleManager(uint32_t count = 0) : m_data(count), m_count(count) {}

        T& operator[](const Handle<T>& handle)
        {
            return Access(handle);
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
            Handle<T>                           handle = {};

            if (m_free_indice_head >= m_count)
            {
                return handle;
            }

            uint32_t index = INVALID_HANDLE_INDEX;
            if (!m_free_indices.empty())
            {
                auto begin = m_free_indices.begin();
                index      = *begin;
                m_free_indices.erase(index);
                ZENGINE_VALIDATE_ASSERT(m_data[index].Counter == INVALID_HANDLE_INDEX, "Counter shouldn't be valid")
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(m_data[m_free_indice_head].Counter == INVALID_HANDLE_INDEX, "Counter shouldn't be valid")
                index = m_free_indice_head++;
            }

            auto& data       = m_data[index];
            data.Counter     = ++m_counter;
            handle.Index     = index;
            handle.m_counter = data.Counter;

            return handle;
        }

        T& Access(const Handle<T>& handle)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            ZENGINE_VALIDATE_ASSERT(handle.Index < m_free_indice_head, "Handle Index is beyond the head")
            return m_data[handle.Index].Data;
        }

        Handle<T> Add(const T& value)
        {
            Handle<T> handle = Create();

            if (handle)
            {
                m_data[handle.Index].Data = value;
            }

            return handle;
        }

        Handle<T> Add(T&& value)
        {
            Handle<T> handle = Create();

            if (handle)
            {
                m_data[handle.Index].Data = std::move(value);
            }

            return handle;
        }

        Handle<T> ToHandle(uint32_t index)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            Handle<T>                           handle{};
            ZENGINE_VALIDATE_ASSERT(index < m_free_indice_head, "Handle Index is beyond the head");

            ArrayData data   = m_data[index];
            handle.Index     = index;
            handle.m_counter = data.Counter;
            return handle;
        }

        void Update(Handle<T>& handle, T& data)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if ((handle) && (m_data[handle.Index].Counter == handle.m_counter) && (handle.Index < m_free_indice_head))
            {
                m_data[handle.Index].Data = data;
            }
        }

        void Update(Handle<T>& handle, T&& data)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if ((handle) && (m_data[handle.Index].Counter == handle.m_counter) && (handle.Index < m_free_indice_head))
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

            if ((handle.Index < m_free_indice_head) && (m_data[handle.Index].Counter == handle.m_counter))
            {
                m_free_indices.insert(handle.Index);
                m_data[handle.Index] = ArrayData{};
                handle               = Handle<T>{};
            }
        }

        size_t Size() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_count;
        }

        uint32_t Head() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_free_indice_head;
        }

        uint32_t Delta() const
        {
            return m_free_indice_head - (uint32_t) m_free_indices.size();
        }

        void Dispose() {}
    };
} // namespace ZEngine::Helpers
