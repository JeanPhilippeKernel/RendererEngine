#include <CLI/CLI.hpp>
#include <Tetragrama/Editor.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/EngineAssetRoot.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Profiling/MemoryProfiler.h>
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
    argv                      = cli.ensure_utf8(argv);
    std::string config_file   = "";
    bool        launch_editor = false;
    bool        gizmo_debug   = false;
    cli.add_option("--projectConfigFile", config_file, "The project config file");
    cli.add_option("--launchEditor", launch_editor, "Activate the editor");
    cli.add_flag("--gizmoDebug", gizmo_debug, "Show the non-interactive native gizmo renderer fixture (editor only)");

    CLI11_PARSE(cli, argc, argv);

    MemoryManager manager = {};
    manager.Initialize(ZGiga(8ULL), launch_editor ? MemoryBudgetConfig::Editor() : MemoryBudgetConfig::Default());
#if ZENGINE_PROFILING
    ZEngine::Profiling::MemoryProfiler::Initialize(&manager.BootstrapArena);
    ZEngine::Profiling::MemoryProfiler::TrackArena("Bootstrap", &manager.BootstrapArena);
#endif

    Helpers::ThreadPoolHelper::Initialize();

    ArenaAllocator      logger_arena = {};
    LoggerConfiguration logger_cfg   = {};
    logger_cfg.OutputDirectory       = log_directory.string();
    logger_cfg.CrashLogDir           = crash_log_directory_string;
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
        app->EnableRenderOverlay                                = true;
    }

    app->ConfigFile = config_file_str;

    app->Initialize(&manager);
    app->Run();
    app->Shutdown();

    // Step 16 — flush and dispose logger
    Logger::Flush();
    Logger::Dispose();

    // OnClosed fires -  may only use stack/OS resources
    app->OnClosed();

    // Step 17 — free the 8 GB arena block
    manager.Shutdown();

    CrashHandler::Uninstall();
    return 0;
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
