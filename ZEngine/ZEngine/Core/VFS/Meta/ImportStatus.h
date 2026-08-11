#pragma once
#include <cstdint>

namespace ZEngine::Core::VFS
{
    enum class ImportStatus : uint8_t
    {
        Unknown  = 0,
        UpToDate = 1,
        Stale    = 2,
        New      = 3,
    };
} // namespace ZEngine::Core::VFS
