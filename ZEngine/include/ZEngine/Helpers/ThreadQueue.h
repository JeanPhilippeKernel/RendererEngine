#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace ZEngine::Helpers
{
    template <typename T>
    class ThreadQueue
    {
    private:
        mutable std::mutex      m_mutex;
        std::queue<T>           m_queue;
        std::condition_variable m_condition;
        bool                    m_terminated = false;

    public:
        ThreadQueue()                              = default;
        ThreadQueue(const ThreadQueue&)            = delete;
        ThreadQueue& operator=(const ThreadQueue&) = delete;

        void Enqueue(T value)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.push(std::move(value));
            }
            m_condition.notify_one();
        }

        std::optional<T> TryPop()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty() || m_terminated)
            {
                return {};
            }
            T value = std::move(m_queue.front());
            m_queue.pop();
            return value;
        }

        void WaitAndPop(T& value)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this] {
                return m_terminated || !m_queue.empty();
            });
            if (m_terminated)
            {
                return;
            }
            value = std::move(m_queue.front());
            m_queue.pop();
        }

        void Wait()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this] {
                return !m_queue.empty() || m_terminated;
            });
        }

        void Wait(const std::chrono::milliseconds& timeout)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait_for(lock, timeout, [this] {
                return !m_queue.empty() || m_terminated;
            });
        }

        bool Empty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.empty();
        }

        void Reset()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_terminated = true;
            m_condition.notify_all();
        }

        void Swap(ThreadQueue& other)
        {
            if (this != other)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                std::swap(m_queue, other.m_queue);
            }
        }
    };

} // namespace ZEngine::Helpers