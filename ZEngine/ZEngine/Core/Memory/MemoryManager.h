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
        SubArenaConfig  AudioEngine      = {};
        SubArenaConfig  AnimationManager = {};
        SubArenaConfig  AssetManager     = {};
        SubArenaConfig  ECSScene         = {};
        SubArenaConfig  Logging          = {};
        SubArenaConfig  VirtualFS        = {};
        SubArenaConfig  VulkanDevice     = {};
        SubArenaConfig  ImportPipeline   = {}; // engine importers + editor importers
        SubArenaConfig  UIContext        = {};
        SubArenaConfig  Swapchain        = {};
        SubArenaConfig  ShaderCache      = {};
        SubArenaConfig  Serializer       = {};
        SubArenaConfig  Network          = {};
        SubArenaConfig  Input            = {};

        // Returns the total bytes committed by all SubArenaConfig entries.
        inline uint64_t TotalCommitted() const
        {
            return AudioEngine.SizeBytes + AnimationManager.SizeBytes + AssetManager.SizeBytes + ECSScene.SizeBytes + Logging.SizeBytes + VirtualFS.SizeBytes + VulkanDevice.SizeBytes + ImportPipeline.SizeBytes + UIContext.SizeBytes + Swapchain.SizeBytes + ShaderCache.SizeBytes + Serializer.SizeBytes + Network.SizeBytes + Input.SizeBytes;
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
            cfg.AudioEngine        = {"AudioEngine", ZMega(128ULL)};
            cfg.AnimationManager   = {"AnimationManager", ZMega(256ULL)};
            cfg.AssetManager       = {"AssetManager", ZGiga(1ULL)};
            cfg.ECSScene           = {"ECSScene", ZMega(512ULL)};
            cfg.Logging            = {"Logging", ZMega(8ULL)};
            cfg.VirtualFS          = {"VirtualFS", ZMega(64ULL)};
            cfg.VulkanDevice       = {"VulkanDevice", ZGiga(1ULL)};
            cfg.ImportPipeline     = {"ImportPipeline", ZMega(3584ULL)}; // 3.5 GB — each importer gets generous headroom for large scenes
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

            return cfg;
        }
    };

    struct MemoryManager
    {
        ArenaAllocator     MainArena = {};
        MemoryBudgetConfig Budget    = {};

        void               Initialize(uint64_t buffer_size, const MemoryBudgetConfig& config);
        void               CreateBudgetedArena(const SubArenaConfig& config, ArenaAllocator* result);
        void               Shutdown();
    };
} // namespace ZEngine::Core::Memory
