#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include "ThreadQueue.h"

namespace ZEngine::Helpers
{

    class ThreadPool
    {
    private:
        std::vector<std::thread>           m_workers;
        ThreadQueue<std::function<void()>> m_taskQueue;
        std::atomic<bool>                  m_stopFlag;

        void WorkerThread()
        {
            while (!m_stopFlag)
            {
                auto taskOpt = m_taskQueue.TryPop();
                if (taskOpt.has_value())
                {
                    try
                    {
                        taskOpt.value()();
                    }
                    catch (...)
                    {
                        // Handle exceptions appropriately
                    }
                }
            }
        }

    public:
        ThreadPool(size_t threadCount = std::thread::hardware_concurrency()) : m_stopFlag(false)
        {
            for (size_t i = 0; i < threadCount; ++i)
            {
                m_workers.emplace_back([this] {
                    WorkerThread();
                });
            }
        }

        ~ThreadPool()
        {
            Shutdown();
        }

        void Submit(std::function<void()> task)
        {
            if (!m_stopFlag)
            {
                m_taskQueue.Enqueue(std::move(task));
            }
        }

        void Shutdown()
        {
            m_stopFlag = true;
            m_taskQueue.Reset();
            for (std::thread& worker : m_workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }
        }

        static ThreadPool& Instance()
        {
            static ThreadPool instance;
            return instance;
        }
    };

    class ThreadPoolHelper
    {
    private:
        ThreadPoolHelper()                                   = delete;
        ThreadPoolHelper(const ThreadPoolHelper&)            = delete;
        ThreadPoolHelper& operator=(const ThreadPoolHelper&) = delete;

    public:
        static void Submit(std::function<void()> task)
        {
            ThreadPool::Instance().Submit(std::move(task));
        }
    };

} // namespace ZEngine::Helpers
