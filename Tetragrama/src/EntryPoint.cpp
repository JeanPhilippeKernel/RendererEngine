#include <pch.h>
#include "Editor.h"

#ifdef ZENGINE_PLATFORM

int applicationEntryPoint(int argc, char* argv[]) {
    auto editor = ZEngine::CreateRef<Tetragrama::Editor>();
    editor->Initialize();
    editor->Run();
    return 0;
}

#ifdef _WIN32
#include <windows.h>

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow) {
    return applicationEntryPoint(__argc, __argv);
}

#else
int main(int argc, char* argv[]) {
    return applicationEntryPoint(argc, argv);
}
#endif
#endif
