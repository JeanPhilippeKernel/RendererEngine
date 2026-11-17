#include <CLI/CLI.hpp>
#include <Tetragrama/Editor.h>
#include <ZEngine/Applications/CookedRuntimeApplication.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/EngineAssetRoot.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Profiling/MemoryProfiler.h>
#include <ZEngine/Profiling/ProfilerBuffer.h>
#include <cstdio>
#include <filesystem>

#ifdef ZENGINE_PLATFORM

using namespace ZEngine;
using namespace ZEngine::Logging;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Applications;
using namespace ZEngine::CrashHandlers;

int applicationEntryPoint(int argc, char* argv[])
{
    const auto executable_directory       = Core::GetExecutablePath().parent_path();
    const auto log_directory              = executable_directory.empty() ? std::filesystem::path("Logs") : executable_directory / "Logs";
    const auto crash_log_directory        = executable_directory.empty() ? std::filesystem::path("CrashDumps") : executable_directory / "CrashDumps";
    const auto crash_log_directory_string = crash_log_directory.string();

    CrashHandler::Install("Obelisk", "1.0.0", crash_log_directory_string.c_str());

    CLI::App cli{"ObeliskCLI"};
    argv                                      = cli.ensure_utf8(argv);
    std::string config_file                   = "";
    std::string cooked_package                = "";
    std::string cooked_scene                  = "";
    std::string cooked_capture                = "";
    std::string source_capture                = "";
    std::string source_import                 = "";
    std::string log_directory_override        = "";
    std::string cooked_alternate              = "";
    std::string cooked_stage_trace            = "";
    std::string cooked_camera                 = "";
    bool        launch_editor                 = false;
    bool        gizmo_debug                   = false;
    bool        cooked_exit_after_frame       = false;
    uint32_t    cooked_expected_draws         = 0;
    uint32_t    cooked_reloads                = 0;
    uint32_t    cooked_steady_frames          = 0;
    uint32_t    cooked_post_steady_frames     = 0;
    bool        cooked_cancel_reload          = false;
    bool        cooked_cancel_cpu             = false;
    bool        cooked_retry_failure          = false;
    bool        cooked_supersede_reload       = false;
    bool        cooked_supersede_cpu          = false;
    bool        cooked_supersede_gpu          = false;
    bool        cooked_runtime_view           = false;
    bool        cooked_standalone             = false;
    bool        cooked_fail_reload_staging    = false;
    bool        cooked_fail_preflight         = false;
    bool        cooked_reject_reload_submit   = false;
    bool        cooked_device_loss            = false;
    bool        cooked_audit_source           = false;
    bool        cooked_lifecycle_stress       = false;
    bool        cooked_fixed_window           = false;
    bool        cooked_wait_visible           = false;
    bool        cooked_observe_gpu_completion = false;
    bool        cooked_shutdown_cpu           = false;
    bool        cooked_shutdown_gpu           = false;
    cli.add_option("--projectConfigFile", config_file, "The project config file");
    cli.add_option("--cookedPackage", cooked_package, "Read-only cooked package directory");
    cli.add_option("--cookedScene", cooked_scene, "Scene UUID in the cooked package");
    cli.add_flag("--cookedRuntimeView", cooked_runtime_view, "Use the cooked scene's authored camera in the editor-hosted full-window view");
    cli.add_flag("--cookedStandalone", cooked_standalone, "Run a cooked scene in a renderer-only runtime host");
    cli.add_option("--cookedCamera", cooked_camera, "Optional authored camera UUID; otherwise require exactly one main camera");
    cli.add_flag("--cookedExitAfterFrame", cooked_exit_after_frame, "Exit after the cooked scene's first recorded frame (test runs)");
    cli.add_option("--cookedExpectedDraws", cooked_expected_draws, "Require this many prepared and submitted draws in a cooked test run");
    cli.add_option("--cookedReloads", cooked_reloads, "Number of alternating cooked scene replacements in a test run");
    cli.add_option("--cookedSteadyFrames", cooked_steady_frames, "Measure this many settled standalone frames before test replacements");
    cli.add_option("--cookedPostSteadyFrames", cooked_post_steady_frames, "Measure settled standalone frames after test replacements");
    cli.add_option("--cookedAlternateScene", cooked_alternate, "Second scene UUID for cooked replacement test runs");
    cli.add_flag("--cookedCancelReload", cooked_cancel_reload, "Cancel the first replacement after recording its mesh upload");
    cli.add_flag("--cookedCancelCpu", cooked_cancel_cpu, "Cancel the first replacement while its CPU worker owns the reconstruction");
    cli.add_flag("--cookedRetryFailure", cooked_retry_failure, "Retry the original cooked scene after a replacement failure");
    cli.add_flag("--cookedSupersedeReload", cooked_supersede_reload, "Supersede the first waiting cooked replacement");
    cli.add_flag("--cookedSupersedeCpu", cooked_supersede_cpu, "Supersede a replacement while its CPU worker owns resources");
    cli.add_flag("--cookedSupersedeGpu", cooked_supersede_gpu, "Supersede a replacement after mesh upload recording");
    cli.add_flag("--cookedFailReloadStaging", cooked_fail_reload_staging, "Refuse new upload staging allocations during the first replacement");
    cli.add_flag("--cookedFailPreflight", cooked_fail_preflight, "Refuse a ready replacement before publication (test runs)");
    cli.add_flag("--cookedRejectReloadSubmit", cooked_reject_reload_submit, "Reject the first replacement's recorded cooked upload batch (test runs)");
    cli.add_flag("--cookedInjectDeviceLoss", cooked_device_loss, "Inject device loss during a standalone replacement upload (test runs)");
    cli.add_flag("--cookedAuditSource", cooked_audit_source, "Deny and count source VFS, registry and import lookups during standalone loading (test runs)");
    cli.add_flag("--cookedLifecycleStress", cooked_lifecycle_stress, "Alternate CPU and GPU upload cancellations with scene replacement (test runs)");
    cli.add_flag("--cookedFixedWindow", cooked_fixed_window, "Use a fixed 1280x720 logical window (test runs)");
    cli.add_flag("--cookedWaitVisible", cooked_wait_visible, "Wait for the fixed test window to become visible before loading");
    cli.add_flag("--cookedObserveGpuCompletion", cooked_observe_gpu_completion, "Observe completion of each generation's first accepted render submission (test runs)");
    cli.add_flag("--cookedShutdownCpu", cooked_shutdown_cpu, "Close while a replacement CPU worker is active (test runs)");
    cli.add_flag("--cookedShutdownGpu", cooked_shutdown_gpu, "Close after recording a replacement GPU upload (test runs)");
    cli.add_option("--cookedCapture", cooked_capture, "Write a bounded RGB8 PPM scene viewport capture in a cooked test run");
    cli.add_option("--cookedStageTrace", cooked_stage_trace, "Write a bounded SceneLoad stage CSV for a cooked test run");
    cli.add_option("--sourceCapture", source_capture, "Capture the configured source scene for a test run");
    cli.add_option("--sourceImport", source_import, "Import this source asset before the source capture scene");
    cli.add_option("--logDirectory", log_directory_override, "Write this process's engine logs to a specified directory");
    cli.add_option("--launchEditor", launch_editor, "Activate the editor");
    cli.add_flag("--gizmoDebug", gizmo_debug, "Show the non-interactive native gizmo renderer fixture (editor only)");

    CLI11_PARSE(cli, argc, argv);

    if (cooked_fixed_window && !((cooked_standalone && cooked_exit_after_frame) || (launch_editor && !source_capture.empty() && cooked_runtime_view)))
    {
        std::fprintf(stderr, "Fixed test window requires a standalone run or an editor runtime-view source capture.\n");
        return 2;
    }

    if (cooked_wait_visible && (!cooked_standalone || !cooked_exit_after_frame || !cooked_fixed_window))
    {
        std::fprintf(stderr, "Visible-window gating requires a fixed standalone test.\n");
        return 2;
    }
#if !defined(__APPLE__) || !ZENGINE_PROFILING
    if (cooked_wait_visible)
    {
        std::fprintf(stderr, "Visible-window gating requires a profiled macOS build.\n");
        return 2;
    }
#endif

    if (cooked_observe_gpu_completion && (!cooked_standalone || !cooked_exit_after_frame || cooked_device_loss || cooked_shutdown_cpu || cooked_shutdown_gpu))
    {
        std::fprintf(stderr, "Render completion observation requires a standalone non-fatal test run.\n");
        return 2;
    }

    if (cooked_post_steady_frames && (!cooked_standalone || !cooked_exit_after_frame || !cooked_reloads || cooked_post_steady_frames > 600 || cooked_lifecycle_stress || cooked_retry_failure || cooked_device_loss || cooked_shutdown_cpu || cooked_shutdown_gpu))
    {
        std::fprintf(stderr, "Post-replacement steady observation requires a bounded standalone replacement run.\n");
        return 2;
    }

    if (cooked_fail_preflight && (!cooked_standalone || cooked_reloads != 1 || !cooked_retry_failure || cooked_cancel_reload || cooked_cancel_cpu || cooked_fail_reload_staging || cooked_supersede_reload || cooked_supersede_cpu || cooked_supersede_gpu || cooked_reject_reload_submit || cooked_device_loss || cooked_lifecycle_stress || cooked_shutdown_cpu || cooked_shutdown_gpu))
    {
        std::fprintf(stderr, "Publication preflight refusal requires one standalone replacement and retry without another probe.\n");
        return 2;
    }

    if ((cooked_shutdown_cpu || cooked_shutdown_gpu) && (!cooked_standalone || !cooked_exit_after_frame || cooked_reloads != 1 || (cooked_shutdown_cpu && cooked_shutdown_gpu) || cooked_lifecycle_stress || cooked_cancel_reload || cooked_cancel_cpu || cooked_retry_failure || cooked_supersede_reload || cooked_supersede_cpu || cooked_supersede_gpu || cooked_fail_reload_staging || cooked_reject_reload_submit || cooked_device_loss || cooked_steady_frames || !cooked_capture.empty()))
    {
        std::fprintf(stderr, "Shutdown probe requires one standalone replacement without other probes.\n");
        return 2;
    }

    if (cooked_lifecycle_stress && (!cooked_standalone || !cooked_exit_after_frame || !cooked_reloads || cooked_cancel_reload || cooked_cancel_cpu || cooked_retry_failure || cooked_supersede_reload || cooked_supersede_cpu || cooked_supersede_gpu || cooked_fail_reload_staging || cooked_reject_reload_submit || cooked_device_loss || cooked_steady_frames || !cooked_capture.empty()))
    {
        std::fprintf(stderr, "Lifecycle stress requires standalone replacements without other probes.\n");
        return 2;
    }

    if (cooked_audit_source && (!cooked_standalone || !cooked_exit_after_frame))
    {
        std::fprintf(stderr, "Source-independence audit requires a standalone test run.\n");
        return 2;
    }

    if (cooked_device_loss && (!cooked_standalone || cooked_reloads != 1 || !cooked_exit_after_frame || cooked_cancel_reload || cooked_cancel_cpu || cooked_fail_reload_staging || cooked_retry_failure || cooked_supersede_reload || cooked_supersede_cpu || cooked_supersede_gpu || cooked_reject_reload_submit || cooked_steady_frames || !cooked_capture.empty()))
    {
        std::fprintf(stderr, "Device-loss probe requires one standalone replacement without other probes.\n");
        return 2;
    }

    if (cooked_steady_frames && (!cooked_standalone || !cooked_exit_after_frame || cooked_steady_frames > 600 || cooked_cancel_reload || cooked_cancel_cpu || cooked_fail_reload_staging || cooked_retry_failure || cooked_supersede_reload || cooked_supersede_cpu || cooked_supersede_gpu || cooked_reject_reload_submit || !cooked_capture.empty()))
    {
        std::fprintf(stderr, "Steady frame measurement requires a standalone test without fault probes or capture and is limited to 600 frames.\n");
        return 2;
    }

    if (cooked_standalone && (cooked_cancel_reload || cooked_cancel_cpu || cooked_fail_reload_staging) && !cooked_retry_failure)
    {
        std::fprintf(stderr, "Standalone cancellation probe requires --cookedRetryFailure.\n");
        return 2;
    }

    if (cooked_reject_reload_submit && (cooked_reloads != 1 || !cooked_retry_failure || cooked_cancel_reload || cooked_cancel_cpu || cooked_fail_reload_staging || cooked_supersede_reload || cooked_supersede_cpu || cooked_supersede_gpu))
    {
        std::fprintf(stderr, "Cooked submission rejection requires one replacement and retry, without another fault probe.\n");
        return 2;
    }

    const auto cooked_uuid    = uuids::uuid::from_string(cooked_scene);
    const auto alternate_uuid = uuids::uuid::from_string(cooked_alternate);
    const auto camera_uuid    = uuids::uuid::from_string(cooked_camera);
    if ((launch_editor == cooked_standalone) || (cooked_standalone && (cooked_package.empty() || cooked_runtime_view || (cooked_retry_failure && cooked_reloads != 1) || gizmo_debug || !source_capture.empty() || !source_import.empty())) || (cooked_package.empty() != cooked_scene.empty()) || (cooked_exit_after_frame && cooked_package.empty()) || (!source_import.empty() && source_capture.empty()) || ((cooked_cancel_reload || cooked_fail_reload_staging || cooked_cancel_cpu) && cooked_reloads != 1) ||
        (int(cooked_cancel_reload) + int(cooked_fail_reload_staging) + int(cooked_cancel_cpu) > 1) || (cooked_reloads && (!cooked_exit_after_frame || !alternate_uuid || alternate_uuid->is_nil() || !cooked_uuid || *alternate_uuid == *cooked_uuid)) || (!cooked_reloads && !cooked_alternate.empty()) || cooked_reloads > (cooked_lifecycle_stress ? 1100u : 1000u) || ((cooked_expected_draws || !cooked_capture.empty()) && !cooked_exit_after_frame && source_capture.empty()) ||
        (!source_capture.empty() && (config_file.empty() || !cooked_package.empty() || cooked_exit_after_frame || !cooked_capture.empty())) || (!cooked_package.empty() && (!config_file.empty() || !cooked_uuid || cooked_uuid->is_nil())))
    {
        std::fprintf(stderr, "Choose --launchEditor true or --cookedStandalone; cooked launch requires --cookedPackage and --cookedScene UUID, without --projectConfigFile.\n");
        return 2;
    }
    if (!cooked_stage_trace.empty() && (!cooked_exit_after_frame || std::filesystem::exists(cooked_stage_trace)))
    {
        std::fprintf(stderr, "Cooked stage trace requires a cooked test run and a new output path.\n");
        return 2;
    }
    if ((cooked_supersede_reload || cooked_supersede_cpu || cooked_supersede_gpu) && (cooked_reloads != 1 || int(cooked_supersede_reload) + int(cooked_supersede_cpu) + int(cooked_supersede_gpu) > 1 || cooked_cancel_reload || cooked_cancel_cpu || cooked_fail_reload_staging || cooked_retry_failure))
    {
        std::fprintf(stderr, "Cooked supersession probe requires one replacement without another fault probe.\n");
        return 2;
    }
    if ((cooked_runtime_view && cooked_package.empty() && source_capture.empty()) || (!cooked_camera.empty() && (!(cooked_runtime_view || cooked_standalone) || !camera_uuid || camera_uuid->is_nil())))
    {
        std::fprintf(stderr, "Runtime view requires a cooked package or source capture; an explicit camera must be a non-nil UUID.\n");
        return 2;
    }
#if !ZENGINE_PROFILING || ZENGINE_TRACY
    if (!cooked_stage_trace.empty())
    {
        std::fprintf(stderr, "Cooked stage CSV requires non-Tracy profiling in this build.\n");
        return 2;
    }
#endif

    MemoryManager manager = {};
    manager.Initialize(ZGiga(8ULL), MemoryBudgetConfig::Editor());
#if ZENGINE_PROFILING
    ZEngine::Profiling::MemoryProfiler::Initialize(&manager.BootstrapArena);
    ZEngine::Profiling::MemoryProfiler::TrackArena("Bootstrap", &manager.BootstrapArena);
#endif

    Helpers::ThreadPoolHelper::Initialize();

    ArenaAllocator      logger_arena = {};
    LoggerConfiguration logger_cfg   = {};
    logger_cfg.OutputDirectory       = log_directory_override.empty() ? log_directory.string() : log_directory_override;
    logger_cfg.CrashLogDir           = crash_log_directory_string;
    if (cooked_lifecycle_stress)
        logger_cfg.FileMaxBytes = 64 * 1024 * 1024;
    manager.CreateBudgetedArena(manager.Budget.Logging, &logger_arena);
    Logger::Initialize(&logger_arena, logger_cfg);
    CrashHandler::SetPreCrashCallback([](void*) {
        Logger::FlushRingBufferToCrashLog();
        Logger::Flush();
    });

    auto arena                = &(manager.BootstrapArena);
    auto config_file_str_size = config_file.size() + 1;
    auto config_file_str      = ZPushString(arena, config_file_str_size);
    Helpers::secure_strncpy(config_file_str, config_file_str_size, config_file.c_str(), config_file.size());

    GameApplicationPtr app = nullptr;

    if (launch_editor)
    {
        app                                                     = ZPushStructCtor<Tetragrama::Editor>(arena);
        static_cast<Tetragrama::Editor*>(app)->EnableGizmoDebug = gizmo_debug;
        if (!cooked_package.empty())
            static_cast<Tetragrama::Editor*>(app)->ConfigureCookedStartup(cooked_package, *cooked_uuid, cooked_exit_after_frame, cooked_expected_draws, cooked_capture, cooked_reloads, alternate_uuid.value_or(uuids::uuid{}), cooked_cancel_reload, cooked_fail_reload_staging, cooked_cancel_cpu);
        if (cooked_runtime_view)
            static_cast<Tetragrama::Editor*>(app)->ConfigureCookedRuntimeView(camera_uuid.value_or(uuids::uuid{}));
        if (cooked_retry_failure)
            static_cast<Tetragrama::Editor*>(app)->EnableCookedRetryProbe();
        if (cooked_reject_reload_submit)
            static_cast<Tetragrama::Editor*>(app)->EnableCookedSubmitRejectionProbe();
        if (cooked_supersede_reload)
            static_cast<Tetragrama::Editor*>(app)->EnableCookedSupersedeProbe();
        if (cooked_supersede_cpu)
            static_cast<Tetragrama::Editor*>(app)->EnableCookedCpuSupersedeProbe();
        if (cooked_supersede_gpu)
            static_cast<Tetragrama::Editor*>(app)->EnableCookedGpuSupersedeProbe();
        if (!source_capture.empty())
            static_cast<Tetragrama::Editor*>(app)->ConfigureSourceCapture(source_capture, cooked_expected_draws, source_import);
        app->EnableRenderOverlay = true;
    }
    else
    {
        auto* runtime = ZPushStructCtor<CookedRuntimeApplication>(arena);
        runtime->Configure(cooked_package, *cooked_uuid, camera_uuid.value_or(uuids::uuid{}), cooked_exit_after_frame, cooked_expected_draws, cooked_reloads, alternate_uuid.value_or(uuids::uuid{}), cooked_retry_failure, cooked_cancel_reload, cooked_cancel_cpu, cooked_supersede_cpu, cooked_supersede_gpu, cooked_supersede_reload);
        if (cooked_lifecycle_stress)
            runtime->EnableLifecycleStress();
        if (cooked_fixed_window)
            runtime->EnableFixedTestWindow();
        if (cooked_observe_gpu_completion)
            runtime->EnableGpuCompletionObservation();
        if (cooked_shutdown_cpu || cooked_shutdown_gpu)
            runtime->EnableShutdownProbe(cooked_shutdown_gpu);
        if (cooked_fail_preflight)
            runtime->EnablePreflightRefusalProbe();
        if (cooked_steady_frames)
            runtime->ConfigureSteadyFrames(cooked_steady_frames);
        if (cooked_post_steady_frames)
            runtime->ConfigurePostSteadyFrames(cooked_post_steady_frames);
        if (cooked_reject_reload_submit)
            runtime->EnableUploadSubmissionRejectionProbe();
        if (cooked_fail_reload_staging)
            runtime->EnableStagingRefusalProbe();
        if (cooked_device_loss)
            runtime->EnableDeviceLossProbe();
        if (cooked_audit_source)
            runtime->EnableSourceIndependenceAudit();
        if (!cooked_capture.empty())
            runtime->ConfigureCapture(cooked_capture);
        app = runtime;
    }

    app->ConfigFile = config_file_str;
    if (cooked_fixed_window)
    {
        app->WindowCfg.Width     = 1280;
        app->WindowCfg.Height    = 720;
        app->WindowCfg.Resizable = false;
        app->WindowCfg.Maximize  = false;
    }
    app->WindowCfg.WaitForVisibleTestWindow = cooked_wait_visible;

    bool trace_ok                           = true;
#if ZENGINE_PROFILING && !ZENGINE_TRACY
    if (!cooked_stage_trace.empty())
        trace_ok = ZEngine::Profiling::ProfilerBuffer::StartTimedCapture(120000, "SceneLoad.");
#endif
    app->Initialize(&manager);
    app->Run();
#if ZENGINE_PROFILING && !ZENGINE_TRACY
    if (!cooked_stage_trace.empty())
    {
        const bool expired = ZEngine::Profiling::ProfilerBuffer::CaptureDeadlinePassed();
        ZEngine::Profiling::ProfilerBuffer::StopCapture();
        const auto dropped  = ZEngine::Profiling::ProfilerBuffer::CaptureDropped();
        const bool exported = ZEngine::Profiling::ProfilerBuffer::WriteCaptureCsv(cooked_stage_trace.c_str());
        std::fprintf(stderr, "Cooked stage capture: records=%u dropped=%u expired=%u exported=%u\n", ZEngine::Profiling::ProfilerBuffer::CaptureCount(), dropped, static_cast<unsigned>(expired), static_cast<unsigned>(exported));
        trace_ok = trace_ok && !dropped && !expired && exported;
    }
#endif
    const bool cooked_frame_recorded     = !(cooked_exit_after_frame || !source_capture.empty()) || (cooked_standalone ? static_cast<CookedRuntimeApplication*>(app)->FrameRecorded() : static_cast<Tetragrama::Editor*>(app)->CookedStartupFrameRecorded());
    const bool cooked_failure_preserved  = !cooked_standalone && cooked_exit_after_frame && static_cast<Tetragrama::Editor*>(app)->CookedStartupFailurePreserved();
    const bool cancellation_observed     = !cooked_cancel_reload || (cooked_standalone ? static_cast<CookedRuntimeApplication*>(app)->UploadCancellationObserved() : static_cast<Tetragrama::Editor*>(app)->CookedReloadCancelledDuringUpload());
    const bool cpu_cancellation_observed = !cooked_cancel_cpu || (cooked_standalone ? static_cast<CookedRuntimeApplication*>(app)->CpuCancellationObserved() : static_cast<Tetragrama::Editor*>(app)->CookedReloadCancelledDuringCpu());
    const bool staging_refusal_observed  = !cooked_fail_reload_staging || (cooked_standalone ? static_cast<CookedRuntimeApplication*>(app)->StagingRefusalObserved() : static_cast<Tetragrama::Editor*>(app)->CookedReloadStagingRefused());
    app->Shutdown();
    const bool source_audit_passed   = !cooked_audit_source || static_cast<CookedRuntimeApplication*>(app)->SourceAuditPassed();
    const bool shutdown_probe_passed = !(cooked_shutdown_cpu || cooked_shutdown_gpu) || static_cast<CookedRuntimeApplication*>(app)->ShutdownProbePassed();

    // Step 16 — flush and dispose logger
    Logger::Flush();
    Logger::Dispose();

    // OnClosed fires -  may only use stack/OS resources
    app->OnClosed();

    // Step 17 — free the 8 GB arena block
    manager.Shutdown();

    CrashHandler::Uninstall();
    return !trace_ok || !source_audit_passed || !shutdown_probe_passed ? 2 : cooked_frame_recorded ? 0 : cooked_failure_preserved && cancellation_observed && cpu_cancellation_observed && staging_refusal_observed ? 1 : 2;
}

#ifdef _WIN32
#include <windows.h>
#include <winrt/Windows.Foundation.h>

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    winrt::init_apartment();
    return applicationEntryPoint(__argc, __argv);
}

#else
int main(int argc, char* argv[])
{
    return applicationEntryPoint(argc, argv);
}
#endif
#endif
