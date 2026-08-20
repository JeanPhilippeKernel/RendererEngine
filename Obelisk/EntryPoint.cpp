#include <CLI/CLI.hpp>
#include <Tetragrama/Editor.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Profiling/MemoryProfiler.h>

#ifdef ZENGINE_PLATFORM

using namespace ZEngine;
using namespace ZEngine::Logging;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Applications;
using namespace ZEngine::CrashHandlers;

int applicationEntryPoint(int argc, char* argv[])
{
    CrashHandler::Install("Obelisk", "1.0.0", "CrashDumps");

    CLI::App cli{"ObeliskCLI"};
    argv                      = cli.ensure_utf8(argv);
    std::string config_file   = "";
    bool        launch_editor = false;
    cli.add_option("--projectConfigFile", config_file, "The project config file");
    cli.add_option("--launchEditor", launch_editor, "Activate the editor");

    CLI11_PARSE(cli, argc, argv);

    MemoryManager manager = {};
    manager.Initialize(ZGiga(8u), launch_editor ? MemoryBudgetConfig::Editor() : MemoryBudgetConfig::Default());
#if ZENGINE_PROFILING
    ZEngine::Profiling::MemoryProfiler::Initialize(&manager.MainArena);
    ZEngine::Profiling::MemoryProfiler::TrackArena("MainArena", &manager.MainArena);
#endif

    Helpers::ThreadPoolHelper::Initialize();

    ArenaAllocator      logger_arena = {};
    LoggerConfiguration logger_cfg   = {};
    manager.CreateBudgetedArena(manager.Budget.Logging, &logger_arena);
    Logger::Initialize(&logger_arena, logger_cfg);
    CrashHandler::SetPreCrashCallback([](void*) {
        Logger::FlushRingBufferToCrashLog();
        Logger::Flush();
    });

    auto arena                = &(manager.MainArena);
    auto config_file_str_size = config_file.size() + 1;
    auto config_file_str      = ZPushString(arena, config_file_str_size);
    Helpers::secure_strncpy(config_file_str, config_file_str_size, config_file.c_str(), config_file.size());

    GameApplicationPtr app = nullptr;

    if (launch_editor)
    {
        app                      = ZPushStructCtor(arena, Tetragrama::Editor);
        app->EnableRenderOverlay = true;
    }

    app->ConfigFile = config_file_str;

    app->Initialize(&manager);
    app->Run();
    app->Shutdown();

    // Step 15 — join worker threads before logger/memory teardown
    Helpers::ThreadPoolHelper::Shutdown();

    // Step 16 — flush and dispose logger
    Logger::Flush();
    Logger::Dispose();

    // Step 17 — free the 8 GB arena block
    manager.Shutdown();

    // OnClosed fires after memory is freed — may only use stack/OS resources
    app->OnClosed();

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
