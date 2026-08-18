#pragma once
#include <ZEngine/Core/Containers/SPSCQueue.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/ZEngineDef.h>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ZEngine::Helpers
{
    using TaskFn = void (*)(void* ctx);

    // C-style callable — 16 bytes, zero allocation on the hot path.
    struct Task
    {
        void*    Context = nullptr;
        TaskFn   Fn      = nullptr;

        explicit operator bool() const
        {
            return Fn != nullptr;
        }
        void operator()() const
        {
            if (Fn)
                Fn(Context);
        }
    };

    struct ThreadPool
    {
        static constexpr uint32_t MAX_WORKERS          = 16;
        static constexpr uint32_t MAX_TASKS_PER_WORKER = 256;

        size_t                    WorkerCount          = 0;
        size_t                    MaxThreadCount       = 0;

        ThreadPool(size_t max_workers = std::thread::hardware_concurrency())
        {
            max_workers    = (max_workers > 1) ? max_workers - 1 : 1;
            max_workers    = (max_workers < MAX_WORKERS) ? max_workers : MAX_WORKERS;
            MaxThreadCount = max_workers;
            WorkerCount    = max_workers;

            m_cancellation.value.store(false, std::memory_order_relaxed);
            m_active_workers.value.store(static_cast<uint32_t>(WorkerCount), std::memory_order_relaxed);

            for (size_t i = 0; i < WorkerCount; ++i)
                std::thread(&ThreadPool::WorkerRun, this, i).detach();
        }

        ~ThreadPool()
        {
            Shutdown();
        }

        // Zero-alloc hot path — C-style fn-ptr + context.
        void Submit(void* ctx, TaskFn fn)
        {
            if (m_cancellation.value.load(std::memory_order_relaxed))
                return;

            Task     task{ctx, fn};
            uint32_t start = m_cursor.value.fetch_add(1, std::memory_order_relaxed) % static_cast<uint32_t>(WorkerCount);
            for (uint32_t i = 0; i < static_cast<uint32_t>(WorkerCount); ++i)
            {
                uint32_t idx = (start + i) % static_cast<uint32_t>(WorkerCount);
                if (m_workers[idx].queue.push(task))
                {
                    m_workers[idx].cv.notify_one();
                    return;
                }
            }
            // All queues full — execute inline as last resort.
            task();
        }

        void Shutdown()
        {
            m_cancellation.value.store(true, std::memory_order_release);
            for (size_t i = 0; i < WorkerCount; ++i)
                m_workers[i].cv.notify_one();
            // Spin until all workers have exited — ensures Worker::mutex and
            // Worker::cv are not destroyed while a thread is still using them.
            // Workers exit almost immediately after seeing the cancellation token.
            while (m_active_workers.value.load(std::memory_order_acquire) > 0)
                ;
        }

    private:
        struct Worker
        {
            Core::Containers::SPSCQueue<Task, MAX_TASKS_PER_WORKER> queue;
            std::mutex                                              mutex;
            std::condition_variable                                 cv;
        };

        Worker                 m_workers[MAX_WORKERS];
        PaddedAtomic<uint32_t> m_cursor{};
        PaddedAtomic<bool>     m_cancellation{};
        PaddedAtomic<uint32_t> m_active_workers{}; // decremented by each worker on exit

        void                   WorkerRun(size_t idx)
        {
            Worker& w = m_workers[idx];
            while (!m_cancellation.value.load(std::memory_order_acquire))
            {
                Task task;
                while (w.queue.pop(task))
                    task();

                std::unique_lock<std::mutex> lock(w.mutex);
                w.cv.wait(lock, [&] { return !w.queue.empty() || m_cancellation.value.load(std::memory_order_relaxed); });
            }
            m_active_workers.value.fetch_sub(1, std::memory_order_release);
        }
    };

    struct ThreadPoolHelper
    {
        static Scope<ThreadPool> Pool;

        static void              Initialize()
        {
            if (!Pool)
                Pool = CreateScope<ThreadPool>();
        }

        static bool IsInitialized()
        {
            return Pool != nullptr;
        }

        static void Shutdown()
        {
            if (Pool)
            {
                Pool->Shutdown();
                Pool.reset();
            }
        }

        // Zero-alloc — C-style direct.
        static void Submit(void* ctx, TaskFn fn)
        {
            Pool->Submit(ctx, fn);
        }

        // Lambda shim — one heap allocation per lambda call (for captures).
        // Use the C-style overload directly to stay on the zero-alloc path.
        template <typename T>
        static void Submit(T&& f)
        {
            using Fn = std::decay_t<T>;
            auto* p  = new Fn(std::forward<T>(f));
            Pool->Submit(p, [](void* ctx) {
                auto* fn = static_cast<Fn*>(ctx);
                (*fn)();
                delete fn;
            });
        }

    private:
        ThreadPoolHelper()  = delete;
        ~ThreadPoolHelper() = delete;
    };

} // namespace ZEngine::Helpers
