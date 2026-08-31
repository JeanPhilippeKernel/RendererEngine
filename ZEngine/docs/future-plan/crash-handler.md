# ZEngine — Crash Handler

**Priority:** P3 — Required for production stability and crash diagnosis
**Status:** Design
**Depends on:** Nothing (standalone system)
**Blocks:** Nothing critical, but shipped games need crash reports

---

## Table of Contents

1. [Goals](#1-goals)
2. [Platform Crash Handlers](#2-platform-crash-handlers)
3. [CrashHandler Class Declaration](#3-crashhandler-class-declaration)
4. [Crash Log Format](#4-crash-log-format)
5. [Minidump — Windows Only](#5-minidump--windows-only)
6. [User-Facing Dialog](#6-user-facing-dialog)
7. [Pre-Crash Callback](#7-pre-crash-callback)
8. [Crash Upload (Optional)](#8-crash-upload-optional)
9. [Integration with ZENGINE_VALIDATE_ASSERT](#9-integration-with-zengine_validate_assert)
10. [Build Configuration](#10-build-configuration)
11. [Symbol Upload for Minidumps](#11-symbol-upload-for-minidumps)
12. [File Layout](#12-file-layout)
13. [Deliverables Checklist](#13-deliverables-checklist)

---

## 1. Goals

The crash handler is a standalone, zero-dependency subsystem that activates when the engine process terminates abnormally. Its responsibilities are:

- **Capture a stack trace** at the moment of the crash, across all platforms, regardless of which thread caused the fault.
- **Write a minidump** (Windows) or a structured text crash log (Linux, macOS) to a configurable directory on disk before the process terminates.
- **Show a user-facing dialog** summarizing the crash and the path of the saved report so the player knows where to find it.
- **Optionally upload** the crash log and minidump to a developer-controlled HTTP endpoint for aggregation and triage (guarded by user consent and the `ZENGINE_CRASH_UPLOAD` compile-time flag).
- **Invoke a pre-crash callback** registered by game code (e.g., flush save data to disk) before writing the dump, bounded by a hard timeout so a hung callback cannot prevent the report from being written.

**Design constraints:**

- The crash handler must work even if the engine heap is corrupted. It allocates its own internal stack buffers using fixed-size arrays; it never calls `new`, `malloc`, or `Core::Memory::ArenaAllocator`.
- It must be installed before the first engine subsystem initialises and survive `CrashHandler::Uninstall()` being called at shutdown.
- It is compiled and active in **Release** and **RelWithDebInfo** builds only. Debug builds leave crashes unhandled so the attached debugger intercepts them first.
- All public API is `static`. There is no `CrashHandler` instance and no constructor.
- The handler does not depend on any other ZEngine header. Its only system-level dependencies are the OS crash-capture APIs (DbgHelp on Windows, `execinfo.h`/`signal.h` on POSIX).

---

## 2. Platform Crash Handlers

### 2.1 Windows — Structured Exception Handling + DbgHelp

#### Handler registration

Windows delivers fatal exceptions through the Win32 Structured Exception Handling (SEH) mechanism. The crash handler registers a top-level filter using `SetUnhandledExceptionFilter`. This filter is invoked for any exception that propagates past all application-level `__try`/`__except` blocks, including access violations, illegal instructions, and stack overflows.

```cpp
// CrashHandlerWindows.cpp  (excerpt)
#include <Windows.h>
#include <DbgHelp.h>

static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exception_info)
{
    // 1. Collect exception metadata from the EXCEPTION_RECORD.
    const DWORD code = exception_info->ExceptionRecord->ExceptionCode;
    const ULONG_PTR crash_addr =
        (code == EXCEPTION_ACCESS_VIOLATION)
        ? exception_info->ExceptionRecord->ExceptionInformation[1]
        : 0;

    // 2. Build a human-readable exception name (stack-allocated).
    char exception_name[128] = {};
    ExceptionCodeToString(code, crash_addr, exception_name, sizeof(exception_name));

    // 3. Call the engine-level crash coordinator.
    CrashHandler::OnCrash(exception_name, static_cast<void*>(exception_info));

    // 4. Return EXCEPTION_EXECUTE_HANDLER to allow the OS to terminate cleanly.
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::Install(cstring app_name, cstring version, cstring crash_log_dir)
{
    // Store metadata in static storage (no heap allocation).
    StoreMetadata(app_name, version, crash_log_dir);

    // Replace any existing filter.
    SetUnhandledExceptionFilter(UnhandledExceptionFilter);

    // Also catch pure virtual call failures and abort().
    _set_purecall_handler(PureCallHandler);
    _set_invalid_parameter_handler(InvalidParameterHandler);
    signal(SIGABRT, SigAbrtHandler);
}
```

`ExceptionCodeToString` maps common NTSTATUS codes to readable strings:

| Code | String |
|------|--------|
| `EXCEPTION_ACCESS_VIOLATION` | `"Access Violation at 0x..."` |
| `EXCEPTION_STACK_OVERFLOW` | `"Stack Overflow"` |
| `EXCEPTION_ILLEGAL_INSTRUCTION` | `"Illegal Instruction"` |
| `EXCEPTION_INT_DIVIDE_BY_ZERO` | `"Integer Divide by Zero"` |
| `EXCEPTION_FLT_DIVIDE_BY_ZERO` | `"Float Divide by Zero"` |
| `EXCEPTION_IN_PAGE_ERROR` | `"In-Page Error"` |

#### Writing a minidump with MiniDumpWriteDump

`MiniDumpWriteDump` is the standard Win32 API for writing a `.dmp` file that WinDbg and Visual Studio can open for post-mortem analysis. It is provided by `DbgHelp.dll`, which ships with all Windows versions and is also redistributable.

```cpp
// CrashHandlerWindows.cpp — WriteMiniDump()
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")

static void WriteMiniDump(EXCEPTION_POINTERS* exception_info, cstring output_path)
{
    // Open (or create) the output file.
    HANDLE file = CreateFileA(
        output_path,
        GENERIC_WRITE,
        0,                   // no sharing
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        // Primary dump path failed — try a fallback in the temp directory.
        char temp_path[MAX_PATH];
        GetTempPathA(MAX_PATH, temp_path);
        strncat_s(temp_path, "zengine_crash.dmp", _TRUNCATE);
        file = CreateFileA(temp_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            // Both paths failed — log to stderr and continue to show dialog.
            fputs("[CrashHandler] ERROR: Cannot write minidump. Disk full or permission denied.\n", stderr);
            return;
        }
    }

    // Describe the exception context to MiniDumpWriteDump.
    MINIDUMP_EXCEPTION_INFORMATION mei = {};
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = exception_info;
    mei.ClientPointers    = FALSE;  // pointers are in our address space

    // MiniDumpWithFullMemory writes the entire virtual address space of
    // the process. This produces large dumps (~hundreds of MB) but allows
    // reconstruction of any heap or stack variable in the debugger.
    // For smaller dumps use MiniDumpWithDataSegs | MiniDumpWithHandleData.
    const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory           |
        MiniDumpWithFullMemoryInfo       |
        MiniDumpWithHandleData           |
        MiniDumpWithUnloadedModules      |
        MiniDumpWithProcessThreadData
    );

    MiniDumpWriteDump(
        GetCurrentProcess(),   // process handle
        GetCurrentProcessId(), // process id
        file,                  // output file handle
        dump_type,
        &mei,                  // exception info
        nullptr,               // user streams (none)
        nullptr                // callback (none)
    );

    CloseHandle(file);
}
```

**Important:** `MiniDumpWriteDump` must be called on the faulting thread (same thread that received the exception), because the EXCEPTION_POINTERS structure contains a thread context record specific to that thread. Calling it from a worker thread produces incomplete context.

Stack overflow is a special case: the faulting thread's stack is exhausted, so `MiniDumpWriteDump` cannot run on that thread. Detect stack overflow (`ExceptionCode == EXCEPTION_STACK_OVERFLOW`) and spawn a worker thread using `CreateThread` with a dedicated stack for the dump call:

```cpp
// Minimum stack for the crash worker thread.
// MiniDumpWriteDump with MiniDumpWithFullMemory requires significant stack space
// (~100-200 KB observed in practice). 64 KB is insufficient.
// Use 256 KB as a safe minimum.
static constexpr size_t kCrashWorkerStackSize = 256 * 1024;  // 256 KB minimum
```

---

### 2.2 Linux — POSIX Signal Handlers + backtrace()

Linux delivers fatal errors as POSIX signals. The crash handler registers `SA_SIGACTION` handlers (not `signal()`, which is less portable) for the signals that indicate programmer errors:

| Signal | Cause |
|--------|-------|
| `SIGSEGV` | Segmentation fault (null dereference, unmapped memory access) |
| `SIGABRT` | `abort()` called — fired by `ZENGINE_VALIDATE_ASSERT` in some modes |
| `SIGFPE` | Floating-point or integer arithmetic exception |
| `SIGILL` | Illegal instruction |
| `SIGBUS` | Bus error (unaligned access on some architectures) |

```cpp
// CrashHandlerLinux.cpp
#include <signal.h>
#include <execinfo.h>   // backtrace(), backtrace_symbols()
#include <unistd.h>
#include <sys/ucontext.h>

static void SignalHandler(int sig, siginfo_t* info, void* ucontext_ptr)
{
    // We are inside a signal handler. We may only call async-signal-safe
    // functions here. write() is safe; printf() is NOT.
    //
    // Strategy: collect the stack trace into a static buffer, then call
    // our crash coordinator which writes the log file using low-level
    // write() calls.

    const char* signal_name = SignalToString(sig);
    CrashHandler::OnCrash(signal_name, ucontext_ptr);

    // Restore the default signal disposition and re-raise so the OS can
    // generate a core dump if ulimit -c allows it.
    struct sigaction sa = {};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    raise(sig);
}

void CrashHandler::Install(cstring app_name, cstring version, cstring crash_log_dir)
{
    StoreMetadata(app_name, version, crash_log_dir);

    struct sigaction sa = {};
    sa.sa_sigaction = SignalHandler;
    sigemptyset(&sa.sa_mask);
    // SA_SIGINFO: pass siginfo_t and ucontext to handler.
    // SA_RESETHAND: auto-restore default handler after first invocation.
    // SA_ONSTACK: run handler on the alternate signal stack (required for
    //             SIGSEGV when the main stack is also corrupted).
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;

    // Set up an alternate signal stack so SIGSEGV from stack overflow
    // can still run the handler.
    static uint8_t alt_stack_storage[SIGSTKSZ * 4];
    stack_t alt_stack = {};
    alt_stack.ss_sp    = alt_stack_storage;
    alt_stack.ss_size  = sizeof(alt_stack_storage);
    alt_stack.ss_flags = 0;
    sigaltstack(&alt_stack, nullptr);

    const int signals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
    for (int s : signals)
        sigaction(s, &sa, nullptr);
}
```

#### Stack trace via backtrace()

`backtrace()` walks the call stack and fills an array of return addresses. `backtrace_symbols()` resolves those addresses to function names using the dynamic symbol table. For names to be useful, the binary must be compiled with `-rdynamic` (exports all symbols) or with debug info.

```cpp
static void CollectStackTrace(
    char* out_buf,
    size_t out_buf_size,
    void* ucontext_ptr)
{
    // Capture up to 64 stack frames.
    void* frames[64] = {};
    const int frame_count = backtrace(frames, 64);

    // If we have a ucontext, patch frame[1] with the faulting PC so the
    // top of the trace is the actual crash site, not inside the handler.
    if (ucontext_ptr != nullptr) {
        const auto* uc = static_cast<ucontext_t*>(ucontext_ptr);
#if defined(__x86_64__)
        frames[1] = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
        frames[1] = reinterpret_cast<void*>(uc->uc_mcontext.pc);
#endif
    }

    // Resolve addresses to symbols (heap allocation inside backtrace_symbols
    // is unavoidable here; if the heap is corrupted this call may fail).
    char** symbols = backtrace_symbols(frames, frame_count);

    // Format into the output buffer using async-signal-safe string ops.
    size_t offset = 0;
    for (int i = 0; i < frame_count && offset < out_buf_size - 1; ++i) {
        const char* sym = (symbols != nullptr) ? symbols[i] : "<unknown>";
        // Write "#N  <sym>\n" manually (no snprintf in signal handler).
        WriteFrameLine(out_buf, &offset, out_buf_size, i, sym);
    }

    // symbols[] was malloc'd by the runtime; free it only if safe.
    if (symbols != nullptr)
        free(symbols);
}
```

`addr2line` (invoked as a child process via `fork`+`execve`, both async-signal-safe) can further demangle C++ names and resolve source file + line numbers from DWARF info. This is optional and significantly increases crash handler complexity; include it only in RelWithDebInfo builds.

---

### 2.3 macOS — POSIX Signals + Mach Exception Ports

macOS signals work identically to Linux for crash capture purposes. The same `sigaction`-based registration applies. However, macOS has an additional low-level mechanism: **Mach exception ports**. The kernel delivers exceptions as Mach IPC messages before converting them to POSIX signals. Setting a Mach exception port allows a crash handler to intercept crashes that would otherwise be silently ignored by `signal()`.

For shipping ZEngine games the `sigaction` approach is sufficient. Mach exception ports are documented here for completeness and for future integration of PLCrashReporter.

```cpp
// CrashHandlerMacOS.cpp
// Signal handler registration is identical to Linux.
// The macOS-specific addition is the stack trace symbolication approach.

#include <execinfo.h>   // same API as Linux
#include <dlfcn.h>      // dladdr() for symbol resolution

static void CollectStackTrace(char* out_buf, size_t out_buf_size, void* /*ucontext*/)
{
    void* frames[64] = {};
    const int frame_count = backtrace(frames, 64);
    char** symbols = backtrace_symbols(frames, frame_count);

    size_t offset = 0;
    for (int i = 0; i < frame_count && offset < out_buf_size - 1; ++i) {
        // Use dladdr() for richer symbol info (includes image name and
        // nearest symbol address for demangling).
        Dl_info di = {};
        if (dladdr(frames[i], &di) != 0 && di.dli_sname != nullptr) {
            // Demangle C++ name using __cxa_demangle (available on macOS).
            int status = 0;
            char* demangled = __cxxabiv1::__cxa_demangle(
                di.dli_sname, nullptr, nullptr, &status);
            const char* name = (status == 0 && demangled) ? demangled : di.dli_sname;
            WriteFrameLine(out_buf, &offset, out_buf_size, i, name);
            if (demangled) free(demangled);
        } else {
            const char* sym = (symbols != nullptr) ? symbols[i] : "<unknown>";
            WriteFrameLine(out_buf, &offset, out_buf_size, i, sym);
        }
    }

    if (symbols) free(symbols);
}
```

#### PLCrashReporter (optional)

[PLCrashReporter](https://github.com/microsoft/plcrashreporter) (MIT license, maintained by Microsoft / AppCenter) provides Mach exception port integration, thread-safe crash capture, and binary `.plcrash` files that can be symbolicated offline. It is the recommended upgrade path for the macOS crash handler once the basic `sigaction` handler is working. Integration: add as a git submodule at `__externals/PLCrashReporter`, build as a static library, link with `-framework Foundation`.

---

## 3. CrashHandler Class Declaration

The full public API surface. All methods are `static`; there is no instance. The implementation files are platform-selected by CMake.

```cpp
// ZEngine/CrashHandler/CrashHandler.h
#pragma once

#include <functional>
#include "Core/Defines.h"   // cstring, ZENGINE_API

namespace ZEngine {

struct CrashHandler {
    // Lifecycle

    // Install the platform crash handler.
    //   app_name      — e.g. "GameName"
    //   version       — e.g. "1.0.0"
    //   crash_log_dir — directory where crash logs and .dmp files are written.
    //                   Created if it does not exist.
    //
    // Call once, before any engine subsystem is initialized.
    // No-op if already installed.
    static void Install(cstring app_name, cstring version, cstring crash_log_dir);

    // Remove the platform crash handler and restore default OS behavior.
    // Call at engine shutdown. Safe to call if Install() was never called.
    static void Uninstall();

    // Crash event

    // Called by the platform handler (SEH filter on Windows, signal handler
    // on POSIX) to perform the cross-platform crash response:
    //   1. Invoke the pre-crash callback (with timeout).
    //   2. Collect the stack trace.
    //   3. Write the crash log file.
    //   4. Write the minidump (Windows only).
    //   5. Show the user-facing dialog.
    //   6. Optionally upload the report.
    //
    //   signal_or_exception — human-readable description, e.g.
    //                         "Access Violation at 0x0000000000000000"
    //   context             — platform-specific context:
    //                         Windows: EXCEPTION_POINTERS*
    //                         POSIX:   ucontext_t*
    //
    // This function does not return under normal conditions.
    [[noreturn]] static void OnCrash(cstring signal_or_exception, void* context);

    // Implementation note — first lines of OnCrash must include reentry protection:
    //
    //     // Reentry protection: if the crash handler itself crashes, prevent infinite recursion.
    //     static std::atomic<bool> s_in_crash_handler{false};
    //     if (s_in_crash_handler.exchange(true, std::memory_order_acq_rel)) {
    //         // Recursive crash — abort immediately with a minimal message.
    //         fputs("[CrashHandler] FATAL: crash handler reentered. Aborting.\n", stderr);
    //         _Exit(EXIT_FAILURE);
    //     }

    // Pre-crash callback

    // Register a function to be called before the crash dump is written.
    // Intended for game-side emergency saves, network disconnects, etc.
    //
    // The callback must complete within the configured timeout (default 2 s).
    // If it does not return in time it is forcibly terminated:
    //   POSIX   — SIGALRM is raised on the callback thread.
    //   Windows — the helper thread's timeout fires.
    //
    // Only one callback can be registered at a time. Calling this a second
    // time replaces the previous registration.
    //
    // The callback is called with no arguments and returns void.
    // It must not allocate on the engine heap or call ZENGINE_VALIDATE_ASSERT.
    // Plain function pointer + context — no std::function, no heap allocation.
    // The crash handler must not allocate; std::function's capture buffer may heap-allocate.
    using PreCrashFn = void (*)(void* ctx);
    static void SetPreCrashCallback(PreCrashFn fn, void* ctx = nullptr);

    // Internal use

    // Called by ZENGINE_VALIDATE_ASSERT to produce a crash report for
    // assertion failures in Release/RelWithDebInfo builds.
    //   file    — __FILE__
    //   line    — __LINE__
    //   message — the assertion condition and user message string
    [[noreturn]] static void OnAssertionFailure(
        cstring file,
        int     line,
        cstring message);

private:
    CrashHandler() = delete;
};

} // namespace ZEngine
```

The internal state lives in an anonymous namespace within the `.cpp` files:

```cpp
// CrashHandlerInternal.h — included only by CrashHandler*.cpp
namespace {

constexpr size_t kMaxPathLen    = 512;
constexpr size_t kMaxTraceLen   = 8192;
constexpr size_t kMaxNameLen    = 128;
constexpr int    kCallbackTimeoutSec = 2;

// Static storage — never heap-allocated.
struct CrashHandlerState {
    char app_name     [kMaxNameLen]  = {};
    char version      [kMaxNameLen]  = {};
    char crash_log_dir[kMaxPathLen]  = {};
    bool installed                   = false;

    // Pre-crash callback — plain function pointer + context, never heap-allocated.
    CrashHandler::PreCrashFn pre_crash_fn  = nullptr;
    void*                    pre_crash_ctx = nullptr;
};

static CrashHandlerState g_state;

} // anonymous namespace
```

---

## 4. Crash Log Format

The crash log is a plain-text UTF-8 file. The filename encodes the timestamp: `crash_YYYY-MM-DD_HH-MM-SS.log`. The directory is the `crash_log_dir` passed to `Install()`.

A representative crash log looks like this:

```
================================================================================
ZEngine Crash Report
================================================================================
App:       GameName v1.0.0
Date:      2026-06-25 14:32:11 UTC
OS:        Windows 11 22H2 (Build 22621)
Arch:      x86_64
CPU:       Intel Core i9-13900K (24 cores)
Exception: Access Violation reading address 0x0000000000000000

================================================================================
Stack Trace (most recent call first):
================================================================================
  #0   ZEngine::ECS::Scene::ForEach<PhysicsComponent>
         Scene.cpp:142
  #1   PhysicsStepSystem::OnUpdate(float)
         PhysicsStepSystem.cpp:67
  #2   ZEngine::SystemScheduler::RunGroup(SystemGroup)
         SystemScheduler.cpp:231
  #3   ZEngine::GameLoop::FixedUpdate(double)
         GameLoop.cpp:88
  #4   ZEngine::GameLoop::Run()
         GameLoop.cpp:44
  #5   WinMain
         Main.cpp:23
  #6   __tmainCRTStartup
         <CRT>
  #7   BaseThreadInitThunk
         <ntdll>
  #8   RtlUserThreadStart
         <ntdll>

================================================================================
Memory Arenas:
================================================================================
  Arena/Main:       512 MB used /  2048 MB reserved
  Arena/ECS:         12 MB used /   256 MB reserved
  Arena/Render:      88 MB used /   512 MB reserved
  Arena/Audio:        4 MB used /    64 MB reserved

================================================================================
Modules:
================================================================================
  ZRuntime.exe          1.0.0.0  0x0000000140000000
  ZEngine.dll           1.0.0.0  0x00007FF800000000
  vulkan-1.dll          1.3.261  0x00007FF7A0000000
  steam_api64.dll      1.57.51   0x00007FF6C0000000

================================================================================
End of Report
================================================================================
```

**Implementation notes:**

- The log is written using low-level `write()` (POSIX) or `WriteFile()` (Windows) system calls so it works even if the C runtime heap is corrupted.
- All formatting uses `snprintf` into fixed-size stack buffers then `write()`/`WriteFile()`. No `std::string`, no `printf`, no `cout`.
- The timestamp is obtained from `time()` + `gmtime_r()` (POSIX) or `GetSystemTimeAsFileTime()` (Windows) — both signal-safe.
- Memory arena stats are collected before calling `OnCrash` if the engine's `ArenaAllocator` exposes a `DumpStats(char* buf, size_t size)` static method. If the allocator state is corrupted, skip this section silently.
- Module list is collected from `EnumerateLoadedModules64` (Windows) or by reading `/proc/self/maps` (Linux) or `_dyld_get_image_name()` (macOS).

---

## 5. Minidump — Windows Only

### Writing the dump

The dump file is named `crash_YYYY-MM-DD_HH-MM-SS.dmp` and placed in the same directory as the `.log` file. It is written by `WriteMiniDump()` (see Section 2.1) immediately after the crash log.

For release builds where disk space matters, use a smaller dump type:

```cpp
// Release builds: smaller dump, still very useful.
const MINIDUMP_TYPE release_dump_type = static_cast<MINIDUMP_TYPE>(
    MiniDumpWithDataSegs        |   // global data
    MiniDumpWithHandleData      |   // open handles
    MiniDumpWithUnloadedModules |   // recently unloaded modules
    MiniDumpWithIndirectlyReferencedMemory  // heap objects pointed to by stack
);

// RelWithDebInfo builds: full memory dump for maximum debuggability.
const MINIDUMP_TYPE reldbg_dump_type = static_cast<MINIDUMP_TYPE>(
    MiniDumpWithFullMemory          |
    MiniDumpWithFullMemoryInfo      |
    MiniDumpWithHandleData          |
    MiniDumpWithUnloadedModules     |
    MiniDumpWithProcessThreadData
);
```

### Opening in WinDbg

1. Launch WinDbg (available in the Windows SDK or via the Microsoft Store as "WinDbg Preview").
2. **File → Open Crash Dump** → select the `.dmp` file.
3. Set the symbol path to your symbol server: `.sympath srv*C:\Symbols*https://symbols.yourgame.com/symbols;srv*C:\Symbols*https://msdl.microsoft.com/download/symbols`
4. Type `!analyze -v` in the command window to get an automated crash analysis.
5. `k` or `kb` shows the call stack. `dv` shows local variables. `!heap -stat` shows heap summary.

### Opening in Visual Studio

1. **File → Open → File** → select the `.dmp` file.
2. Visual Studio opens the **Minidump Summary** page. Click **Debug with Native Only**.
3. Visual Studio will attempt to match PDB files from the executable directory or a configured symbol server.
4. Once loaded, use the call stack window, locals, and watch windows as in a live debug session.

### PDB matching

The minidump embeds the module's CodeView debug directory, which records the PDB GUID and age. Both WinDbg and Visual Studio use this to locate the correct PDB on the symbol server. If the PDB matches, full source-level debugging is available from the dump.

---

## 6. User-Facing Dialog

After writing the crash log (and minidump on Windows), the handler shows a native dialog. The dialog must not depend on the engine's UI system — it uses raw OS APIs.

### 6.1 Windows — MessageBoxW

```cpp
static void ShowCrashDialog_Windows(cstring log_path)
{
    // Build the message using wide strings (required for MessageBoxW).
    wchar_t message[1024] = {};
    wchar_t path_w[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, log_path, -1, path_w, 512);

    _snwprintf_s(message, _countof(message), _TRUNCATE,
        L"The application has crashed.\n\n"
        L"A crash report has been saved to:\n%s\n\n"
        L"Would you like to send this report to the developers?",
        path_w);

    // MB_YESNO produces "Yes" / "No" buttons.
    // IDYES = user consents to upload.
    const int result = MessageBoxW(
        nullptr,
        message,
        L"Crash Report",
        MB_YESNO | MB_ICONERROR | MB_TASKMODAL | MB_TOPMOST);

    if (result == IDYES)
        g_state.user_consented_upload = true;
}
```

`MB_TASKMODAL` ensures the dialog appears even if no parent window exists (which is the case during a crash). `MB_TOPMOST` prevents the dialog from being hidden behind other windows.

### 6.2 Linux — stderr + Optional GTK Dialog

GTK may not be available on all Linux systems. The crash handler first writes to stderr (always available), then attempts a GTK dialog by dynamically loading `libgtk-3.so` via `dlopen`:

```cpp
static void ShowCrashDialog_Linux(cstring log_path)
{
    // Always write to stderr (async-signal-safe).
    const char prefix[] = "\n[CRASH] The application has crashed.\n"
                          "[CRASH] Crash report: ";
    write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    write(STDERR_FILENO, log_path, strlen(log_path));
    write(STDERR_FILENO, "\n", 1);

    // Attempt GTK dialog (best-effort, non-fatal if not available).
    void* gtk = dlopen("libgtk-3.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (gtk == nullptr)
        gtk = dlopen("libgtk-4.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (gtk == nullptr)
        return; // no GTK, stderr is sufficient

    // Resolve GTK symbols dynamically so we don't link against GTK at compile time.
    using gtk_init_fn        = void(*)(int*, char***);
    using gtk_message_fn     = void*(*)(void*, int, int, int, int, const char*, ...);
    using gtk_dialog_run_fn  = int(*)(void*);
    using gtk_widget_destroy = void(*)(void*);

    auto gtk_init         = (gtk_init_fn)       dlsym(gtk, "gtk_init");
    auto gtk_message_new  = (gtk_message_fn)    dlsym(gtk, "gtk_message_dialog_new");
    auto gtk_dialog_run   = (gtk_dialog_run_fn) dlsym(gtk, "gtk_dialog_run");
    auto gtk_widget_destr = (gtk_widget_destroy)dlsym(gtk, "gtk_widget_destroy");

    if (!gtk_init || !gtk_message_new || !gtk_dialog_run || !gtk_widget_destr) {
        ZENGINE_CORE_WARN("CrashHandler: GTK symbols not found — skipping dialog");
        dlclose(gtk);
        return;
    }

    if (gtk_init && gtk_message_new && gtk_dialog_run && gtk_widget_destr) {
        gtk_init(nullptr, nullptr);
        char msg[1024];
        snprintf(msg, sizeof(msg),
            "The application has crashed.\n\nCrash report saved to:\n%s\n\n"
            "Send report to developers?", log_path);
        void* dialog = gtk_message_new(
            nullptr,           // no parent window
            4,                 // GTK_DIALOG_MODAL
            0,                 // GTK_MESSAGE_ERROR
            3,                 // GTK_BUTTONS_YES_NO
            "%s", msg);
        const int response = gtk_dialog_run(dialog);
        if (response == -8)    // GTK_RESPONSE_YES
            g_state.user_consented_upload = true;
        gtk_widget_destr(dialog);
    }

    dlclose(gtk);
}
```

### 6.3 macOS — NSAlert via Objective-C Runtime

macOS requires AppKit for a native dialog. Rather than importing `<AppKit/AppKit.h>` (which would add an Objective-C compile dependency), the crash handler uses the Objective-C runtime API directly from C++:

```cpp
// CrashHandlerMacOS.cpp
#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>

// Type alias for objc_msgSend with specific return types.
using objc_msg_id  = id(*)(id, SEL, ...);
using objc_msg_str = id(*)(id, SEL, id);

static void ShowCrashDialog_macOS(cstring log_path)
{
    // Resolve classes and selectors at runtime — no AppKit header needed.
    Class NSString_class    = objc_getClass("NSString");
    Class NSAlert_class     = objc_getClass("NSAlert");
    SEL   alloc_sel         = sel_registerName("alloc");
    SEL   init_sel          = sel_registerName("init");
    SEL   addButton_sel     = sel_registerName("addButtonWithTitle:");
    SEL   setMsg_sel        = sel_registerName("setMessageText:");
    SEL   setInfo_sel       = sel_registerName("setInformativeText:");
    SEL   runModal_sel      = sel_registerName("runModal");
    SEL   str_sel           = sel_registerName("stringWithUTF8String:");
    SEL   setStyle_sel      = sel_registerName("setAlertStyle:");

    auto msgSend = reinterpret_cast<objc_msg_id>(objc_msgSend);

    // Build NSStrings.
    id title    = msgSend((id)NSString_class, str_sel, "Application Crashed");
    char info_buf[1024];
    snprintf(info_buf, sizeof(info_buf),
        "The application has crashed. A crash report has been saved to:\n\n"
        "%s\n\nWould you like to send this report to the developers?", log_path);
    id info_str = msgSend((id)NSString_class, str_sel, info_buf);

    // Create NSAlert.
    id alert = msgSend(msgSend((id)NSAlert_class, alloc_sel), init_sel);

    // NSAlertStyleCritical = 2
    msgSend(alert, setStyle_sel, (id)(uintptr_t)2);
    msgSend(alert, setMsg_sel,  title);
    msgSend(alert, setInfo_sel, info_str);

    // Add buttons. NSAlert returns buttons in reverse order of addition;
    // first added = default (Return key) = NSAlertFirstButtonReturn (1000).
    id btn_send = msgSend((id)NSString_class, str_sel, "Send Report");
    id btn_no   = msgSend((id)NSString_class, str_sel, "No Thanks");
    msgSend(alert, addButton_sel, btn_send);
    msgSend(alert, addButton_sel, btn_no);

    // NSAlertFirstButtonReturn = 1000 ("Send Report")
    const long response = (long)msgSend(alert, runModal_sel);
    if (response == 1000)
        g_state.user_consented_upload = true;
}
```

This approach compiles as pure C++ with no Objective-C translation unit requirements. The only link dependencies are `libobjc.dylib` and the Objective-C runtime (`-lobjc`), which are always present on macOS.

---

## 7. Pre-Crash Callback

The game can register a single callback that runs before the crash dump is written. Its purpose is to perform emergency persistence operations: flush the save game buffer, disconnect from a multiplayer session cleanly, flush analytics events, etc.

```cpp
// Game-side registration (called during engine init):
ZEngine::CrashHandler::SetPreCrashCallback([]() {
    SaveManager::Get().EmergencySave();
    NetworkManager::Get().DisconnectGracefully();
    AnalyticsClient::Get().FlushQueue();
});
```

The callback is bounded by a **hard 2-second timeout**. If it does not return within the deadline, it is forcibly terminated and the crash handler continues with dump writing.

### 7.1 Windows — Timer-Based Timeout

```cpp
static void InvokePreCrashCallback_Windows()
{
    if (!g_state.pre_crash_callback)
        return;

    // Spawn a worker thread that runs the callback.
    struct ThreadData { std::function<void()>* fn; bool done; };
    static ThreadData td = { &g_state.pre_crash_callback, false };

    HANDLE thread = CreateThread(
        nullptr, 0,
        [](LPVOID param) -> DWORD {
            auto* data = static_cast<ThreadData*>(param);
            (*data->fn)();
            data->done = true;
            return 0;
        },
        &td, 0, nullptr);

    if (thread == nullptr)
        return;

    // Wait up to kCallbackTimeoutMs milliseconds.
    const DWORD timeout_ms = kCallbackTimeoutSec * 1000;
    WaitForSingleObject(thread, timeout_ms);

    if (!td.done) {
        // Callback timed out. Terminate the thread forcibly.
        // TerminateThread is generally unsafe but acceptable here because
        // we are about to terminate the entire process anyway.
        TerminateThread(thread, 0);
        // Log the timeout in the crash report.
        g_state.callback_timed_out = true;
    }

    CloseHandle(thread);
}
```

### 7.2 POSIX — SIGALRM Timeout

```cpp
static void InvokePreCrashCallback_POSIX()
{
    if (!g_state.pre_crash_callback)
        return;

    // Set an alarm: SIGALRM fires if the callback doesn't finish in time.
    // Our SIGALRM handler just sets a flag; the crash handler then continues.
    struct sigaction sa = {};
    sa.sa_handler = [](int) { g_state.callback_timed_out = true; };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr);

    alarm(kCallbackTimeoutSec);

    g_state.pre_crash_callback();

    // Cancel the alarm if the callback returned in time.
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}
```

**Contract with game code:** The callback must not:
- Call `ZENGINE_VALIDATE_ASSERT` (would recurse into the crash handler).
- Allocate on the engine arena heap (may be corrupted).
- Block indefinitely on a mutex (could deadlock if the crash occurred while holding that mutex).

The callback should operate only on pre-allocated, lock-free structures such as a ring buffer or a memory-mapped file.

---

## 8. Crash Upload (Optional)

Upload is gated behind two conditions:
1. The `ZENGINE_CRASH_UPLOAD` compile-time define must be set (off by default, enabled in Release builds).
2. The user must have consented via the crash dialog (see Section 6).

The upload endpoint is compiled in via a CMake define: `-DZENGINE_CRASH_ENDPOINT="https://crashes.yourgame.com/ingest"`.

### 8.1 Implementation

On Windows, use `WinHTTP` (ships with Windows, no extra DLL). On Linux/macOS, use `libcurl` (dynamically loaded via `dlopen` to avoid a hard runtime dependency).

```cpp
#if defined(ZENGINE_CRASH_UPLOAD)
static void UploadCrashReport(cstring log_path, cstring dmp_path)
{
    if (!g_state.user_consented_upload)
        return;

    const char* endpoint = ZENGINE_CRASH_ENDPOINT;

#if defined(ZENGINE_PLATFORM_WINDOWS)
    UploadCrashReport_WinHTTP(log_path, dmp_path, endpoint);
#else
    UploadCrashReport_Curl(log_path, dmp_path, endpoint);
#endif
}
#endif // ZENGINE_CRASH_UPLOAD
```

**Windows WinHTTP path** (abbreviated):

```cpp
static void UploadCrashReport_WinHTTP(
    cstring log_path, cstring dmp_path, cstring endpoint)
{
    HINTERNET session = WinHttpOpen(
        L"ZEngine/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    // Parse the endpoint URL, create connection and request handles,
    // send multipart/form-data POST with the log and dmp files as attachments.
    // Full implementation follows standard WinHTTP multipart POST pattern.
    // TLS is automatic when the URL scheme is https://.
    // ...

    WinHttpCloseHandle(session);
}
```

**libcurl path** (Linux/macOS, dynamically loaded):

```cpp
static void UploadCrashReport_Curl(
    cstring log_path, cstring dmp_path, cstring endpoint)
{
    // Dynamically load libcurl to avoid a hard dependency.
    void* curl_lib = dlopen("libcurl.so.4", RTLD_LAZY | RTLD_LOCAL);
    if (!curl_lib)
        curl_lib = dlopen("libcurl.dylib", RTLD_LAZY | RTLD_LOCAL); // macOS

    if (!curl_lib)
        return; // curl not available; silently skip upload

    // Resolve curl_easy_init, curl_easy_setopt, curl_mime_init, etc.
    // Build a multipart POST with the log file and optional .dmp.
    // Set CURLOPT_SSL_VERIFYPEER = 1 (TLS verification always on).
    // Set CURLOPT_TIMEOUT = 30 (seconds).
    // ...

    dlclose(curl_lib);
}
```

The upload is fire-and-forget. If it fails (network unavailable, server error), the crash handler logs the failure to the existing crash log file and terminates normally. Upload failure must never prevent the process from exiting or cause a secondary crash.

---

## 9. Integration with ZENGINE_VALIDATE_ASSERT

In Release and RelWithDebInfo builds, assertion failures should produce the same crash log as a null dereference. This makes assertion failures diagnosable even from crash reports submitted by players.

Modify `ZENGINE_VALIDATE_ASSERT` in `Core/Defines.h`:

```cpp
// Before (Debug-only behavior):
#define ZENGINE_VALIDATE_ASSERT(cond, msg)          \
    do {                                             \
        if (!(cond)) {                               \
            ZENGINE_CORE_CRITICAL(msg);              \
            ZENGINE_DEBUG_BREAK();                   \
        }                                            \
    } while (false)

// After (Release behavior includes crash handler):
#if defined(NDEBUG) || defined(ZENGINE_RELWITHDEBINFO)
    #define ZENGINE_VALIDATE_ASSERT(cond, msg)              \
        do {                                                 \
            if (!(cond)) [[unlikely]] {                      \
                /* Format a string describing the failure */ \
                char _assert_buf[512];                       \
                snprintf(_assert_buf, sizeof(_assert_buf),   \
                    "Assertion failed: (%s)\n"               \
                    "  Message: %s\n"                        \
                    "  File:    %s\n"                        \
                    "  Line:    %d",                         \
                    #cond, (msg), __FILE__, __LINE__);       \
                ZEngine::CrashHandler::OnAssertionFailure(   \
                    __FILE__, __LINE__, _assert_buf);        \
            }                                                \
        } while (false)
#else
    // Debug: use debugger break, no crash handler.
    #define ZENGINE_VALIDATE_ASSERT(cond, msg)          \
        do {                                             \
            if (!(cond)) {                               \
                ZENGINE_CORE_CRITICAL(msg);              \
                ZENGINE_DEBUG_BREAK();                   \
            }                                            \
        } while (false)
#endif
```

`CrashHandler::OnAssertionFailure` calls `CrashHandler::OnCrash` with the formatted assertion string as the "exception" description. The resulting crash log will show:

```
Exception: Assertion failed: (entity_id < m_entity_count)
  Message: Entity ID out of range
  File:    ZEngine/ECS/EntityRegistry.cpp
  Line:    87
```

`OnAssertionFailure` is marked `[[noreturn]]` — it must terminate the process (by raising `SIGABRT` on POSIX or calling `RaiseException` on Windows) after writing the dump.

The implementation must truncate the message buffer safely:

```cpp
[[noreturn]] static void OnAssertionFailure(
    cstring file,
    int     line,
    cstring message)
{
    static constexpr size_t kMaxAssertMessageLen = 1024;
    char buf[kMaxAssertMessageLen];
    // snprintf always null-terminates; truncates if message exceeds buffer.
    snprintf(buf, kMaxAssertMessageLen,
             "Assertion failed: %s\nFile: %s\nLine: %d",
             message ? message : "(null)", file ? file : "(null)", line);
    buf[kMaxAssertMessageLen - 1] = '\0';  // guaranteed even if snprintf misbehaves
    OnCrash(buf, nullptr);
}
```

The declaration in the `CrashHandler` class must also carry `[[noreturn]]`:

```cpp
[[noreturn]] static void OnAssertionFailure(
    cstring file,
    int     line,
    cstring message);
```

---

## 10. Build Configuration

```cmake
# ZEngine/CrashHandler/CMakeLists.txt

add_library(ZEngineCrashHandler STATIC)

target_sources(ZEngineCrashHandler
    PRIVATE
        CrashHandler.cpp         # platform dispatch + log writing
)

# Platform-specific source files.
if(WIN32)
    target_sources(ZEngineCrashHandler PRIVATE CrashHandlerWindows.cpp)
    target_link_libraries(ZEngineCrashHandler PRIVATE DbgHelp)
elseif(APPLE)
    target_sources(ZEngineCrashHandler PRIVATE CrashHandlerMacOS.cpp)
    target_link_libraries(ZEngineCrashHandler PRIVATE "-lobjc")
else() # Linux
    target_sources(ZEngineCrashHandler PRIVATE CrashHandlerLinux.cpp)
    target_link_libraries(ZEngineCrashHandler PRIVATE dl) # for dlopen (GTK)
endif()

target_include_directories(ZEngineCrashHandler
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/..  # ZEngine root
)

# Crash handler is compiled ONLY in Release and RelWithDebInfo.
# In Debug, assert macros fall through to ZENGINE_DEBUG_BREAK.
target_compile_definitions(ZEngineCrashHandler
    PRIVATE
        $<$<CONFIG:Release>:ZENGINE_CRASH_HANDLER_ENABLED=1>
        $<$<CONFIG:RelWithDebInfo>:ZENGINE_CRASH_HANDLER_ENABLED=1>
)

# Optional: upload endpoint.
if(DEFINED ZENGINE_CRASH_ENDPOINT)
    target_compile_definitions(ZEngineCrashHandler
        PRIVATE
            ZENGINE_CRASH_UPLOAD=1
            ZENGINE_CRASH_ENDPOINT="${ZENGINE_CRASH_ENDPOINT}"
    )
endif()
```

In Debug builds, `ZEngineCrashHandler` is still compiled (so the `CrashHandler::Install()` call in engine init doesn't require `#ifdef`), but `Install()` is a no-op at runtime when `ZENGINE_CRASH_HANDLER_ENABLED` is not defined.

---

## 11. Symbol Upload for Minidumps

Post-mortem debugging of release crashes requires that the symbols (PDB on Windows, DWARF on Linux) are available when the crash dump is opened in a debugger. Symbols are uploaded as a CI/CD post-deploy step.

### Windows — PDB Upload

PDB files are produced by MSVC for all Release/RelWithDebInfo builds. They must be uploaded to a symbol server before the corresponding build is distributed to players.

**Option A — Microsoft Symbol Server (private):** Use `symstore.exe` (Windows SDK) to add PDBs to a file-share-based symbol store. WinDbg/VS look up symbols via `srv*<local cache>*\\share\symbols`.

**Option B — Sentry (recommended):** [Sentry](https://sentry.io) supports PDB upload via the `sentry-cli` tool:
```sh
sentry-cli difutil check ZRuntime.pdb
sentry-cli upload-dif --org my-org --project my-game ZRuntime.pdb ZEngine.pdb
```
When a crash dump is uploaded to Sentry (via the HTTP endpoint in Section 8), Sentry automatically symbolizes the stack trace using the matching PDB.

### Linux — DWARF / Breakpad

**Option A — GDB remote symbolication:** Strip binaries with `objcopy --strip-debug` and retain a sidecar `.debug` file. Rebuild symbol index with `dwz`. Upload the `.debug` file.

**Option B — Breakpad/Crashpad:** Google's Breakpad converts DWARF to a `.sym` text format:
```sh
dump_syms ZRuntime > ZRuntime.sym
symupload ZRuntime.sym https://symbols.yourgame.com/
```

**Option C — Sentry:** `sentry-cli upload-dif` supports ELF files with DWARF directly.

### macOS — dSYM Upload

Xcode / LLVM produces `.dSYM` bundles alongside `.app` bundles. Upload with:
```sh
sentry-cli upload-dif ZRuntime.app.dSYM
```

---

## 12. File Layout

```
ZEngine/
└── CrashHandler/
    ├── CMakeLists.txt
    ├── CrashHandler.h              # Public API (Section 3)
    ├── CrashHandler.cpp            # Platform-dispatch + log writer
    ├── CrashHandlerInternal.h      # Shared internal state (Section 3)
    ├── CrashHandlerWindows.cpp     # SEH filter + MiniDumpWriteDump
    ├── CrashHandlerLinux.cpp       # sigaction + backtrace()
    └── CrashHandlerMacOS.cpp       # sigaction + backtrace + NSAlert
```

The `CrashHandler` library has no include dependencies on other ZEngine subsystems. The only engine header it references is `Core/Defines.h` for `cstring` and platform macros, which has no further dependencies.

---

## 13. Deliverables Checklist

- [ ] `CrashHandler.h` — public API as specified in Section 3
- [ ] `CrashHandler.cpp` — `OnCrash()` coordinator, log writer, pre-crash callback dispatch
- [ ] `CrashHandlerWindows.cpp` — SEH filter, `MiniDumpWriteDump`, `MessageBoxW` dialog
- [ ] `CrashHandlerLinux.cpp` — `sigaction` registration, `backtrace()` collection, GTK dialog
- [ ] `CrashHandlerMacOS.cpp` — `sigaction` registration, `dladdr` symbolication, NSAlert via ObjC runtime
- [ ] `CMakeLists.txt` — platform-conditional compilation, DbgHelp link, upload endpoint define
- [ ] `ZENGINE_VALIDATE_ASSERT` updated to call `OnAssertionFailure` in non-Debug builds
- [ ] Integration test: trigger a deliberate null dereference in RelWithDebInfo; verify log file is created with a valid stack trace
- [ ] Integration test (Windows): verify `.dmp` file is created and can be opened in WinDbg
- [ ] Symbol upload script (`scripts/upload_symbols.sh`) for CI/CD post-deploy step
- [ ] Documentation added to `docs/future-plan/crash-handler.md` (this file)
