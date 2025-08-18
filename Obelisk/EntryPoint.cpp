#include <pch.h>
#include <CLI/CLI.hpp>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Applications/GameApplication.h>

#include <Tetragrama/Editor.h>

#ifdef ZENGINE_PLATFORM

using namespace ZEngine;
using namespace ZEngine::Logging;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Applications;

int applicationEntryPoint(int argc, char* argv[])
{
    MemoryManager       manager = {};
    MemoryConfiguration config  = {.DefaultSize = ZGiga(2u)};
    manager.Initialize(config);
    auto                arena      = &(manager.ArenaAllocator);

    LoggerConfiguration logger_cfg = {};
    Logger::Initialize(arena, logger_cfg);


    GameApplicationPtr app = nullptr;


    CLI::App cli{"ObeliskCLI"};
    argv                      = cli.ensure_utf8(argv);

    std::string config_file   = "";
    bool        launch_editor = false;
    cli.add_option("--projectConfigFile", config_file, "The project config file");
    cli.add_option("--launchEditor", launch_editor, "The project config file");

    CLI11_PARSE(cli, argc, argv);


    if (launch_editor)
    {
        app = ZPushStruct(arena, Tetragrama::Editor);    
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
