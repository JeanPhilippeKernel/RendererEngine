#include <Tetragrama/Editor.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Profiling/MemoryProfiler.h>
#include <ZEngine/Profiling/Profiling.h>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

using ZEngine::ECS::SceneCancelResult;
using ZEngine::ECS::SceneLoadOutcome;
using ZEngine::ECS::SceneLoadResult;
using ZEngine::ECS::SceneRequestId;
using Clock = std::chrono::steady_clock;

namespace Tetragrama
{
    // The existing test-access friendship avoids adding automation controls to
    // the shipping editor. This executable is opt-in and never saves a scene.
    struct EditorPickTestAccess
    {
        static bool Recorded(Editor& editor)
        {
            return editor.m_loaded_scene && !editor.m_loaded_scene->InitialRenderResources();
        }
        static uint32_t DrawCount(Editor& editor)
        {
            return editor.m_loaded_scene ? editor.m_loaded_scene->DrawCount() : 0;
        }
    };
} // namespace Tetragrama

namespace
{
    struct LifecycleEditor final : Tetragrama::Editor
    {
        std::array<ZEngine::Core::VFS::VFSPath, 2>                          Sources             = {};
        std::array<SceneRequestId, 2>                                       Requests            = {};
        std::array<SceneLoadOutcome, 2>                                     Expected            = {};
        std::ofstream                                                       Samples             = {};
        Clock::time_point                                                   Started             = Clock::now();
        uint32_t                                                            Cycles              = 0;
        uint32_t                                                            Warmup              = 0;
        uint32_t                                                            Completed           = 0;
        uint32_t                                                            Count               = 0;
        uint32_t                                                            Settled             = 0;
        uint32_t                                                            Updates             = 0;
        uint64_t                                                            Generation          = 0;
        double                                                              MaximumUpdateMs     = 0.0;
        bool                                                                StartedOperation    = false;
        bool                                                                CancelAfterDispatch = false;
        bool                                                                Failed              = false;
        bool                                                                Finished            = false;
        bool                                                                Stopping            = false;
        bool                                                                ZeroRetention       = false;
        bool                                                                Steady              = false;
        bool                                                                Capacity            = false;
        uint64_t                                                            RequiredSubmissions = 8;
        bool                                                                RestoringReference  = false;
        bool                                                                HaveBaseline        = false;
        std::array<uint64_t, 8>                                             Baseline            = {};
        std::array<uint64_t, 8>                                             LastLedger          = {};
        std::array<uint32_t, ZEngine::Managers::AssetCacheLease::SlotCount> BaselinePins        = {};

        void                                                                OverrideWindowConfiguration() override
        {
            Editor::OverrideWindowConfiguration();
            WindowCfg.Width  = 1280;
            WindowCfg.Height = 720;
        }

        void Fail(const char* reason)
        {
            std::fprintf(stderr, "scene lifecycle failed at cycle %u: %s\n", Completed, reason);
            if (HaveBaseline)
            {
                for (size_t index = 0; index < Baseline.size(); ++index)
                {
                    std::fprintf(stderr, "reference ledger[%zu]: expected=%llu observed=%llu\n", index, static_cast<unsigned long long>(Baseline[index]), static_cast<unsigned long long>(LastLedger[index]));
                }
            }
            Failed = true;
            ZEngine::Engine::RequestClose();
        }

        bool Admit(uint32_t index, const ZEngine::Core::VFS::VFSPath& source)
        {
            if (!RequestSceneOpen(source, Session.SceneEpoch(), &Requests[index]))
            {
                Fail("request admission refused");
                return false;
            }
            return true;
        }

