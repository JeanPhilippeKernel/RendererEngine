#pragma once
#include <Helpers/IntrusivePtr.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

namespace ZEngine::Helpers
{
    template <typename T>
    class ThreadSafeQueue : public Helpers::RefCounted
    {
    public:
        void Enqueue(const T& task)
        {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_queue.push(task);
            }
            m_condition.notify_one();
        }

        void Emplace(T&& task)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.emplace(task);
            }
            m_condition.notify_one();
        }

        bool Pop(T& task)
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_queue.empty())
            {
                return false;
            }

            task = std::move(m_queue.front());
            m_queue.pop();
            return true;
        }

        bool Empty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.empty();
        }

        size_t Size() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.size();
        }

        void Wait()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_queue.empty())
            {
                m_condition.wait(lock, [this] {
                    return !m_queue.empty();
                });
            }
        }

        void Clear()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::queue<T>               empty;
            std::swap(m_queue, empty);
            m_condition.notify_all();
        }

    private:
        mutable std::mutex      m_mutex;
        std::condition_variable m_condition;
        std::queue<T>           m_queue;
    };

} // namespace ZEngine::Helpers
