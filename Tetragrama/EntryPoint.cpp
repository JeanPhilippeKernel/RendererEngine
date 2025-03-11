#include <pch.h>
#include <CLI/CLI.hpp>
#include "Editor.h"

#ifdef ZENGINE_PLATFORM

int applicationEntryPoint(int argc, char* argv[])
{
    CLI::App app{"ZEngine Editor"};
    argv = app.ensure_utf8(argv);

    std::string json_config_file{""};
    app.add_option("--projectConfigFile", json_config_file, "The project config file");

    CLI11_PARSE(app, argc, argv);

    Tetragrama::EditorConfiguration editor_config = {};
    editor_config.ReadConfig(json_config_file);

    auto editor = ZEngine::Helpers::CreateRef<Tetragrama::Editor>(editor_config);
    editor->Initialize();
    editor->Run();

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
