#pragma once
#include <ZEngine/Cook/CookMemory.h>

namespace ZEngine::Cook
{
    struct SceneCookContext;

    // Complete output/scratch caller-facade values and retained native arena
    // families. Shared registry/callback/facade storage is charged once across
    // all three service queries. Pure, bounded and allocation-free; custom
    // contexts without a complete native-storage contract fail before roots.
    // Operational root/provider/mapping domains remain the VFS policy's charge.
    [[nodiscard]] Core::VFS::VFSResult<CookServiceMemoryRequirements> QueryCookVFSServiceMemory(const SceneCookContext&) noexcept;
} // namespace ZEngine::Cook
