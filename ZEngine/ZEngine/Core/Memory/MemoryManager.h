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
        // Two independent scene-load slots allow a serializer worker to prepare a
        // replacement scene while the active deserialized scene remains readable.
        SubArenaConfig  EditorSceneLoadA = {};
        SubArenaConfig  EditorSceneLoadB = {};
        SubArenaConfig  Swapchain        = {};
        SubArenaConfig  ShaderCache      = {};
        SubArenaConfig  Serializer       = {};
        SubArenaConfig  Network          = {};
        SubArenaConfig  Input            = {};

        // Returns the total virtual capacity reserved by all SubArenaConfig entries.
        // Physical pages are committed lazily when an arena allocates from them.
        inline uint64_t TotalCapacity() const
        {
            return Bootstrap.SizeBytes + AudioEngine.SizeBytes + AnimationManager.SizeBytes + AssetManager.SizeBytes + ECSScene.SizeBytes + Logging.SizeBytes + VirtualFS.SizeBytes + VulkanDevice.SizeBytes + ImportPipeline.SizeBytes + UIContext.SizeBytes + EditorContext.SizeBytes + EditorSceneLoadA.SizeBytes + EditorSceneLoadB.SizeBytes + Swapchain.SizeBytes + ShaderCache.SizeBytes + Serializer.SizeBytes + Network.SizeBytes + Input.SizeBytes;
        }

        // Validates that the sum of all SizeBytes fields does not exceed total_available_bytes.
        // Returns false and logs the overage if the budget is exceeded.
        inline bool Validate(uint64_t total_available_bytes) const
        {
            const uint64_t capacity = TotalCapacity();
            ZENGINE_VALIDATE_ASSERT(capacity <= total_available_bytes, "MemoryBudgetConfig::Validate: budget exceeds arena size")
            return capacity <= total_available_bytes;
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
            // The active editor scene reserves 200 MiB from this owner. Separate
            // scene-load owners below keep replacement deserialization bounded.
            cfg.EditorContext         = {"EditorContext", ZMega(256ULL)};
            cfg.EditorSceneLoadA      = {"EditorSceneLoadA", ZMega(200ULL)};
            cfg.EditorSceneLoadB      = {"EditorSceneLoadB", ZMega(200ULL)};

            return cfg;
        }
    };

    struct MemoryManager
    {
        // Configured application runs reserve each named budget independently.
        // MainArena is retained only for unconfigured allocator/unit-test use.
        ArenaAllocator     MainArena      = {};
        // The sole long-lived owner created automatically during Initialize. It is
        // intentionally small and exists before logging, VFS, or device state.
        ArenaAllocator     BootstrapArena = {};
        MemoryBudgetConfig Budget         = {};

        void               Initialize(uint64_t buffer_size, const MemoryBudgetConfig& config);
        void               CreateBudgetedArena(const SubArenaConfig& config, ArenaAllocator* result);
        void               Shutdown();

    private:
        static constexpr uint32_t MaxOwnedArenas = 32;

        void                      RegisterOwnedArena(ArenaAllocator* arena);

        ArenaAllocator*           m_owned_arenas[MaxOwnedArenas] = {};
        uint32_t                  m_owned_arena_count            = 0;
        size_t                    m_page_size                    = 0;
        bool                      m_uses_independent_owners      = false;
    };
} // namespace ZEngine::Core::Memory
