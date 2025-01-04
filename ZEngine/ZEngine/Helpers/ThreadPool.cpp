#include <pch.h>
#include <Helpers/ThreadPool.h>

namespace ZEngine::Helpers
{
    Scope<ThreadPool> ThreadPoolHelper::m_threadPool = CreateScope<ThreadPool>();
}