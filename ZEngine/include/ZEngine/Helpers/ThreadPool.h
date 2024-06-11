#pragma once
#include <Helpers/ThreadSafeQueue.h>
#include <atomic>
#include <thread>


namespace ZEngine::Helpers
{

    class ThreadPool
    {
    public:
        ThreadPool(size_t maxThreadCount = std::thread::hardware_concurrency())
            : m_maxThreadCount(maxThreadCount), m_taskQueue(std::make_shared<ThreadSafeQueue<std::function<void()>>>()), m_shouldStop(false)
        {
        }

        ~ThreadPool()
        {
            Shutdown();
        }

        void Enqueue(std::function<void()>&& f)
        {
            m_taskQueue->Enqueue(std::move(f));
            std::unique_lock<std::mutex> m_lock(m_mutex);
            if (!m_taskQueue->Empty() && m_currentThreadCount < m_maxThreadCount)
            {
                m_lock.unlock();
                StartWorkerThread();
            }
        }

        void Shutdown()
        {
            {
                std::unique_lock<std::mutex> m_lock(m_mutex);
                m_shouldStop = true;
            }
            m_taskQueue->Clear();
        }

    private:
        size_t m_maxThreadCount;
        std::atomic<size_t> m_currentThreadCount{0};
        std::mutex m_mutex;
        std::shared_ptr<ThreadSafeQueue<std::function<void()>>> m_taskQueue;
        std::atomic<bool> m_shouldStop;

        void WorkerThread()
        {
            while (!m_shouldStop)
            {
                m_taskQueue->Wait();
                if (m_shouldStop)
                    break;

                std::function<void()> task;
                if (m_taskQueue->Pop(task))
                {
                    task();
                }
            }
            m_currentThreadCount--;
        }

        void StartWorkerThread()
        {
            std::thread(&ThreadPool::WorkerThread, this).detach();
            m_currentThreadCount++;
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

    private:
        ThreadPoolHelper() = delete;
        ~ThreadPoolHelper() = delete;

        inline static std::unique_ptr<ThreadPool> m_threadPool = std::make_unique<ThreadPool>();
    };
} // namespace ZEngine::Helpers