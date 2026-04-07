#include <CLI/CLI.hpp>
#include <Tetragrama/Editor.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>

#ifdef ZENGINE_PLATFORM

using namespace ZEngine;
using namespace ZEngine::Logging;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Applications;

int applicationEntryPoint(int argc, char* argv[])
{
    MemoryManager       manager = {};
    MemoryConfiguration config  = {.DefaultSize = ZGiga(3u)};
    manager.Initialize(config);
    auto                arena      = &(manager.Allocator);

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

    app->ConfigFile = config_file.c_str();

    app->Initialize(arena);
    app->Run();
    app->Shutdown();

    Logger::Dispose();

    manager.Shutdowm();

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
