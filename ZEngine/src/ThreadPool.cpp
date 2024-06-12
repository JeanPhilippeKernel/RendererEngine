#include <pch.h>
#include <Helpers/ThreadPool.h>

namespace ZEngine::Helpers
{
    std::unique_ptr<ThreadPool> ThreadPoolHelper::m_threadPool = std::make_unique<ThreadPool>();
}