#include <atomic>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#include "ThreadSafeQueue.h"

namespace ZEngine::Helpers
{

    class ThreadPool
    {
    public:
        ThreadPool(size_t maxThreadCount = std::thread::hardware_concurrency())
            : m_maxThreadCount(maxThreadCount), m_taskQueue(std::make_shared<ThreadSafeQueue<std::function<void()>>>())
        {
        }

        ~ThreadPool()
        {
            shutdown();
        }

        void enqueue(std::function<void()>&& f)
        {
            m_taskQueue->enqueue(std::move(f));
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_currentThreadCount < m_maxThreadCount)
            {
                startWorkerThread();
                m_currentThreadCount++;
            }
        }

        void shutdown()
        {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_taskQueue->clear();   
        }

    private:
        std::vector<std::thread>                          m_workers;
        std::shared_ptr<ThreadSafeQueue<std::function<void()>>> m_taskQueue;
        std::mutex                                        m_mutex;
        size_t                                            m_maxThreadCount;
        std::atomic<size_t>                               m_currentThreadCount;

        void startWorkerThread()
        {
            std::weak_ptr<ThreadSafeQueue<std::function<void()>>> weakQueue = m_taskQueue;
            m_workers.emplace_back([weakQueue] {
                while (auto queue = weakQueue.lock())
                {
                    std::function<void()> task;
                    queue->wait();
                    if (queue->isEmpty())
                    {
                        continue;
                    }
                    queue->pop(task);
                    task();
                }
            });
        }
    };

    struct ThreadPoolHelper
    {
        template <typename T>
        static void Submit(T&& f)
        {
            if (m_threadPool)
            {
                m_threadPool->enqueue(std::move(f));
            }
        }

        static void Initialize()
        {
            if (!m_threadPool)
            {
                m_threadPool = std::make_unique<ThreadPool>();
            }
        }

        static void Shutdown()
        {
            m_threadPool->shutdown();
        }

    private:
        ThreadPoolHelper()  = delete;
        ~ThreadPoolHelper() = delete;

        static std::unique_ptr<ThreadPool> m_threadPool;
    };

    std::unique_ptr<ThreadPool> ThreadPoolHelper::m_threadPool = std::make_unique<ThreadPool>();
} // namespace ZEngine::Helpers