        void OnUpdate(float dt) override
        {
            const auto before = Clock::now();
            Editor::OnUpdate(dt);
            const double update_ms = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
            if (update_ms > MaximumUpdateMs)
            {
                MaximumUpdateMs = update_ms;
            }
            if (Failed || Finished)
            {
                return;
            }
#if ZENGINE_PROFILING && !ZENGINE_TRACY
            if (ZEngine::Profiling::ProfilerBuffer::CaptureDropped())
            {
                Fail("bounded diagnostic capture overflowed");
                return;
            }
#endif
            if (Stopping)
            {
                // The base update above has dispatched the final request. Close
                // with a worker/candidate in flight, not merely a waiting intent.
                Finished = true;
                ZEngine::Engine::RequestClose();
                return;
            }
            auto& engine = *ZEngine::Engine::GetContext();
            if (Clock::now() - Started > std::chrono::seconds(60))
            {
                Fail("operation/startup deadline exceeded");
                return;
            }
            if (!StartedOperation)
            {
                if (SceneOpenPending() || !Tetragrama::EditorPickTestAccess::Recorded(*this))
                {
                    return;
                }
                Started             = Clock::now();
                MaximumUpdateMs     = 0.0;
                Generation          = engine.ScenePublication.Generation();
                Count               = 1;
                Settled             = 0;
                Updates             = 0;
                StartedOperation    = true;
                RequiredSubmissions = 8;
                if (Steady && Completed)
                {
                    // Matched steady rendering: after the first load, do not
                    // admit replacements. Each sample spans eight new accepted
                    // scene submissions, not a number of host updates.
                    ZEngine::Rendering::Scenes::SceneSubmissionProgress::Snapshot current = {};
                    if (!RenderPipeline->SceneSubmissions().TryRead(&current) || current.Generation != Generation)
                    {
                        StartedOperation = false;
                        return;
                    }
                    RequiredSubmissions = current.Count + 8;
                    Count               = 0;
                    return;
                }
                Expected           = {SceneLoadOutcome::Committed, SceneLoadOutcome::Committed};
                const auto& source = Capacity ? Sources[Completed % 4 == 1 ? 1 : 0] : RestoringReference ? Sources[0] : Sources[(Completed / 4) % Sources.size()];
                if (!Admit(0, source))
                {
                    return;
                }
                if (Capacity)
                {
                    Expected[0] = Completed % 4 == 1 ? SceneLoadOutcome::Failed : SceneLoadOutcome::Committed;
                    return;
                }
                switch (RestoringReference ? 0 : Completed % 4)
                {
                    case 1:
                        Expected[0] = SceneLoadOutcome::Cancelled;
                        if (engine.SceneLoads.Cancel(Requests[0]) != SceneCancelResult::Cancelled)
                        {
                            Fail("immediate cancellation refused");
                        }
                        break;
                    case 2:
                        Expected[0] = SceneLoadOutcome::Superseded;
                        Count       = 2;
                        (void) Admit(1, Sources[1 - (Completed / 4) % Sources.size()]);
                        break;
                    case 3:
                        CancelAfterDispatch = true;
                        break;
                    default:
                        break;
                }
                return;
            }
            if (CancelAfterDispatch && ++Updates >= 2)
            {
                const auto cancelled = engine.SceneLoads.Cancel(Requests[0]);
                Expected[0]          = cancelled == SceneCancelResult::Cancelled ? SceneLoadOutcome::Cancelled : SceneLoadOutcome::Committed;
                CancelAfterDispatch  = false;
            }
            bool committed = false;
            for (uint32_t index = 0; index < Count; ++index)
            {
                SceneLoadResult result = {};
                if (!engine.SceneLoads.Poll(Requests[index], &result))
                {
                    Fail("retained result disappeared");
                    return;
                }
                if (result.Outcome == SceneLoadOutcome::Pending || result.CleanupPending || CancelAfterDispatch)
                {
                    return;
                }
                if (result.Outcome != Expected[index])
                {
                    Fail("unexpected terminal outcome");
                    return;
                }
                committed |= result.Outcome == SceneLoadOutcome::Committed;
            }
            if (engine.ScenePublication.Generation() != Generation + (committed ? 1 : 0))
            {
                Fail("publication generation advanced incorrectly");
                return;
            }
            ZEngine::Rendering::Scenes::SceneSubmissionProgress::Snapshot submitted = {};
            if (SceneOpenPending() || (committed && !Tetragrama::EditorPickTestAccess::Recorded(*this)) || !RenderPipeline->SceneSubmissions().TryRead(&submitted) || submitted.Generation != engine.ScenePublication.Generation() || submitted.Count < RequiredSubmissions || ++Settled < 8)
            {
                return;
            }
            if (Capacity && (submitted.DrawCount != ZEngine::Rendering::RenderResourceManager::SceneDrawCapacity || Tetragrama::EditorPickTestAccess::DrawCount(*this) != ZEngine::Rendering::RenderResourceManager::SceneDrawCapacity))
            {
                Fail("accepted or retained scene lost exact-cap draws");
                return;
            }
            if (ZeroRetention && !RestoringReference)
            {
                for (uint32_t index = 0; index < Count; ++index)
                {
                    if (!engine.SceneLoads.Acknowledge(Requests[index]))
                    {
                        Fail("terminal acknowledgement refused before reference restoration");
                        return;
                    }
                }
                RestoringReference = true;
                StartedOperation   = false;
                return;
            }
            auto& assets = *ZEngine::Managers::AssetManager::Instance();
            if (ZeroRetention)
            {
                // Diagnostic-only maintenance: try-locking never waits on a
                // decoder. Ordinary shipping updates do not perform this work.
                (void) assets.TrimCpuCache();
            }
            const auto cpu        = ZEngine::Managers::AssetManager::Instance()->ContainerSlab.GetStatistics();
            const auto upload     = engine.RenderResourceManager->GetCpuUploadMemoryStatistics();
            const auto gpu        = engine.Device->GpuMem.GetAllocationStatistics();
            const auto retirement = engine.Device->PendingFree.GetStatistics();
            if (ZeroRetention)
            {
                // A zero queue alone is not a GPU-idle fence. Require the same
                // live GPU ledger as the warmed reference too, rather than
                // mistaking stable retained old resources for reclamation.
                if (upload.LiveBytes || upload.LiveBlocks || upload.ActiveDecodeLeases || upload.PendingWorkerTasks || retirement.Queued || retirement.Draining)
                {
                    return;
                }
                const std::array<uint64_t, 8> current = {cpu.LiveBytes, cpu.LiveBlocks, gpu.AllocationBytes, gpu.AdmittedBytes, gpu.AllocationCount, gpu.ReservedRecords, retirement.Reserved, static_cast<uint64_t>(engine.EditorSceneLoadArenaAInUse.value.load()) + static_cast<uint64_t>(engine.EditorSceneLoadArenaBInUse.value.load())};
                LastLedger                            = current;
                if (current[7] != 1)
                {
                    return;
                }
                if (Completed >= Warmup)
                {
                    if (!HaveBaseline)
                    {
                        Baseline = current;
                        for (size_t index = 0; index < BaselinePins.size(); ++index)
                        {
                            BaselinePins[index] = assets.CpuAssetOwnerPins[index].load(std::memory_order_acquire);
                        }
                        HaveBaseline = true;
                    }
                    if (current != Baseline)
                    {
                        return; // Bounded by the operation deadline above.
                    }
                    for (size_t index = 0; index < BaselinePins.size(); ++index)
                    {
                        if (BaselinePins[index] != assets.CpuAssetOwnerPins[index].load(std::memory_order_acquire))
                        {
                            return;
                        }
                    }
                }
            }
            const auto   started_ns          = std::chrono::duration_cast<std::chrono::nanoseconds>(Started.time_since_epoch()).count();
            const double first_submission_ms = committed ? (static_cast<double>(submitted.FirstTimestampNs) - static_cast<double>(started_ns)) / 1'000'000.0 : 0.0;
            const auto   sample_ns           = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
            Samples << Completed << ',' << (Completed < Warmup ? "warmup" : "measured") << ','
                    << (Capacity              ? (Completed % 4 == 1 ? 6 : 5)
                        : Steady && Completed ? 4
                                              : Completed % 4)
                    << ',' << std::chrono::duration<double, std::milli>(Clock::now() - Started).count() << ',' << MaximumUpdateMs << ',' << cpu.LiveBytes << ',' << cpu.LiveBlocks << ',' << upload.LiveBytes << ',' << upload.LiveBlocks << ',' << gpu.AllocationBytes << ',' << gpu.AdmittedBytes << ',' << gpu.AllocationCount << ',' << gpu.ReservedRecords << ',' << retirement.Reserved << ',' << retirement.Queued << ',' << retirement.Draining << ',' << first_submission_ms << ',' << submitted.Generation << ',' << submitted.Count << ',' << started_ns << ',' << sample_ns
                    << ',' << submitted.DrawCount << ',' << cpu.HighWaterLiveBytes << ',' << upload.SumOfPoolHighWaterBytes << ',' << gpu.PeakAllocationBytes << ',' << gpu.PeakAdmittedBytes << ',' << gpu.PeakAllocationCount << '\n';
            Samples.flush();
            if (!Samples)
            {
                Fail("cannot write evidence");
                return;
            }
            for (uint32_t index = 0; index < Count; ++index)
            {
                if (!engine.SceneLoads.Acknowledge(Requests[index]))
                {
                    Fail("terminal acknowledgement refused");
                    return;
                }
            }
            ++Completed;
            StartedOperation   = false;
            RestoringReference = false;
            if (Completed == Warmup + Cycles)
            {
                // Exercise teardown with a newly admitted request, not just idle
                // shutdown. Engine teardown must join before releasing services.
                if (!Admit(0, Sources[0]))
                {
                    return;
                }
                Stopping = true;
            }
        }
    };

