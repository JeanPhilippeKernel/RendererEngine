#pragma once
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Helpers/ThreadSafeQueue.h>
#include <atomic>
#include <thread>

namespace ZEngine::Helpers
{

    struct ThreadPool
    {
        size_t MaxThreadCount      = 0;
        size_t CurrentThreadCount  = 0;
        size_t ReservedThreadCount = 1;

        ThreadPool(size_t maxThreadCount = std::thread::hardware_concurrency()) : MaxThreadCount(maxThreadCount), m_taskQueue(CreateRef<ThreadSafeQueue<std::function<void()>>>())
        {
            MaxThreadCount -= ReservedThreadCount;
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
        std::atomic_bool                            m_cancellationToken{false};
        std::mutex                                  m_mutex;
        Ref<ThreadSafeQueue<std::function<void()>>> m_taskQueue;

        static void                                 WorkerThread(WeakRef<ThreadSafeQueue<std::function<void()>>> weakQueue, const std::atomic_bool& cancellationToken)
        {
            while (auto queue = weakQueue.lock())
            {
                queue->Wait(cancellationToken);

                auto op_canceled = cancellationToken.load(std::memory_order_relaxed);
                if (op_canceled == true)
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
                if (CurrentThreadCount < MaxThreadCount)
                {
                    std::thread(ThreadPool::WorkerThread, m_taskQueue.Weak(), std::cref(m_cancellationToken)).detach();
                    CurrentThreadCount++;
                }
            }
        }
    };

    struct ThreadPoolHelper
    {
        static Scope<ThreadPool> Pool;

        static void              Initialize()
        {
            if (!Pool)
            {
                Pool = CreateScope<ThreadPool>();
            }
        }

        static bool IsInitialized()
        {
            return Pool != nullptr;
        }

        template <typename T>
        static void Submit(T&& f)
        {
            Pool->Enqueue(std::move(f));
        }

    private:
        ThreadPoolHelper()  = delete;
        ~ThreadPoolHelper() = delete;
    };
} // namespace ZEngine::Helpers