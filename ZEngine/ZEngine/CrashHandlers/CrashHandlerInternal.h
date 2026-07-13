#pragma once
#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <cstddef>

namespace ZEngine::CrashHandlers
{
    namespace
    {
        constexpr size_t kMaxPathLen         = 512;
        constexpr size_t kMaxNameLen         = 128;
        constexpr size_t kMaxCommentLen      = 1024;
        constexpr int    kCallbackTimeoutSec = 2;

        struct CrashHandlerState
        {
            bool                     Installed                   = false;
            bool                     UserConsentUpload           = false;
            char                     AppName[kMaxNameLen]        = {};
            char                     Version[kMaxNameLen]        = {};
            char                     CrashLogDir[kMaxPathLen]    = {};
            char                     UserComment[kMaxCommentLen] = {};

            void*                    PreCrashCtx                 = nullptr;
            CrashHandler::PreCrashFn PreCrashFn                  = nullptr;
        };

        CrashHandlerState g_state = {};
    } // namespace
} // namespace ZEngine::CrashHandlers