#include <pch.h>
#include <CLI/CLI.hpp>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Logging/Logger.h>
#include "Editor.h"

#ifdef ZENGINE_PLATFORM

using namespace ZEngine;
using namespace ZEngine::Logging;
using namespace ZEngine::Core::Memory;

int applicationEntryPoint(int argc, char* argv[])
{
    CLI::App app{"ZEngine Editor"};
    argv = app.ensure_utf8(argv);

    std::string editor_cfg_file{""};
    app.add_option("--projectConfigFile", editor_cfg_file, "The project config file");

    CLI11_PARSE(app, argc, argv);

    MemoryManager       manager = {};
    MemoryConfiguration config  = {.DefaultSize = ZGiga(1)};
    manager.Initialize(config);
    auto                arena      = &(manager.ArenaAllocator);

    LoggerConfiguration logger_cfg = {};
    Logger::Initialize(arena, logger_cfg);

    auto editor = ZPushStruct(arena, Tetragrama::Editor);
    editor->Initialize(arena, editor_cfg_file.c_str());
    editor->Run();

    editor->Dispose();
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
