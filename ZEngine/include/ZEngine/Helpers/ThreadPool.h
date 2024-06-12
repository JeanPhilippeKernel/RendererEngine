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
        }

        ~ThreadPool()
        {
            Shutdown();
        }

        void Enqueue(std::function<void()>&& f)
        {
            m_taskQueue->Emplace(std::forward<std::function<void()>>(f));
            if (!m_taskQueue->Empty())
            {
                StartWorkerThread();
            }
        }

        void Shutdown()
        {
            m_cancellationToken.exchange(true);
            m_taskQueue->Clear();
        }

    private:
        size_t                                                  m_maxThreadCount;
        size_t                                                  m_currentThreadCount{0};
        std::atomic_bool                                        m_cancellationToken{false};
        std::mutex                                              m_mutex;
        std::shared_ptr<ThreadSafeQueue<std::function<void()>>> m_taskQueue;

        static void WorkerThread(std::weak_ptr<ThreadSafeQueue<std::function<void()>>> weakQueue, const std::atomic_bool& cancellationToken)
        {
            while (auto queue = weakQueue.lock())
            {
                if (cancellationToken.load() == true)
                {
                    break;
                }

                queue->WaitFor();

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
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (m_currentThreadCount < m_maxThreadCount)
                {
                    std::thread(ThreadPool::WorkerThread, m_taskQueue, std::cref(m_cancellationToken)).detach();
                    m_currentThreadCount++;
                }
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

    private:
        ThreadPoolHelper()  = delete;
        ~ThreadPoolHelper() = delete;

        inline static std::unique_ptr<ThreadPool> m_threadPool = std::make_unique<ThreadPool>();
    };
} // namespace ZEngine::Helpers