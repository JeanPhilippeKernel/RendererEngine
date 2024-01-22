#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

namespace ZEngine::Helpers
{
    template <typename T>
    class ThreadSafeQueue
    {
    public:
        void enqueue(const T& task)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_queue.push(task);
            m_condition.notify_one();
        }

        bool pop(T& task)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            if (m_queue.empty())
            {
                return false;
            }

            task = std::move(m_queue.front());
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

        void clear()
        {
            std::unique_lock<std::mutex>      lock(m_mutex);
            m_condition.notify_all();
            std::queue<T> empty;
            std::swap(m_queue, empty);
        }

    private:
        mutable std::mutex                m_mutex;
        std::condition_variable           m_condition;
        std::queue<T> m_queue;
    };

} // namespace ZEngine::Helpers
