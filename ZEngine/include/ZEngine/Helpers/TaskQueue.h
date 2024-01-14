#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

namespace ZEngine::Helpers
{
    class TaskQueue
    {
    public:
        void enqueue(const std::function<void()>& task)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_queue.push(task);
            m_condition.notify_one();
        }

        bool dequeue(std::function<void()>& task, const std::atomic<bool>& stop)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this, &stop] {
                return !m_queue.empty() || stop;
            });

            if (stop && m_queue.empty())
            {
                return false; // Return false to indicate no task was dequeued
            }

            task = m_queue.front();
            m_queue.pop();
            return true;
        }

        bool isEmpty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.empty();
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.size();
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_queue.empty())
            {
                m_condition.wait(lock, [this] {
                    return !m_queue.empty();
                });
            }
        }

        void notify_all()
        {
            m_condition.notify_all();
        }

        void clear()
        {
            std::unique_lock<std::mutex>      lock(m_mutex);
            std::queue<std::function<void()>> empty;
            std::swap(m_queue, empty);
        }

    private:
        mutable std::mutex                m_mutex;
        std::condition_variable           m_condition;
        std::queue<std::function<void()>> m_queue;
    };

} // namespace ZEngine::Helpers