    bool ParseCount(const char* text, uint32_t* output)
    {
        const std::string_view value(text);
        const auto             parsed = std::from_chars(value.data(), value.data() + value.size(), *output);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && *output <= 100000;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 8 && argc != 9)
    {
        std::fprintf(
            stderr,
            "usage: SceneLifecycleProcess project.json /scene-a.zescene /scene-b.zescene evidence-directory samples.csv cycles warmup [zero-retention|steady|capacity]\nRequires desktop Vulkan and ZENGINE_ASSET_ROOT. Capacity requires scene-a with exactly the draw cap and scene-b one over; every fourth cycle refuses scene-b, preserving exact-cap accepted draws (modes 5/6). Zero-retention restores scene-a and asserts warmed live ledgers and per-slot pins; requires at least eight warmup cycles. Its timings describe that reference restoration. Steady loads scene-a "
            "once, then samples eight new accepted frames per window without further requests (mode "
            "4).\n");
        return 2;
    }
    uint32_t   cycles = 0, warmup = 0;
    const auto first  = ZEngine::Core::VFS::VFSPath::Parse(argv[2]);
    const auto second = ZEngine::Core::VFS::VFSPath::Parse(argv[3]);
    if (!ParseCount(argv[6], &cycles) || !cycles || !ParseCount(argv[7], &warmup) || first.Failed() || second.Failed())
    {
        return 2;
    }
    const bool zero_retention = argc == 9 && std::string_view(argv[8]) == "zero-retention";
    const bool steady         = argc == 9 && std::string_view(argv[8]) == "steady";
    const bool capacity       = argc == 9 && std::string_view(argv[8]) == "capacity";
    if ((argc == 9 && !zero_retention && !steady && !capacity) || (zero_retention && warmup < 8))
    {
        return 2;
    }
    std::error_code             error = {};
    const std::filesystem::path evidence(argv[4]);
    // argv[5] explicitly identifies the sample file, allowing separate fresh
    // processes to share an evidence directory without overwriting one another.
    const std::filesystem::path sample_name(argv[5]);
    if (sample_name.has_parent_path() || sample_name.extension() != ".csv")
    {
        return 2;
    }
    std::filesystem::create_directories(evidence, error);
    const auto stage_name = sample_name.stem().string() + "-stages.csv";
    if (error || std::filesystem::exists(evidence / sample_name) || std::filesystem::exists(evidence / stage_name))
    {
        return 2;
    }
    std::ofstream samples(evidence / sample_name);
    if (!samples)
    {
        return 2;
    }
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZGiga(8ULL), ZEngine::Core::Memory::MemoryBudgetConfig::Editor());
#if ZENGINE_PROFILING
    ZEngine::Profiling::MemoryProfiler::Initialize(&memory.BootstrapArena);
