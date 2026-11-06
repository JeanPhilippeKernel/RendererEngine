#pragma once
#include <ZEngine/Cook/CookMemory.h>

namespace ZEngine::Cook
{
    struct SceneCookContext;

    // Registry objects/native arena owners plus complete retained writable
    // backing. A shared root family is counted once; uncommitted address space
    // is reported separately. Callback-target/context storage is not inferred
    // from void pointers; QueryCookCallbackMemory checks that separate contract.
    // Pure setup query. Registry/arena families remain exclusively stable for
    // the synchronous call. Unknown borrowed/routed backing fails closed, never
    // silently counts just array payloads. No allocation, pin or registry change.
    // Inline objects inside a declared callback/VFS family or facade object are
    // covered there: registry, callback and VFS service queries must all succeed
    // and be charged together before use, as the public cooker enforces.
    [[nodiscard]] Core::VFS::VFSResult<CookServiceMemoryRequirements> QueryCookRegistryMemory(const SceneCookContext&) noexcept;
} // namespace ZEngine::Cook
