#include <ZEngine/Helpers/ThreadPool.h>

namespace ZEngine::Helpers
{
    Scope<ThreadPool> ThreadPoolHelper::Pool = CreateScope<ThreadPool>();
}
