#include <CLI/CLI.hpp>
#include <ZEngine/Applications/StandaloneRuntimeApplication.h>
#include <ZEngine/Core/EngineAssetRoot.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Profiling/MemoryProfiler.h>
#include <cstdio>
#include <filesystem>
#include <optional>

using namespace ZEngine;
using namespace ZEngine::Applications;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::CrashHandlers;
using namespace ZEngine::Logging;

/// @brief Run the renderer-only application using a cooked scene package.
static int RunRuntime(int argc, char* argv[])
{
    CLI::App cli{"ZEngine standalone runtime"};
    argv = cli.ensure_utf8(argv);
    std::string package, scene, camera, log_directory;
    bool        standalone = false, exit_after_frame = false, audit_source = false;
    uint32_t    expected_draws = 0;
    cli.add_option("--cookedPackage", package, "Cooked package directory")->required();
    cli.add_option("--cookedScene", scene, "Startup scene UUID")->required();
    cli.add_option("--cookedCamera", camera, "Authored camera UUID");
    cli.add_option("--logDirectory", log_directory, "Output directory for logs");
    cli.add_flag("--cookedStandalone", standalone, "Use the standalone runtime");
    cli.add_flag("--cookedExitAfterFrame", exit_after_frame, "Exit after the first scene frame");
    cli.add_flag("--cookedAuditSource", audit_source, "Audit source access during a test run");
    cli.add_option("--cookedExpectedDraws", expected_draws, "Expected draw count in a test run");
    CLI11_PARSE(cli, argc, argv);

    const auto scene_id  = uuids::uuid::from_string(scene);
    const auto camera_id = camera.empty() ? std::optional<uuids::uuid>{} : uuids::uuid::from_string(camera);
    if (!scene_id || scene_id->is_nil() || (!camera.empty() && (!camera_id || camera_id->is_nil())) || ((audit_source || expected_draws) && !exit_after_frame))
    {
        std::fprintf(stderr, "A non-nil scene UUID is required; camera UUID and test options must be valid.\n");
        return 2;
    }

    const auto executable_directory = Core::GetExecutablePath().parent_path();
    const auto crash_directory      = (executable_directory / "CrashDumps").string();
    CrashHandler::Install("ZEngineRuntime", "1.0.0", crash_directory.c_str());
    MemoryManager memory = {};
    // The runtime scene loader currently shares the editor scene-load arena profile.
    memory.Initialize(ZGiga(8ULL), MemoryBudgetConfig::Editor());
#if ZENGINE_PROFILING
    Profiling::MemoryProfiler::Initialize(&memory.BootstrapArena);
    Profiling::MemoryProfiler::TrackArena("Bootstrap", &memory.BootstrapArena);
#endif
    Helpers::ThreadPoolHelper::Initialize();
    ArenaAllocator      logger_arena  = {};
    LoggerConfiguration logger_config = {};
    logger_config.OutputDirectory     = log_directory.empty() ? (executable_directory / "Logs").string() : log_directory;
    logger_config.CrashLogDir         = crash_directory;
    memory.CreateBudgetedArena(memory.Budget.Logging, &logger_arena);
    Logger::Initialize(&logger_arena, logger_config);
    CrashHandler::SetPreCrashCallback([](void*) {
        Logger::FlushRingBufferToCrashLog();
        Logger::Flush();
    });

    auto* app = ZPushStructCtor<StandaloneRuntimeApplication>(&memory.BootstrapArena);
    app->Configure(package, *scene_id, camera_id.value_or(uuids::uuid{}), exit_after_frame, expected_draws);
    if (audit_source)
        app->EnableSourceIndependenceAudit();
    app->Initialize(&memory);
    app->Run();
    const bool frame_recorded = !exit_after_frame || app->FrameRecorded();
    const bool failed         = app->Failed();
    app->Shutdown();
    const bool audit_passed = !audit_source || app->SourceAuditPassed();
    Logger::Flush();
    Logger::Dispose();
    app->OnClosed();
    memory.Shutdown();
    CrashHandler::Uninstall();
    return frame_recorded && !failed && audit_passed ? 0 : 2;
}

#ifdef _WIN32
#include <windows.h>
#include <winrt/Windows.Foundation.h>
int main(int argc, char* argv[])
{
    winrt::init_apartment();
    return RunRuntime(argc, argv);
}
#else
int main(int argc, char* argv[])
{
    return RunRuntime(argc, argv);
}
#endif
