#pragma once 
#include <ZEngine/Helpers/ThreadPool.h>

namespace Tetragrama::Helpers
{
    struct UIDispatcher
    {
        using Action = std::function<void(void)>;

        static void RunAsync(Action action)
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit(action);
        }
    };
}