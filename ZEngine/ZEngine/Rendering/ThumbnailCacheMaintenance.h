#pragma once
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <atomic>

namespace ZEngine::Rendering
{
    /// Worker-only, streaming disk maintenance. Never follows symlinks, descends
    /// beyond UUID directories, or removes unrecognized files. PNGs are derived,
    /// regenerable data; source assets are never maintenance targets.
    struct ThumbnailCacheMaintenance
    {
        enum class Resolution : uint8_t
        {
            Unknown,    // transient/importing/unavailable: preserve all current-format keys
            SourceOnly, // registered source known, material dependencies not yet loaded
            Missing,
            Current
        };
        struct CurrentKey
        {
            RenderResourceManager::ThumbnailCacheIdentity Identity         = {};
            bool                                          FullyTransparent = false;
        };
        struct Statistics
        {
            uint64_t Examined       = 0;
            uint64_t Removed        = 0;
            uint64_t ReclaimedBytes = 0;
            uint64_t Errors         = 0;
        };
        using Resolve = Resolution (*)(void*, const uuids::uuid&, CurrentKey*);

        static Statistics Cleanup(const char* project_root, uint32_t schema, uint32_t renderer, Resolve resolve, void* context, const std::atomic<bool>* accepting = nullptr);
    };
} // namespace ZEngine::Rendering