#endif
    ZEngine::Helpers::ThreadPoolHelper::Initialize();
    ZEngine::Core::Memory::ArenaAllocator logging = {};
    memory.CreateBudgetedArena(memory.Budget.Logging, &logging);
    ZEngine::Logging::LoggerConfiguration configuration = {};
    configuration.OutputDirectory                       = evidence.string();
    ZEngine::Logging::Logger::Initialize(&logging, configuration);
    auto* app                = ZPushStructCtor<LifecycleEditor>(&memory.BootstrapArena);
    app->ConfigFile          = argv[1];
    app->EnableRenderOverlay = true;
    app->Sources             = {first.Value(), second.Value()};
    app->Cycles              = cycles;
    app->Warmup              = warmup;
    app->ZeroRetention       = zero_retention;
    app->Steady              = steady;
    app->Capacity            = capacity;
    app->Samples             = std::move(samples);
    app->Samples << "cycle,phase,mode,request_to_settled_submission_ms,max_host_update_ms,cpu_cache_live_bytes,cpu_cache_live_blocks,upload_live_bytes,upload_live_blocks,gpu_allocation_bytes,gpu_admitted_bytes,gpu_allocation_count,gpu_reserved_records,native_reserved,native_queued,native_draining,request_to_first_submission_ms,submitted_generation,submitted_frames,window_started_ns,sample_timestamp_ns,accepted_draw_count,cpu_cache_peak_bytes,upload_sum_pool_peak_bytes,gpu_peak_allocation_bytes,gpu_peak_admitted_bytes,gpu_peak_allocation_count\n";
