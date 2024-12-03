#pragma once
#include <IntrusivePtr.h>
#include <ThreadSafeQueue.h>
#include <atomic>
#include <thread>

namespace ZEngine::Helpers
{

    class ThreadPool
    {
    public:
        ThreadPool(size_t maxThreadCount = std::thread::hardware_concurrency()) : m_maxThreadCount(maxThreadCount), m_taskQueue(CreateRef<ThreadSafeQueue<std::function<void()>>>())
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
        size_t                                      m_maxThreadCount;
        size_t                                      m_currentThreadCount{0};
        std::atomic_bool                            m_cancellationToken{false};
        std::mutex                                  m_mutex;
        Ref<ThreadSafeQueue<std::function<void()>>> m_taskQueue;

        static void WorkerThread(WeakRef<ThreadSafeQueue<std::function<void()>>> weakQueue, const std::atomic_bool& cancellationToken)
        {
            while (auto queue = weakQueue.lock())
            {
                queue->Wait(cancellationToken);

                if (cancellationToken == true)
                {
                    break;
                }

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
                    std::thread(ThreadPool::WorkerThread, m_taskQueue.Weak(), std::cref(m_cancellationToken)).detach();
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
            if (!m_threadPool)
            {
                m_threadPool = CreateScope<ThreadPool>();
            }
            m_threadPool->Enqueue(std::move(f));
        }

    private:
        ThreadPoolHelper()  = delete;
        ~ThreadPoolHelper() = delete;

        static Scope<ThreadPool> m_threadPool;
    };
} // namespace ZEngine::Helpers