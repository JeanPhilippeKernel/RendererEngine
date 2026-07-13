#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <cstdio>

int main()
{
    ZEngine::CrashHandlers::CrashHandler::Install("ZEngineDemo", "0.2.0", "/tmp/zengine_crash_demo_logs");
    fprintf(stderr, "[demo] Crash handler installed. Triggering crash...\n");
    ZEngine::CrashHandlers::CrashHandler::OnCrash("GPU device lost: VK_ERROR_DEVICE_LOST");
}
