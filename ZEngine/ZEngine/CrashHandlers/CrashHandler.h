#pragma once

namespace ZEngine::CrashHandlers
{
    struct CrashHandler
    {
        using PreCrashFn = void (*)(void* ctx);

        // Install the platform crash handler.
        //   app_name      — e.g. "GameName"
        //   version       — e.g. "1.0.0"
        //   crash_log_dir — directory where crash logs and .dmp files are written.
        //                   Created if it does not exist.
        //
        // Call once, before any engine subsystem is initialized.
        // No-op if already installed.
        static void              Install(const char* app_name, const char* version, const char* crash_log_dir);

        // Remove the platform crash handler and restore default OS behavior.
        // Call at engine shutdown. Safe to call if Install() was never called.
        static void              Uninstall();

        // Set a callback to be called just before the crash dump is written.
        // This is useful for flushing logs, saving game state, etc.
        //
        // The callback must complete within a defined timeout (default 2 seconds) or the crash handler will proceed to write the dump anyway.
        //      On Windows, a helper thread's timeout fires
        //      On Linux, macOS SIGALRM is raised on the callback thread
        //
        // Only one callback can be set at a time. Setting a new callback replaces the previous one.
        // It must not allocate memory on the ZEngine heap, or call ZENGINE_VALIDATE_ASSERT.
        static void              SetPreCrashCallback(PreCrashFn fn, void* ctx = nullptr);

        // Called by the platform handler (SEH filter on Windows, signal handler
        // on POSIX) to perform the cross-platform crash response.
        //   signal_or_exception — human-readable description, e.g.
        //                         "Access Violation at 0x0000000000000000"
        //   context             — platform-specific context:
        //                         Windows: EXCEPTION_POINTERS*
        //                         POSIX:   ucontext_t*
        //
        // This function does not return under normal conditions.
        [[noreturn]] static void OnCrash(const char* signal_or_exception, void* ctx = nullptr);

        // Called by ZENGINE_VALIDATE_ASSERT to produce a crash report for
        // assertion failures in Release/RelWithDebInfo builds.
        //   file    — __FILE__
        //   line    — __LINE__
        //   message — the assertion condition and user message string
        [[noreturn]] static void OnAssertionFailure(const char* file, int line, const char* message);

    private:
        CrashHandler() = delete;

        static void StoreMetadata(const char* app_name, const char* version, const char* crash_log_dir);
    };
} // namespace ZEngine::CrashHandlers