#if ZENGINE_PROFILING && !ZENGINE_TRACY
    // No app tasks/render thread exist yet. Export only after Run joins them.
    // Reclamation is qualified by every-cycle live ledgers/pins, not by a
    // partial timing trace. Keep its diagnostic prefix bounded to 30 seconds;
    // short timing profiles retain two minutes and require measured coverage.
    const uint32_t capture_duration_ms = zero_retention ? 30000 : 120000;
    std::fprintf(stderr, "scene lifecycle diagnostic capture: %u ms; zero-retention timings are not load qualification\n", capture_duration_ms);
    if (!ZEngine::Profiling::ProfilerBuffer::StartTimedCapture(capture_duration_ms, "SceneLoad."))
    {
        return 2;
    }
#endif
    app->Initialize(&memory);
    if (zero_retention)
    {
        ZEngine::Managers::AssetManager::Instance()->CpuRetention.store(ZEngine::Managers::AssetManager::CpuCacheRetention::ZeroRetention);
    }
    app->Run();
#if ZENGINE_PROFILING && !ZENGINE_TRACY
    ZEngine::Profiling::ProfilerBuffer::StopCapture();
    const auto dropped  = ZEngine::Profiling::ProfilerBuffer::CaptureDropped();
    // Preserve the bounded prefix even on overflow, but never report that
    // partial export as a successful capture. Diagnose either failure explicitly.
    const bool exported = ZEngine::Profiling::ProfilerBuffer::WriteCaptureCsv((evidence / stage_name).string().c_str());
    std::fprintf(stderr, "scene lifecycle diagnostic capture: records=%u dropped=%u exported=%u\n", ZEngine::Profiling::ProfilerBuffer::CaptureCount(), dropped, static_cast<unsigned>(exported));
    if (dropped || !exported)
    {
        app->Failed = true;
    }
#endif
    app->Shutdown();
    const bool passed = app->Finished && !app->Failed;
    app->OnClosed();
    app->~LifecycleEditor();
    ZEngine::Logging::Logger::Dispose();
    memory.Shutdown();
    return passed ? 0 : 1;
}
