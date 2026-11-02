#pragma once
#include <ZEngine/Helpers/ThreadPool.h>
#include <algorithm>

namespace ZEngine::Rendering::Renderers
{
    /// Device-owned, render-thread-only batch executor. Each index always maps
    /// to the same worker (and Vulkan command pool). No general-purpose FIFO:
    /// there is at most one task per worker and Record joins the entire batch.
    /// Background I/O must use ThreadPoolHelper, never this execution domain.
    /// Record and Shutdown must not overlap; stop the render loop before teardown.
    class RenderRecordingWorkers
    {
    public:
        static constexpr uint32_t MaxWorkers = Helpers::ThreadPool::MAX_WORKERS;

        explicit RenderRecordingWorkers(uint32_t count) : m_count(std::clamp(count, 1u, MaxWorkers))
        {
            for (uint32_t i = 0; i < m_count; ++i)
                m_workers[i].Thread = std::thread(&RenderRecordingWorkers::Run, this, i);
        }

        ~RenderRecordingWorkers()
        {
            Shutdown();
        }
        RenderRecordingWorkers(const RenderRecordingWorkers&)            = delete;
        RenderRecordingWorkers& operator=(const RenderRecordingWorkers&) = delete;

        uint32_t                WorkerCount() const
        {
            return m_count;
        }

        /// Tasks are indexed by command-pool owner, not compacted. Empty tasks
        /// are allowed. Caller-owned contexts are no longer used on return.
        void Record(const Helpers::Task* tasks, uint32_t count)
        {
            ZENGINE_VALIDATE_ASSERT(count <= m_count, "Render recording batch exceeds worker capacity")
            if (m_stopped)
            {
                // Shutdown has joined every worker, so command-pool ownership
                // can safely transfer to the caller for sequential fallback.
                for (uint32_t i = 0; i < count; ++i)
                    tasks[i]();
                return;
            }
            {
                std::lock_guard lock(m_completion_mutex);
                m_remaining = 0;
                for (uint32_t i = 0; i < count; ++i)
                    m_remaining += bool(tasks[i]);
            }
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!tasks[i])
                    continue;
                auto&           worker = m_workers[i];
                std::lock_guard lock(worker.Mutex);
                worker.Task = tasks[i];
                worker.CV.notify_one();
            }
            std::unique_lock lock(m_completion_mutex);
            m_completion.wait(lock, [&] { return m_remaining == 0; });
        }

        void Shutdown()
        {
            if (m_stopped)
                return;
            for (uint32_t i = 0; i < m_count; ++i)
            {
                auto&           worker = m_workers[i];
                std::lock_guard lock(worker.Mutex);
                worker.Stopping = true;
                worker.CV.notify_one();
            }
            for (uint32_t i = 0; i < m_count; ++i)
                m_workers[i].Thread.join();
            m_stopped = true;
        }

    private:
        struct Worker
        {
            std::thread             Thread;
            std::mutex              Mutex;
            std::condition_variable CV;
            Helpers::Task           Task;
            bool                    Stopping = false;
        };
        Worker                  m_workers[MaxWorkers];
        uint32_t                m_count;
        bool                    m_stopped = false;
        std::mutex              m_completion_mutex;
        std::condition_variable m_completion;
        uint32_t                m_remaining = 0;

        void                    Run(uint32_t index)
        {
            auto& worker = m_workers[index];
            for (;;)
            {
                Helpers::Task task;
                {
                    std::unique_lock lock(worker.Mutex);
                    worker.CV.wait(lock, [&] { return worker.Stopping || bool(worker.Task); });
                    if (worker.Stopping)
                        return;
                    task        = worker.Task;
                    worker.Task = {};
                }
                task();
                // Notify while holding the mutex. Record acquires this same
                // lock even for an already-complete batch, so no worker can
                // still touch the batch's completion state after it returns.
                std::lock_guard lock(m_completion_mutex);
                if (--m_remaining == 0)
                    m_completion.notify_one();
            }
        }
    };
} // namespace ZEngine::Rendering::Renderers
