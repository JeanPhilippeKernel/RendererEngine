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

        // Returns the total bytes committed by all SubArenaConfig entries.
        inline uint64_t TotalCommitted() const
        {
            return AudioEngine.SizeBytes + AnimationManager.SizeBytes + AssetManager.SizeBytes + ECSScene.SizeBytes + Logging.SizeBytes + VirtualFS.SizeBytes + VulkanDevice.SizeBytes + Importer.SizeBytes + UIContext.SizeBytes + Swapchain.SizeBytes + ShaderCache.SizeBytes + Serializer.SizeBytes + Network.SizeBytes;
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

        void               Initialize(uint64_t buffer_size, const MemoryBudgetConfig& config)
        {
            Budget = config;
            config.Validate(buffer_size);

            unsigned long page_size = 0ul;

            // Get the system page size
            // This is platform-specific, so we need to handle it differently for Windows and Linux/macOS
            // 4KB is the default page size, but it isn't guaranteed.
            // Linux on ARM64 has a 16KB page size, macOS on ARM64 has a 16KB page size, and Windows on ARM64 has a 4KB page size.
#ifdef _WIN32
            SYSTEM_INFO sys_info;
            GetSystemInfo(&sys_info);
            page_size = sys_info.dwPageSize;
#elif defined(__linux__) || defined(__APPLE__)
            page_size = sysconf(_SC_PAGESIZE);
#endif
            MainArena.Initialize(buffer_size, page_size);
        }

        void CreateBudgetedArena(const SubArenaConfig& config, ArenaAllocator* result)
        {
            ZENGINE_VALIDATE_ASSERT(config.SizeBytes > 0, "MemoryManager::CreateBudgetedArena: SizeBytes must be > 0")
            ZENGINE_VALIDATE_ASSERT(result != nullptr, "MemoryManager::CreateBudgetedArena: out must not be null")

            MainArena.CreateSubArena(config.SizeBytes, result);

#if defined(ZENGINE_ENABLE_PROFILING)
            MemoryProfiler::TrackArena(config.Name, result);
#endif
        }

        void Shutdown()
        {
            MainArena.Shutdown();
        }
    };
} // namespace ZEngine::Core::Memory
