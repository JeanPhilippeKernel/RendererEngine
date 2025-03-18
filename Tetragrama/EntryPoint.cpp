#include <pch.h>
#include <CLI/CLI.hpp>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include "Editor.h"

#ifdef ZENGINE_PLATFORM

using namespace ZEngine::Core::Memory;

int applicationEntryPoint(int argc, char* argv[])
{
    CLI::App app{"ZEngine Editor"};
    argv = app.ensure_utf8(argv);

    std::string json_config_file{""};
    app.add_option("--projectConfigFile", json_config_file, "The project config file");

    CLI11_PARSE(app, argc, argv);

    MemoryConfiguration config = {.DefaultSize = ZMega(5)};
    MemoryManager       manager;
    manager.Initialize(config);
    auto arena  = &(manager.ArenaAllocator);

    auto editor = ZPushStruct(arena, Tetragrama::Editor);
    editor->Initialize(arena, json_config_file.c_str());
    editor->Run();

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
