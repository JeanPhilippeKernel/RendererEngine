#pragma once
#include <atomic>
#include <thread>
#include <Helpers/ThreadSafeQueue.h>

namespace ZEngine::Helpers
{

    class ThreadPool
    {
    public:
        ThreadPool(size_t maxThreadCount = std::thread::hardware_concurrency())
            : m_maxThreadCount(maxThreadCount), m_taskQueue(std::make_shared<ThreadSafeQueue<std::function<void()>>>())
        {
            m_maxThreadCount -= m_reservedThreadsCount;
        }

        ~ThreadPool()
        {
            Shutdown();
        }

        void Enqueue(std::function<void()>&& f)
        {
            m_taskQueue->Enqueue(std::move(f));
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_taskQueue->Empty())
            {
                StartWorkerThread();
            }
        }

        void Shutdown()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_taskQueue->Clear();
        }

    private:
        size_t                                                  m_maxThreadCount;
        size_t                                                  m_currentThreadCount{0};
        uint32_t                                                m_reservedThreadsCount{4};
        std::mutex                                              m_mutex;
        std::shared_ptr<ThreadSafeQueue<std::function<void()>>> m_taskQueue;

        static void WorkerThread(std::weak_ptr<ThreadSafeQueue<std::function<void()>>> weakQueue)
        {
            while (auto queue = weakQueue.lock())
            {
                queue->Wait();

                std::function<void()> task;
                if (!queue->Pop(task))
                {
                    continue;
                }
                task();
            }
        }

        void StartWorkerThread()
        {
            if (m_currentThreadCount < m_maxThreadCount)
            {
                std::thread(ThreadPool::WorkerThread, m_taskQueue).detach();
                m_currentThreadCount++;
            }
        }
    };

    struct ThreadPoolHelper
    {
        template <typename T>
        static void Submit(T&& f)
        {
            if (m_threadPool)
            {
                m_threadPool->Enqueue(std::move(f));
            }
        }

        static void Shutdown()
        {
            m_threadPool->Shutdown();
        }

    private:
        ThreadPoolHelper()  = delete;
        ~ThreadPoolHelper() = delete;

        inline static std::unique_ptr<ThreadPool> m_threadPool = std::make_unique<ThreadPool>();;
    };
} // namespace ZEngine::Helpers