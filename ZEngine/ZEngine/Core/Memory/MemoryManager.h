#pragma once
#ifdef _WIN32
// clang-format off
#include <windows.h>
// clang-format on
#include <sysinfoapi.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

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
        SubArenaConfig  Importer         = {};
        SubArenaConfig  UIContext        = {};
        SubArenaConfig  Swapchain        = {};
        SubArenaConfig  ShaderCache      = {};
        SubArenaConfig  Serializer       = {};
        SubArenaConfig  Network          = {};
        SubArenaConfig  Input            = {};

        // Returns the total bytes committed by all SubArenaConfig entries.
        inline uint64_t TotalCommitted() const
        {
            return AudioEngine.SizeBytes + AnimationManager.SizeBytes + AssetManager.SizeBytes + ECSScene.SizeBytes + Logging.SizeBytes + VirtualFS.SizeBytes + VulkanDevice.SizeBytes + Importer.SizeBytes + UIContext.SizeBytes + Swapchain.SizeBytes + ShaderCache.SizeBytes + Serializer.SizeBytes + Network.SizeBytes + Input.SizeBytes;
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
            cfg.AudioEngine        = {"AudioEngine", ZMega(32ULL)};
            cfg.AnimationManager   = {"AnimationManager", ZMega(64ULL)};
            cfg.AssetManager       = {"AssetManager", ZMega(100ULL)};
            cfg.ECSScene           = {"ECSScene", ZMega(128ULL)};
            cfg.Logging            = {"Logging", ZMega(4ULL)};
            cfg.VirtualFS          = {"VirtualFS", ZMega(32ULL)};
            cfg.VulkanDevice       = {"VulkanDevice", ZMega(512ULL)};
            cfg.Importer           = {"Importer", ZMega(350ULL)};
            cfg.UIContext          = {"UIContext", ZMega(8ULL)};
            cfg.Swapchain          = {"Swapchain", ZMega(3ULL)};
            cfg.ShaderCache        = {"ShaderCache", ZMega(16ULL)};
            cfg.Serializer         = {"Serializer", ZMega(150ULL)};
            cfg.Network            = {"Network", ZMega(32ULL)};
            cfg.Input              = {"Input", ZMega(1ULL)};

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
            cfg.UIContext.SizeBytes   = ZMega(32ULL);

            return cfg;
        }
    };

    struct MemoryManager
    {
        ArenaAllocator     MainArena = {};
        MemoryBudgetConfig Budget    = {};

        void               Initialize(uint64_t buffer_size, const MemoryBudgetConfig& config);
        void               CreateBudgetedArena(const SubArenaConfig& config, ArenaAllocator* result);

        void               Shutdown()
        {
            MainArena.Shutdown();
        }
    };
} // namespace ZEngine::Core::Memory
