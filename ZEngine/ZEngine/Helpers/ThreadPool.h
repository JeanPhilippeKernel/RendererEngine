#pragma once
#include <ZEngine/Core/Containers/SPSCQueue.h>
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

    // Per-worker TLSFSlab pointer — set by RRM at startup via SetWorkerSlab().
    // Worker tasks call GetWorkerSlab() to obtain their exclusive allocation slab.
    // nullptr on the main thread and on any thread where SetWorkerSlab was not called.
    // Thread safety: each worker owns its slab exclusively — no locking required.
    // Cross-thread Free is a data race — see issue #690.
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

    // Called once per worker thread at the start of its task loop.
    // Signature: fn(ctx, worker_idx)
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

        /// @brief Register a per-worker init callback.
        ///        Called exactly once per worker at the start of its task loop.
        ///        If called after workers have started (the typical case — RRM
        ///        initialises after the pool is constructed), all idle workers
        ///        are woken via notify_one so they pick up the callback immediately.
        ///        The callback runs before any queued tasks on each worker thread.
        /// @param fn  Callback — fn(ctx, worker_idx). Must be thread-safe.
        /// @param ctx Caller context forwarded to fn.
        void InitClosureSlab(Core::Memory::ArenaAllocator* arena, size_t bytes)
        {
            m_closure_slab.Init(arena, bytes);
        }

        Core::Memory::TLSFSlab* GetClosureSlab()
        {
            return m_closure_slab.Pool ? &m_closure_slab : nullptr;
        }

        void RegisterWorkerInit(WorkerInitFn fn, void* ctx)
        {
            m_init_ctx.value.store(ctx, std::memory_order_relaxed);
            m_init_fn.value.store(fn, std::memory_order_release); // release: ctx visible after fn
            for (size_t i = 0; i < WorkerCount; ++i)
                m_workers[i].cv.notify_one();
        }

        void Shutdown()
        {
            m_init_fn.value.store(nullptr, std::memory_order_relaxed);
            m_cancellation.value.store(true, std::memory_order_release);
            for (size_t i = 0; i < WorkerCount; ++i)
                m_workers[i].cv.notify_one();
            while (m_active_workers.value.load(std::memory_order_acquire) > 0)
                std::this_thread::yield();
            m_closure_slab.Shutdown();
        }

    private:
        struct Worker
        {
            Core::Containers::SPSCQueue<Task, MAX_TASKS_PER_WORKER> queue;
            std::mutex                                              mutex;
            std::condition_variable                                 cv;
        };

        Core::Memory::TLSFSlab     m_closure_slab{};
        Worker                     m_workers[MAX_WORKERS];
        PaddedAtomic<uint32_t>     m_cursor{};
        PaddedAtomic<bool>         m_cancellation{};
        PaddedAtomic<uint32_t>     m_active_workers{}; // decremented by each worker on exit
        PaddedAtomic<WorkerInitFn> m_init_fn{};        // per-worker init callback; set by RegisterWorkerInit
        PaddedAtomic<void*>        m_init_ctx{};       // context passed to m_init_fn

        void                       WorkerRun(size_t idx)
        {
            // Call per-worker init callback if registered (e.g. set t_worker_slab).
            // Runs before the first task — guaranteed by RegisterWorkerInit waking the worker.
            WorkerInitFn init = m_init_fn.value.load(std::memory_order_acquire);
            if (init)
                init(m_init_ctx.value.load(std::memory_order_relaxed), idx);

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

        template <typename T>
        static void Submit(T&& f)
        {
            using Fn = std::decay_t<T>;

            static constexpr size_t fn_offset  = (sizeof(Core::Memory::TLSFSlab*) + alignof(Fn) - 1) & ~(alignof(Fn) - 1);
            static constexpr size_t block_size = fn_offset + sizeof(Fn);

            Core::Memory::TLSFSlab* slab  = Pool ? Pool->GetClosureSlab() : nullptr;
            uint8_t*                block = slab ? static_cast<uint8_t*>(slab->Alloc(block_size)) : static_cast<uint8_t*>(::operator new(block_size));

            *reinterpret_cast<Core::Memory::TLSFSlab**>(block) = slab;
            new (block + fn_offset) Fn(std::forward<T>(f));

            Pool->Submit(block, [](void* ctx) {
                uint8_t*                raw  = static_cast<uint8_t*>(ctx);
                Core::Memory::TLSFSlab* slab = *reinterpret_cast<Core::Memory::TLSFSlab**>(raw);
                auto*                   fn   = reinterpret_cast<Fn*>(raw + fn_offset);
                (*fn)();
                fn->~Fn();
                if (slab)
                    slab->Free(raw);
                else
                    ::operator delete(raw);
            });
        }

    private:
        ThreadPoolHelper()  = delete;
        ~ThreadPoolHelper() = delete;
    };

} // namespace ZEngine::Helpers
