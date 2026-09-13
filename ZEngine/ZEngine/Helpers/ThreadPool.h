#pragma once
#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/ZEngineDef.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ZEngine::Helpers
{
    using TaskFn = void (*)(void* ctx);

    /// @brief Allocation-free C-style task and its caller-owned context.
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

    /// @brief Thread-local worker slab set by the resource manager at worker startup.
    /// @details It is null outside initialized worker threads. A slab is exclusively
    /// owned by its worker, so cross-thread frees are not permitted.
    inline thread_local Core::Memory::TLSFSlab* t_worker_slab = nullptr;

    /// @brief Set the calling thread's worker slab. Call once per worker at task-loop start.
    /// @param slab Pointer to the worker's TLSFSlab, or nullptr to clear.
    inline void                                 SetWorkerSlab(Core::Memory::TLSFSlab* slab)
    {
        t_worker_slab = slab;
    }

    /// @brief Get the calling thread's worker slab.
    /// @returns The slab set by SetWorkerSlab(), or nullptr if not set.
    inline Core::Memory::TLSFSlab* GetWorkerSlab()
    {
        return t_worker_slab;
    }

    /// @brief C-style initialization callback invoked once per registration on each worker.
    using WorkerInitFn = void (*)(void* ctx, size_t worker_idx);

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

        /// @brief Submit a C-style task without allocating a closure.
        /// @return False when the pool is shutting down and the task was not run.
        bool Submit(void* ctx, TaskFn fn)
        {
            if (!fn || m_cancellation.value.load(std::memory_order_relaxed))
                return false;

            Task     task{ctx, fn};
            uint32_t start = m_cursor.value.fetch_add(1, std::memory_order_relaxed) % static_cast<uint32_t>(WorkerCount);
            for (uint32_t i = 0; i < static_cast<uint32_t>(WorkerCount); ++i)
            {
                uint32_t idx = (start + i) % static_cast<uint32_t>(WorkerCount);
                if (m_workers[idx].queue.push(task))
                {
                    NotifyWorker(m_workers[idx]);
                    return true;
                }
            }
            // All queues full — execute inline as last resort.
            task();
            return true;
        }

        /// @brief Submits a task to one specific worker without an inline fallback.
        /// @details Render-graph command recording uses this to preserve command-pool
        ///          ownership. The render thread is its only caller.
        /// @return False when the worker is invalid, its queue is full, or shutdown began.
        bool SubmitToWorker(uint32_t worker_index, void* ctx, TaskFn fn)
        {
            if (!fn || worker_index >= WorkerCount || m_cancellation.value.load(std::memory_order_relaxed))
                return false;

            Worker& worker = m_workers[worker_index];
            if (!worker.queue.push({ctx, fn}))
                return false;
            NotifyWorker(worker);
            return true;
        }

        /// @brief Registers a per-worker initialization callback.
        /// @details Each worker invokes `fn(ctx, worker_index)` once for this registration.
        void RegisterWorkerInit(WorkerInitFn fn, void* ctx)
        {
            m_init_ctx.value.store(ctx, std::memory_order_relaxed);
            m_init_fn.value.store(fn, std::memory_order_release); // release: ctx visible after fn
            m_init_generation.value.fetch_add(1, std::memory_order_release);
            for (size_t i = 0; i < WorkerCount; ++i)
                NotifyWorker(m_workers[i]);
        }

        /// @brief Stops workers after their current task and waits for their exit.
        void Shutdown()
        {
            m_init_fn.value.store(nullptr, std::memory_order_relaxed);
            m_init_generation.value.fetch_add(1, std::memory_order_release);
            m_cancellation.value.store(true, std::memory_order_release);
            for (size_t i = 0; i < WorkerCount; ++i)
                NotifyWorker(m_workers[i]);
            while (m_active_workers.value.load(std::memory_order_acquire) > 0)
                std::this_thread::yield();
        }

    private:
        struct Worker
        {
            // Submit() may be called from the main thread or another worker, while
            // WorkerRun() is this queue's sole consumer.
            Core::Containers::MPSCQueue<Task, MAX_TASKS_PER_WORKER> queue;
            std::mutex                                              mutex;
            std::condition_variable                                 cv;
        };

        Worker                     m_workers[MAX_WORKERS];
        PaddedAtomic<uint32_t>     m_cursor{};
        PaddedAtomic<bool>         m_cancellation{};
        PaddedAtomic<uint32_t>     m_active_workers{};  // decremented by each worker on exit
        PaddedAtomic<WorkerInitFn> m_init_fn{};         // per-worker init callback; set by RegisterWorkerInit
        PaddedAtomic<void*>        m_init_ctx{};        // context passed to m_init_fn
        PaddedAtomic<uint32_t>     m_init_generation{}; // changes on every registration, including clear

        // Serialize a producer's notification with the worker's transition from
        // checking the queue to waiting. Without this mutex, a task can be
        // published after the wait predicate observes an empty queue but before
        // the worker blocks, losing the notification indefinitely.
        static void                NotifyWorker(Worker& worker)
        {
            std::lock_guard<std::mutex> lock(worker.mutex);
            worker.cv.notify_one();
        }

        void WorkerRun(size_t idx)
        {
            // Re-checked every time this worker reaches the top of its loop, not just
            // once before entering it — RegisterWorkerInit is normally called after the
            // pool's workers are already running and idle-waiting in cv.wait below;
            // notify_one wakes the wait but does NOT resume execution back at a one-time
            // pre-loop check, so a "run once before the loop" version of this never
            // actually ran the callback on any real worker. The registration generation
            // prevents a plain task-arrival wake from re-running the callback.
            uint32_t last_init_generation = 0;
            auto     run_init_if_new      = [&] {
                uint32_t generation = m_init_generation.value.load(std::memory_order_acquire);
                if (generation == last_init_generation)
                    return;
                WorkerInitFn init = m_init_fn.value.load(std::memory_order_relaxed);
                if (init)
                    init(m_init_ctx.value.load(std::memory_order_relaxed), idx);
                last_init_generation = generation;
            };

            Worker& w = m_workers[idx];
            while (!m_cancellation.value.load(std::memory_order_acquire))
            {
                run_init_if_new();

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

        /// @brief Submit a C-style task without allocating a closure.
        /// @return False when the pool has not been initialized or is shutting down.
        static bool Submit(void* ctx, TaskFn fn)
        {
            return Pool && Pool->Submit(ctx, fn);
        }

        /// @brief Submits a render-thread task to a specific worker.
        /// @return False when the pool is unavailable or that worker cannot accept work.
        static bool SubmitToWorker(uint32_t worker_index, void* ctx, TaskFn fn)
        {
            return Pool && Pool->SubmitToWorker(worker_index, ctx, fn);
        }

    private:
        ThreadPoolHelper()  = delete;
        ~ThreadPoolHelper() = delete;
    };

} // namespace ZEngine::Helpers
