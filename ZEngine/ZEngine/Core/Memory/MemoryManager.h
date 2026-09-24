#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Profiling/MemoryProfiler.h>

namespace ZEngine::Core::Memory
{

    struct SubArenaConfig
    {
        cstring  Name      = nullptr;
        uint64_t SizeBytes = 0u;
    };

    struct MemoryBudgetConfig
    {
        // Process-lifetime engine/application objects that exist before a subsystem
        // owner is available (window, EngineContext, application state, scheduler).
        SubArenaConfig  Bootstrap        = {};
        SubArenaConfig  AudioEngine      = {};
        SubArenaConfig  AnimationManager = {};
        SubArenaConfig  AssetManager     = {};
        SubArenaConfig  ECSScene         = {};
        SubArenaConfig  Logging          = {};
        SubArenaConfig  VirtualFS        = {};
        SubArenaConfig  VulkanDevice     = {};
        SubArenaConfig  ImportPipeline   = {}; // importers + renderer resource uploads
        SubArenaConfig  UIContext        = {};
        // Editor-only persistent state: editor scene, viewport tools, and panel layer.
        // This remains zero for game and server profiles.
        SubArenaConfig  EditorContext    = {};
        SubArenaConfig  Swapchain        = {};
        SubArenaConfig  ShaderCache      = {};
        SubArenaConfig  Serializer       = {};
        SubArenaConfig  Network          = {};
        SubArenaConfig  Input            = {};

        // Returns the total bytes committed by all SubArenaConfig entries.
        inline uint64_t TotalCommitted() const
        {
            return Bootstrap.SizeBytes + AudioEngine.SizeBytes + AnimationManager.SizeBytes + AssetManager.SizeBytes + ECSScene.SizeBytes + Logging.SizeBytes + VirtualFS.SizeBytes + VulkanDevice.SizeBytes + ImportPipeline.SizeBytes + UIContext.SizeBytes + EditorContext.SizeBytes + Swapchain.SizeBytes + ShaderCache.SizeBytes + Serializer.SizeBytes + Network.SizeBytes + Input.SizeBytes;
        }

        // Validates that the sum of all SizeBytes fields does not exceed total_available_bytes.
        // Returns false and logs the overage if the budget is exceeded.
        inline bool Validate(uint64_t total_available_bytes) const
        {
            const uint64_t committed = TotalCommitted();
            ZENGINE_VALIDATE_ASSERT(committed <= total_available_bytes, "MemoryBudgetConfig::Validate: budget exceeds arena size")
            return committed <= total_available_bytes;
        }

        inline static MemoryBudgetConfig Default()
        {
            MemoryBudgetConfig cfg = {};
            cfg.Bootstrap          = {"Bootstrap", ZMega(32ULL)};
            cfg.AudioEngine        = {"AudioEngine", ZMega(128ULL)};
            cfg.AnimationManager   = {"AnimationManager", ZMega(256ULL)};
            cfg.AssetManager       = {"AssetManager", ZGiga(1ULL)};
            cfg.ECSScene           = {"ECSScene", ZMega(512ULL)};
            cfg.Logging            = {"Logging", ZMega(8ULL)};
            cfg.VirtualFS          = {"VirtualFS", ZMega(64ULL)};
            cfg.VulkanDevice       = {"VulkanDevice", ZGiga(1ULL)};
            // 4 GiB covers the persistent importer arenas plus the bounded
            // per-worker CPU decode slabs used to upload imported resources.
            cfg.ImportPipeline     = {"ImportPipeline", ZGiga(4ULL)};
            cfg.UIContext          = {"UIContext", ZMega(64ULL)};
            cfg.Swapchain          = {"Swapchain", ZMega(8ULL)};
            cfg.ShaderCache        = {"ShaderCache", ZMega(64ULL)};
            cfg.Serializer         = {"Serializer", ZMega(256ULL)};
            cfg.Network            = {"Network", ZMega(64ULL)};
            cfg.Input              = {"Input", ZMega(4ULL)};

            return cfg;
        }

        // Returns a reduced budget for dedicated server builds (no GPU, no audio, no UI).
        inline static MemoryBudgetConfig Server()
        {
            auto cfg                   = Default();
            cfg.AudioEngine.SizeBytes  = 0ull;
            cfg.UIContext.SizeBytes    = 0ull;
            cfg.VulkanDevice.SizeBytes = 0ull;
            cfg.Network.SizeBytes      = 0ull;

            return cfg;
        }

        // Returns a reduced budget for tool / editor builds (no audio, no network).
        inline static MemoryBudgetConfig Editor()
        {
            auto cfg                  = Default();
            cfg.AudioEngine.SizeBytes = 0ull;
            cfg.Network.SizeBytes     = 0ull;
            cfg.UIContext.SizeBytes   = ZMega(128ULL);
            // EditorScene reserves 200 MiB itself. The remaining capacity owns
            // editor objects, panel state, camera state, and transient font work.
            cfg.EditorContext         = {"EditorContext", ZMega(256ULL)};

            return cfg;
        }
    };

    struct MemoryManager
    {
        ArenaAllocator     MainArena      = {};
        // The sole long-lived owner carved automatically during Initialize. It is
        // intentionally small and exists before logging, VFS, or device state.
        ArenaAllocator     BootstrapArena = {};
        MemoryBudgetConfig Budget         = {};

        void               Initialize(uint64_t buffer_size, const MemoryBudgetConfig& config);
        void               CreateBudgetedArena(const SubArenaConfig& config, ArenaAllocator* result);
        void               Shutdown();
    };
} // namespace ZEngine::Core::Memory
