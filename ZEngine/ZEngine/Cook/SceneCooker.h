#pragma once
#include <ZEngine/Cook/CookDiagnostics.h>
#include <ZEngine/Cook/CookDigest.h>
#include <ZEngine/Cook/CookLimits.h>
#include <span>

namespace ZEngine::Core::VFS
{
    struct IVFSContext;
}

namespace ZEngine::ECS
{
    class SceneComponentSchemaRegistry;
    class SceneMigrationRegistry;
} // namespace ZEngine::ECS

namespace ZEngine::Cook
{
    class ICookInputSource;
    class ICookAllocator;
    class CookAdapterRegistry;
    class ICookCallbackStorage;

    struct SceneCookRequest
    {
        std::span<const Core::VFS::VFSPath> Sources      = {};
        Core::VFS::VFSPath                  OutputRoot   = {};
        CookTarget                          Target       = CookTarget::Invalid;
        bool                                ForceRebuild = false;
    };

    struct SceneCookContext
    {
        ICookInputSource*                        Inputs      = nullptr;
        Core::VFS::IVFSContext*                  Output      = nullptr;
        Core::VFS::IVFSContext*                  Scratch     = nullptr;
        Core::VFS::VFSPath                       ScratchRoot = {};
        const ECS::SceneComponentSchemaRegistry* Schemas     = nullptr;
        const ECS::SceneMigrationRegistry*       Migrations  = nullptr;
        const CookAdapterRegistry*               Adapters    = nullptr;
        ICookAllocator*                          Allocator   = nullptr;
        CookLimits                               Limits      = {};
        const ICookCallbackStorage*              Callbacks   = nullptr;
    };

    struct SceneCookResult
    {
        Core::VFS::VFSPath                                                                     ManifestPath               = {};
        Digest256                                                                              ManifestDigest             = {};
        uint32_t                                                                               SceneCount                 = 0;
        uint32_t                                                                               AssetCount                 = 0;
        uint32_t                                                                               ObjectCount                = 0;
        uint64_t                                                                               ReferencedObjectBytes      = 0;
        uint64_t                                                                               PeakWorkingBytes           = 0;
        // Independent category high-water marks, not additive and not serialized.
        Core::Containers::FixedArray<uint64_t, static_cast<size_t>(CookMemoryCategory::Count)> PeakWorkingBytesByCategory = {};
        bool                                                                                   Published                  = false;
    };

    // Synchronous CPU-only cook. Services and caller storage are exclusively
    // stable until return. Inputs belong to one run; cleanup releases their
    // retained storage before the per-call allocator and scratch authority die.
    // No live ECS, raw reimport, GPU, queued work or borrowed result ownership.
    // Failure preserves the caller's previous result and published manifest.
    class SceneCooker
    {
    public:
        [[nodiscard]] static Core::VFS::VFSResult<void> Cook(const SceneCookRequest&, const SceneCookContext&, SceneCookResult* out_result, CookDiagnostics* diagnostics);
    };
} // namespace ZEngine::Cook
