#include <atomic>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#include "TaskQueue.h"

namespace ZEngine::Helpers
{

    class ThreadPool
    {
    public:
        ThreadPool(size_t threadCount = std::thread::hardware_concurrency()) : m_stop(false)
        {
            for (size_t i = 0; i < threadCount; ++i)
            {
                m_workers.emplace_back([this] {
                    this->workerFunction();
                });
            }
        }
        ~ThreadPool()
        {
            shutdown();
        }

        void shutdown()
        {
            m_stop = true;
            m_taskQueue.notify_all();
            for (std::thread& worker : m_workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }
            m_taskQueue.clear();
        }

        template <class F, class... Args>
        auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
        {
            using return_type = decltype(f(args...));

            if (m_stop.load())
            {
                throw std::runtime_error("ThreadPool is stopped, cannot enqueue new tasks.");
            }

            auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

            std::future<return_type> res = task->get_future();
            m_taskQueue.enqueue([task]() {
                (*task)();
            });
            return res;
        }

    private:
        std::vector<std::thread> m_workers;
        TaskQueue                m_taskQueue;
        std::atomic<bool>        m_stop;

        void workerFunction()
        {
            while (!m_stop)
            {
                std::function<void()> task;
                if (m_taskQueue.dequeue(task, m_stop))
                {
                    task();
                }
            }
        }
    };
} // namespace ZEngine::Helpers
