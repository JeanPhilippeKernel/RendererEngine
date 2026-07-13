#include <CLI/CLI.hpp>
#include <Tetragrama/Editor.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>

#ifdef ZENGINE_PLATFORM

using namespace ZEngine;
using namespace ZEngine::Logging;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Applications;
using namespace ZEngine::CrashHandlers;

int applicationEntryPoint(int argc, char* argv[])
{
    CrashHandler::Install("Obelisk", "1.0.0", "CrashDumps");

    MemoryManager       manager = {};
    MemoryConfiguration config  = {.BufferSize = ZGiga(3u)};
    manager.Initialize(config);
    auto                arena      = &(manager.MainArena);

    LoggerConfiguration logger_cfg = {};
    Logger::Initialize(arena, logger_cfg);

    Helpers::ThreadPoolHelper::Initialize();

    GameApplicationPtr app = nullptr;

    CLI::App           cli{"ObeliskCLI"};
    argv                      = cli.ensure_utf8(argv);

    std::string config_file   = "";
    bool        launch_editor = false;
    cli.add_option("--projectConfigFile", config_file, "The project config file");
    cli.add_option("--launchEditor", launch_editor, "The project config file");

    CLI11_PARSE(cli, argc, argv);

    if (launch_editor)
    {
        app                      = ZPushStructCtor(arena, Tetragrama::Editor);
        app->EnableRenderOverlay = true;
    }

    auto config_file_str_size = config_file.size() + 1;
    auto config_file_str      = ZPushString(arena, config_file_str_size);
    Helpers::secure_strncpy(config_file_str, config_file_str_size, config_file.c_str(), config_file.size());
    app->ConfigFile = config_file_str;

    app->Initialize(arena);
    app->Run();
    app->Shutdown();

    Logger::Dispose();

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
