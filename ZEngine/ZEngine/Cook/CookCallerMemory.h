#pragma once
#include <ZEngine/Core/VFS/VFSError.h>

namespace ZEngine::Cook
{
    struct SceneCookRequest;
    struct SceneCookContext;
    struct SceneCookResult;
    struct CookDiagnostics;

    // Caller value storage: request/context, the complete source-path span and
    // result/diagnostics. Overlapping value ranges count once. Complete ranges
    // already inside charged native service families or declared VFS facade
    // objects add no bytes. Charge together with all three service profiles
    // before acquiring input roots.
    // Pure, allocation-free; does not infer external heap capacity/overhead or
    // ownership of services referenced by the context.
    [[nodiscard]] Core::VFS::VFSResult<uint64_t> QueryCookCallerMemory(const SceneCookRequest&, const SceneCookContext&, const SceneCookResult*, const CookDiagnostics*) noexcept;
} // namespace ZEngine::Cook
