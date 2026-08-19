#include <ZEngine/CrashHandlers/CrashHandlerInternal.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
// clang-format off
#include <Windows.h>
// clang-format on
#include <DbgHelp.h>
#include <crtdbg.h>
#include <new.h>
#include <atomic>
#include <csignal>
#include <cstdint>

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace ZEngine::CrashHandlers
{
    using ZEngine::Helpers::secure_strlen;
    using ZEngine::Helpers::secure_strncpy;

    struct WorkerThreadParams
    {
        DWORD               FaultedThreadId        = 0;
        HANDLE              FaultedThreadHandle    = nullptr;
        char                SignalOrException[512] = {};
        EXCEPTION_POINTERS* ExceptionInfo          = nullptr;
        char                LogPath[kMaxPathLen]   = {0};
        char                DumpPath[kMaxPathLen]  = {0};
    };

    enum : int
    {
        IDC_HEADER       = 101,
        IDC_DESC         = 102,
        IDC_COMMENT_HINT = 103,
        IDC_COMMENT_EDIT = 104,
        IDC_LOG_CAPTION  = 105,
        IDC_LOG_EDIT     = 106,
        IDC_CLOSE_BTN    = 107,
        IDC_SEND_BTN     = 108,
    };

    struct CrashDlgState
    {
        cstring AppName;
        cstring Signal;
        cstring LogPath;
        bool    DidSend;
        WCHAR   Comment[kMaxCommentLen];
        HFONT   hFontBold;
        HFONT   hFontNormal;
        HFONT   hFontMono;
    };

    static LPTOP_LEVEL_EXCEPTION_FILTER g_previous_exception_filter = nullptr;
    // Minimum stack for the crash worker thread.
    // MiniDumpWriteDump with MiniDumpWithFullMemory requires significant stack space
    // (~100-200 KB observed in practice). 64 KB is insufficient.
    // Use 256 KB as a safe minimum.
    static constexpr size_t             kCrashWorkerStackSize       = 256 * 1024; // 256 KB minimum

    static void                         ExceptionToString(ULONG_PTR crash_address, DWORD code, char* buffer, size_t buffer_size)
    {
        const char* exception_name = nullptr;
        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:
                exception_name = "Access Violation";
                break;
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                exception_name = "Array Bounds Exceeded";
                break;
            case EXCEPTION_BREAKPOINT:
                exception_name = "Breakpoint";
                break;
            case EXCEPTION_DATATYPE_MISALIGNMENT:
                exception_name = "Datatype Misalignment";
                break;
            case EXCEPTION_FLT_DENORMAL_OPERAND:
                exception_name = "Float Denormal Operand";
                break;
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                exception_name = "Float Divide by Zero";
                break;
            case EXCEPTION_FLT_INEXACT_RESULT:
                exception_name = "Float Inexact Result";
                break;
            case EXCEPTION_FLT_INVALID_OPERATION:
                exception_name = "Float Invalid Operation";
                break;
            case EXCEPTION_FLT_OVERFLOW:
                exception_name = "Float Overflow";
                break;
            case EXCEPTION_FLT_STACK_CHECK:
                exception_name = "Float Stack Check";
                break;
            case EXCEPTION_FLT_UNDERFLOW:
                exception_name = "Float Underflow";
                break;
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                exception_name = "Illegal Instruction";
                break;
            case EXCEPTION_IN_PAGE_ERROR:
                exception_name = "In Page Error";
                break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                exception_name = "Integer Divide by Zero";
                break;
            case EXCEPTION_INT_OVERFLOW:
                exception_name = "Integer Overflow";
                break;
            case EXCEPTION_INVALID_DISPOSITION:
                exception_name = "Invalid Disposition";
                break;
            case EXCEPTION_NONCONTINUABLE_EXCEPTION:
                exception_name = "Noncontinuable Exception";
                break;
            case EXCEPTION_PRIV_INSTRUCTION:
                exception_name = "Privileged Instruction";
                break;
            case EXCEPTION_SINGLE_STEP:
                exception_name = "Single Step";
                break;
            case EXCEPTION_STACK_OVERFLOW:
                exception_name = "Stack Overflow";
                break;
            default:
                exception_name = "Unknown Exception";
                break;
        }
        snprintf(buffer, buffer_size, "%s at address 0x%p", exception_name, (void*) crash_address);
    }

    static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exception_pointers)
    {
        const DWORD     code                       = exception_pointers->ExceptionRecord->ExceptionCode;
        const ULONG_PTR crash_address              = (code == EXCEPTION_ACCESS_VIOLATION) ? exception_pointers->ExceptionRecord->ExceptionInformation[1] : 0;

        char            exception_description[128] = {};
        ExceptionToString(crash_address, code, exception_description, sizeof(exception_description));

        ZEngine::CrashHandlers::CrashHandler::OnCrash(exception_description, static_cast<void*>(exception_pointers));
        return EXCEPTION_EXECUTE_HANDLER;
    }

    static void PureCallHandler()
    {
        ZEngine::CrashHandlers::CrashHandler::OnCrash("CRT Error: Pure Virtual Function Call", nullptr);
    }

    static void InvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
    {
        ZEngine::CrashHandlers::CrashHandler::OnCrash("CRT Error: Invalid Parameter passed to Secure Function", nullptr);
    }

    static void SignalAbrtHandler(int signal)
    {
        ZEngine::CrashHandlers::CrashHandler::OnCrash("CRT Error: SIGABRT (abort requested)", nullptr);
    }

    static int OutOfMemoryHandler(size_t size)
    {
        ZEngine::CrashHandlers::CrashHandler::OnCrash("CRT Error: Out of Memory", nullptr);
        return 0; // Return 0 to indicate that the new handler did not handle the allocation failure
    }

    static void SafeWriteToFile(HANDLE file, cstring str);

    static void WriteSystemInfo(HANDLE file)
    {
        // OS version via RtlGetVersion (not affected by compatibility shim, unlike GetVersionEx)
        char os_buf[128] = "(unknown)";
        {
            using RtlGetVersionFn = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);
            HMODULE ntdll         = GetModuleHandleW(L"ntdll.dll");
            if (ntdll)
            {
                auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
                if (fn)
                {
                    RTL_OSVERSIONINFOW vi  = {};
                    vi.dwOSVersionInfoSize = sizeof(vi);
                    if (fn(&vi) == 0)
                        snprintf(os_buf, sizeof(os_buf), "Windows %lu.%lu (build %lu)", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
                }
            }
        }

        // Architecture
        const char* arch = "unknown";
        {
            SYSTEM_INFO si = {};
            GetNativeSystemInfo(&si);
            switch (si.wProcessorArchitecture)
            {
                case PROCESSOR_ARCHITECTURE_AMD64:
                    arch = "x86_64";
                    break;
                case PROCESSOR_ARCHITECTURE_ARM64:
                    arch = "arm64";
                    break;
                case PROCESSOR_ARCHITECTURE_INTEL:
                    arch = "x86";
                    break;
                default:
                    arch = "unknown";
                    break;
            }
        }

        // CPU brand string from registry
        char cpu_model[256] = "(unknown)";
        {
            HKEY hKey = nullptr;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                DWORD type = 0, size = static_cast<DWORD>(sizeof(cpu_model));
                RegQueryValueExA(hKey, "ProcessorNameString", nullptr, &type, reinterpret_cast<LPBYTE>(cpu_model), &size);
                RegCloseKey(hKey);
            }
        }

        char line_buf[512];
        snprintf(line_buf, sizeof(line_buf), "OS:        %s\n", os_buf);
        SafeWriteToFile(file, line_buf);
        snprintf(line_buf, sizeof(line_buf), "Arch:      %s\n", arch);
        SafeWriteToFile(file, line_buf);
        snprintf(line_buf, sizeof(line_buf), "CPU:       %s\n", cpu_model);
        SafeWriteToFile(file, line_buf);
    }

    static void SafeWriteToFile(HANDLE file, cstring str)
    {
        if (file == INVALID_HANDLE_VALUE || !str)
            return;
        DWORD written;
        WriteFile(file, str, static_cast<DWORD>(secure_strlen(str)), &written, nullptr);
    }

    static LRESULT WinProcFn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
            case WM_CREATE:
            {
                auto* cs    = reinterpret_cast<CREATESTRUCTW*>(lp);
                auto* state = reinterpret_cast<CrashDlgState*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

                // fonts
                state->hFontNormal = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                state->hFontBold   = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

                HINSTANCE hInst    = GetModuleHandleW(nullptr);
                const int PAD      = 20;
                const int W        = 720;
                const int BTN_H = 28, BTN_W = 160;
                int       y           = PAD;

                // bold header
                WCHAR     header[256] = {};
                {
                    WCHAR app_w[128] = {};
                    MultiByteToWideChar(CP_UTF8, 0, state->AppName, -1, app_w, 128);
                    _snwprintf_s(header, _countof(header), _TRUNCATE, L"An application has crashed: %s", app_w);
                }
                HWND hHeader = CreateWindowExW(0, L"STATIC", header, WS_CHILD | WS_VISIBLE | SS_LEFT, PAD, y, W - PAD * 2, 24, hwnd, (HMENU) IDC_HEADER, hInst, nullptr);
                SendMessageW(hHeader, WM_SETFONT, (WPARAM) state->hFontBold, TRUE);
                y               += 30;

                // description
                WCHAR desc[512]  = {};
                {
                    WCHAR sig_w[256] = {};
                    MultiByteToWideChar(CP_UTF8, 0, state->Signal, -1, sig_w, 256);
                    _snwprintf_s(desc, _countof(desc), _TRUNCATE, L"The application encountered an unexpected error and could not continue.\r\n\r\nSignal: %s", sig_w);
                }
                HWND hDesc = CreateWindowExW(0, L"STATIC", desc, WS_CHILD | WS_VISIBLE | SS_LEFT, PAD, y, W - PAD * 2, 56, hwnd, (HMENU) IDC_DESC, hInst, nullptr);
                SendMessageW(hDesc, WM_SETFONT, (WPARAM) state->hFontNormal, TRUE);
                y                 += 64;

                // comment hint
                HWND hCommentHint  = CreateWindowExW(0, L"STATIC", L"Describe what you were doing when the crash occurred (optional):", WS_CHILD | WS_VISIBLE | SS_LEFT, PAD, y, W - PAD * 2, 18, hwnd, (HMENU) IDC_COMMENT_HINT, hInst, nullptr);
                SendMessageW(hCommentHint, WM_SETFONT, (WPARAM) state->hFontNormal, TRUE);
                y                 += 22;

                // editable comment multiline edit
                HWND hCommentEdit  = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, PAD, y, W - PAD * 2, 90, hwnd, (HMENU) IDC_COMMENT_EDIT, hInst, nullptr);
                SendMessageW(hCommentEdit, WM_SETFONT, (WPARAM) state->hFontNormal, TRUE);
                SendMessageW(hCommentEdit, EM_SETLIMITTEXT, kMaxCommentLen - 1, 0);
                y                += 96;

                // log caption
                HWND hLogCaption  = CreateWindowExW(0, L"STATIC", L"Crash log:", WS_CHILD | WS_VISIBLE | SS_LEFT, PAD, y, W - PAD * 2, 16, hwnd, (HMENU) IDC_LOG_CAPTION, hInst, nullptr);
                SendMessageW(hLogCaption, WM_SETFONT, (WPARAM) state->hFontNormal, TRUE);
                y             += 20;

                // read-only log edit (dark bg / light text)
                HWND hLogEdit  = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL, PAD, y, W - PAD * 2, 200, hwnd, (HMENU) IDC_LOG_EDIT, hInst, nullptr);
                {
                    state->hFontMono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
                    SendMessageW(hLogEdit, WM_SETFONT, (WPARAM) state->hFontMono, TRUE);
                }
                // load log content
                {
                    HANDLE hLog = CreateFileA(state->LogPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hLog != INVALID_HANDLE_VALUE)
                    {
                        DWORD size = GetFileSize(hLog, nullptr);
                        if (size != INVALID_FILE_SIZE && size > 0)
                        {
                            char* buf = (char*) HeapAlloc(GetProcessHeap(), 0, size + 1);
                            if (buf)
                            {
                                DWORD read = 0;
                                if (ReadFile(hLog, buf, size, &read, nullptr) && read > 0)
                                {
                                    buf[read] = '\0'; // Ensure it's null-terminated

                                    // 1. Find out how many WCHAR characters are needed for the UTF-8 string
                                    int wlen  = MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
                                    if (wlen > 0)
                                    {
                                        WCHAR* wbuf = (WCHAR*) HeapAlloc(GetProcessHeap(), 0, wlen * sizeof(WCHAR));
                                        if (wbuf)
                                        {
                                            // 2. Perform the conversion into wbuf
                                            MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, wlen);

                                            // 3. Allocate a destination buffer for line-ending normalization (\n -> \r\n)
                                            // Worst-case scenario is every character is \n, doubling the size
                                            int    normalized_capacity = wlen * 2;
                                            WCHAR* normalized_wbuf     = (WCHAR*) HeapAlloc(GetProcessHeap(), 0, normalized_capacity * sizeof(WCHAR));

                                            if (normalized_wbuf)
                                            {
                                                WCHAR* src = wbuf;
                                                WCHAR* dst = normalized_wbuf;

                                                while (*src)
                                                {
                                                    // If we encounter a lone \n, prepend a \r
                                                    if (*src == L'\n' && (src == wbuf || *(src - 1) != L'\r'))
                                                    {
                                                        *dst++ = L'\r';
                                                    }
                                                    *dst++ = *src++;
                                                }
                                                *dst = L'\0'; // Null-terminate the final string

                                                // Push the normalized text to your edit control
                                                SetWindowTextW(hLogEdit, normalized_wbuf);

                                                HeapFree(GetProcessHeap(), 0, normalized_wbuf);
                                            }

                                            // Scroll to bottom
                                            SendMessageW(hLogEdit, EM_SETSEL, (WPARAM) -1, (LPARAM) -1);
                                            SendMessageW(hLogEdit, EM_SCROLLCARET, 0, 0);

                                            HeapFree(GetProcessHeap(), 0, wbuf);
                                        }
                                    }
                                }
                                HeapFree(GetProcessHeap(), 0, buf);
                            }
                        }
                        CloseHandle(hLog);
                    }
                }
                y              += 206;

                // "Close Without Sending" button
                HWND hCloseBtn  = CreateWindowExW(0, L"BUTTON", L"Close Without Sending", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, PAD, y, BTN_W, BTN_H, hwnd, (HMENU) IDC_CLOSE_BTN, hInst, nullptr);
                SendMessageW(hCloseBtn, WM_SETFONT, (WPARAM) state->hFontNormal, TRUE);

                // "Send and Close" button (default)
                HWND hSendBtn = CreateWindowExW(0, L"BUTTON", L"Send and Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, W - PAD - BTN_W, y, BTN_W, BTN_H, hwnd, (HMENU) IDC_SEND_BTN, hInst, nullptr);
                SendMessageW(hSendBtn, WM_SETFONT, (WPARAM) state->hFontNormal, TRUE);

                (void) hCommentEdit;
                (void) hLogEdit;
                (void) hCloseBtn;
                (void) hSendBtn;
                return 0;
            }
            case WM_CTLCOLORSTATIC:
            {
                HDC  hdcStatic  = (HDC) wp;
                HWND hwndStatic = (HWND) lp;

                if (GetDlgCtrlID(hwndStatic) == IDC_HEADER)
                {
                    // Make the text background transparent
                    SetBkMode(hdcStatic, TRANSPARENT);
                    return (INT_PTR) GetStockObject(HOLLOW_BRUSH);
                }

                return 0;
            }
            case WM_CTLCOLOREDIT:
            {
                // Dark background for the log read-only edit
                HWND hCtrl = reinterpret_cast<HWND>(lp);
                if (GetDlgCtrlID(hCtrl) == IDC_LOG_EDIT)
                {
                    HDC hdc = reinterpret_cast<HDC>(wp);
                    SetBkColor(hdc, RGB(30, 30, 30));
                    SetTextColor(hdc, RGB(212, 212, 212));
                    static HBRUSH hDarkBrush = CreateSolidBrush(RGB(30, 30, 30));
                    return reinterpret_cast<LRESULT>(hDarkBrush);
                }
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            case WM_COMMAND:
            {
                auto* state = reinterpret_cast<CrashDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                WORD  id    = LOWORD(wp);
                if (id == IDC_CLOSE_BTN)
                {
                    state->DidSend = false;
                    PostQuitMessage(0);
                }
                else if (id == IDC_SEND_BTN)
                {
                    state->DidSend = true;
                    GetDlgItemTextW(hwnd, IDC_COMMENT_EDIT, state->Comment, _countof(state->Comment));
                    PostQuitMessage(0);
                }
                return 0;
            }
            case WM_CLOSE:
            {
                auto* state    = reinterpret_cast<CrashDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                state->DidSend = false;
                PostQuitMessage(0);
                return 0;
            }
            case WM_DESTROY:
            {
                auto* state = reinterpret_cast<CrashDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (state->hFontBold)
                {
                    DeleteObject(state->hFontBold);
                    state->hFontBold = nullptr;
                }
                if (state->hFontNormal)
                {
                    DeleteObject(state->hFontNormal);
                    state->hFontNormal = nullptr;
                }
                if (state->hFontMono)
                {
                    DeleteObject(state->hFontMono);
                    state->hFontMono = nullptr;
                }
                return 0;
            }
            default:
                return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    static DWORD WINAPI CrashWorkerThreadFn(LPVOID param)
    {
        auto* params = reinterpret_cast<WorkerThreadParams*>(param);

        if (g_state.PreCrashFn)
        {
            struct CallbackParams
            {
                CrashHandler::PreCrashFn fn;
                void*                    ctx;
            };
            CallbackParams callback_params = {g_state.PreCrashFn, g_state.PreCrashCtx};

            HANDLE         callback_thread = CreateThread(
                nullptr,
                0,
                [](LPVOID p) -> DWORD {
                    auto* cp = static_cast<CallbackParams*>(p);
                    __try
                    {
                        cp->fn(cp->ctx);
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                    }
                    return 0;
                },
                &callback_params,
                0,
                nullptr);

            if (callback_thread)
            {
                const DWORD timeout_ms = static_cast<DWORD>(kCallbackTimeoutSec) * 1000;
                if (WaitForSingleObject(callback_thread, timeout_ms) == WAIT_TIMEOUT)
                {
                    // Callback did not complete in time. TerminateThread is unsafe in general
                    // but acceptable here — the process is about to exit anyway.
                    TerminateThread(callback_thread, 0);
                }
                CloseHandle(callback_thread);
            }
        }

        HANDLE log_file_handle = CreateFileA(params->LogPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log_file_handle != INVALID_HANDLE_VALUE)
        {
            char       log_buffer[1024] = {};
            SYSTEMTIME current_time;
            GetSystemTime(&current_time);

            SafeWriteToFile(log_file_handle, "==================================================================\n");
            SafeWriteToFile(log_file_handle, "ZEngine Crash Report\n");
            SafeWriteToFile(log_file_handle, "==================================================================\n");

            snprintf(log_buffer, sizeof(log_buffer), "App:       %s %s\n", g_state.AppName, g_state.Version);
            SafeWriteToFile(log_file_handle, log_buffer);
            snprintf(log_buffer, sizeof(log_buffer), "Date:      %04d-%02d-%02d %02d:%02d:%02d UTC\n", current_time.wYear, current_time.wMonth, current_time.wDay, current_time.wHour, current_time.wMinute, current_time.wSecond);
            SafeWriteToFile(log_file_handle, log_buffer);
            WriteSystemInfo(log_file_handle);
            snprintf(log_buffer, sizeof(log_buffer), "Exception: %s\n\n", params->SignalOrException);
            SafeWriteToFile(log_file_handle, log_buffer);

            SafeWriteToFile(log_file_handle, "==================================================================\n");
            SafeWriteToFile(log_file_handle, "Stack Trace (most recent call first):\n");
            SafeWriteToFile(log_file_handle, "==================================================================\n");

            constexpr size_t kMaxStackTrace                    = 32 * 1024;
            char             stacktrace_buffer[kMaxStackTrace] = {0};

            HANDLE           process                           = GetCurrentProcess();

            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            SymInitialize(process, nullptr, TRUE);

            CONTEXT context = {};
            if (params->ExceptionInfo)
            {
                context = *params->ExceptionInfo->ContextRecord;
            }
            else
            {
                RtlCaptureContext(&context);
            }

            STACKFRAME64 frame   = {};
            DWORD        machine = 0;

#ifdef _M_AMD64
            machine                = IMAGE_FILE_MACHINE_AMD64;
            frame.AddrPC.Offset    = context.Rip;
            frame.AddrFrame.Offset = context.Rbp;
            frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
            machine                = IMAGE_FILE_MACHINE_ARM64;
            frame.AddrPC.Offset    = context.Pc;
            frame.AddrFrame.Offset = context.Fp;
            frame.AddrStack.Offset = context.Sp;
#endif
            frame.AddrPC.Mode                                              = AddrModeFlat;
            frame.AddrFrame.Mode                                           = AddrModeFlat;
            frame.AddrStack.Mode                                           = AddrModeFlat;

            constexpr size_t kMaxSymName                                   = 256;
            uint8_t          sym_buffer[sizeof(SYMBOL_INFO) + kMaxSymName] = {};
            auto*            sym                                           = reinterpret_cast<SYMBOL_INFO*>(sym_buffer);
            sym->SizeOfStruct                                              = sizeof(SYMBOL_INFO);
            sym->MaxNameLen                                                = kMaxSymName;

            IMAGEHLP_LINE64 line_info                                      = {};
            line_info.SizeOfStruct                                         = sizeof(IMAGEHLP_LINE64);

            size_t offset                                                  = 0;
            int    frame_idx                                               = 0;

            while (StackWalk64(machine, process, params->FaultedThreadHandle, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            {
                if (frame.AddrPC.Offset == 0)
                {
                    break;
                }

                char    name_buf[kMaxSymName] = {0};
                DWORD64 sym_disp              = 0;

                if (SymFromAddr(process, frame.AddrPC.Offset, &sym_disp, sym))
                {
                    secure_strncpy(name_buf, sizeof(name_buf), sym->Name, sizeof(name_buf) - 1);
                }
                else
                {
                    _snprintf_s(name_buf, sizeof(name_buf), _TRUNCATE, "<unknown>");
                }

                DWORD line_disp = 0;
                int   written   = 0;

                if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_disp, &line_info))
                {
                    written = _snprintf_s(stacktrace_buffer + offset, kMaxStackTrace - offset, _TRUNCATE, "\t#%-2d  %s\n        %s:%lu\n", frame_idx, name_buf, line_info.FileName, line_info.LineNumber);
                }
                else
                {
                    written = _snprintf_s(stacktrace_buffer + offset, kMaxStackTrace - offset, _TRUNCATE, "\t#%-2d  %s  [0x%016llX]\n", frame_idx, name_buf, (ULONG64) frame.AddrPC.Offset);
                }

                if (written > 0)
                    offset += written;
                if (offset >= kMaxStackTrace - 1)
                    break;
                ++frame_idx;
            }
            SafeWriteToFile(log_file_handle, stacktrace_buffer);
            SafeWriteToFile(log_file_handle, "\n");

            SafeWriteToFile(log_file_handle, "==================================================================\n");
            SafeWriteToFile(log_file_handle, "Modules:\n");
            SafeWriteToFile(log_file_handle, "==================================================================\n");
            EnumerateLoadedModules64(
                GetCurrentProcess(),
                [](PCSTR module_name, DWORD64 base_address, ULONG size, PVOID user_context) -> BOOL {
                    HANDLE log_file_handle = reinterpret_cast<HANDLE>(user_context);
                    char   log_buffer[512] = {};
                    snprintf(log_buffer, sizeof(log_buffer), "\t%s | Base Address: 0x%llX | Size: %lu bytes\n", module_name, base_address, size);
                    SafeWriteToFile(log_file_handle, log_buffer);
                    return TRUE; // Continue enumeration
                },
                log_file_handle);

            SymCleanup(process);

            SafeWriteToFile(log_file_handle, "==================================================================\n");
            SafeWriteToFile(log_file_handle, "End of Report\n");
            SafeWriteToFile(log_file_handle, "==================================================================\n");
            CloseHandle(log_file_handle);
        }

        {
            // Attempt to open the dump file; fall back to %TEMP% if the primary path fails.
            HANDLE dump_file_handle = CreateFileA(params->DumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (dump_file_handle == INVALID_HANDLE_VALUE)
            {
                char temp_dir[MAX_PATH]       = {};
                char temp_dump_path[MAX_PATH] = {};
                GetTempPathA(MAX_PATH, temp_dir);
                snprintf(temp_dump_path, sizeof(temp_dump_path), "%s\\crash_temp_%llu.dmp", temp_dir, GetTickCount64());
                dump_file_handle = CreateFileA(temp_dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            }

            if (dump_file_handle == INVALID_HANDLE_VALUE)
            {
                fputs("[CrashHandler] FATAL: Failed to create minidump file at both the specified path and temporary path.\n", stderr);
            }
            else
            {
                MINIDUMP_EXCEPTION_INFORMATION  dump_info     = {};
                MINIDUMP_EXCEPTION_INFORMATION* dump_info_ptr = nullptr;
                if (params->ExceptionInfo)
                {
                    dump_info.ThreadId          = params->FaultedThreadId;
                    dump_info.ExceptionPointers = params->ExceptionInfo;
                    dump_info.ClientPointers    = FALSE;
                    dump_info_ptr               = &dump_info;
                }

#ifdef _DEBUG
                const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal);
#elif defined(NDEBUG) || defined(ZENGINE_RELWITHDEBINFO)
                const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo | MiniDumpWithHandleData | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData);
#else
                const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory);
#endif
                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump_file_handle, dump_type, dump_info_ptr, nullptr, nullptr);
                CloseHandle(dump_file_handle);
            }
        }

        // Native Win32 crash dialog matching the macOS layout.
        auto       hasenv      = [](const char* name) { return GetEnvironmentVariableA(name, nullptr, 0) > 0; };
        const bool skip_dialog = hasenv("ZENGINE_CRASH_NO_DIALOG") || hasenv("CI");
        if (!skip_dialog)
        {
            static CrashDlgState s_dlg_state;
            s_dlg_state               = {};
            s_dlg_state.AppName       = g_state.AppName;
            s_dlg_state.Signal        = params->SignalOrException;
            s_dlg_state.LogPath       = params->LogPath;

            // Register window class (unique name to avoid conflicts)
            const wchar_t* kClassName = L"ZEngineCrashDialog";
            WNDCLASSEXW    wc         = {};
            wc.cbSize                 = sizeof(wc);
            wc.style                  = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc            = WinProcFn;
            HINSTANCE hInst           = GetModuleHandleW(nullptr);
            wc.hInstance              = hInst;
            wc.hCursor                = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground          = (HBRUSH) (COLOR_WINDOW + 1);
            wc.lpszClassName          = kClassName;
            RegisterClassExW(&wc);

            const int WND_W = 760, WND_H = 560;
            int       sx   = GetSystemMetrics(SM_CXSCREEN);
            int       sy   = GetSystemMetrics(SM_CYSCREEN);
            HWND      hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, kClassName, L"Application Crash", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, (sx - WND_W) / 2, (sy - WND_H) / 2, WND_W, WND_H, nullptr, nullptr, hInst, &s_dlg_state);

            if (hwnd)
            {
                ShowWindow(hwnd, SW_SHOW);
                UpdateWindow(hwnd);

                MSG m = {};
                while (GetMessageW(&m, nullptr, 0, 0) > 0)
                {
                    TranslateMessage(&m);
                    DispatchMessageW(&m);
                }
                DestroyWindow(hwnd);
            }
            UnregisterClassW(kClassName, GetModuleHandleW(nullptr));

            if (s_dlg_state.DidSend)
            {
                g_state.UserConsentUpload = true;
                if (s_dlg_state.Comment[0] != L'\0')
                    WideCharToMultiByte(CP_UTF8, 0, s_dlg_state.Comment, -1, g_state.UserComment, static_cast<int>(kMaxCommentLen), nullptr, nullptr);
            }

            // Append user comment to the log file if provided
            if (g_state.UserComment[0] != '\0')
            {
                HANDLE hAppend = CreateFileA(params->LogPath, FILE_APPEND_DATA, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hAppend != INVALID_HANDLE_VALUE)
                {
                    SafeWriteToFile(hAppend, "\n==================================================================\n");
                    SafeWriteToFile(hAppend, "User Comment:\n");
                    SafeWriteToFile(hAppend, "==================================================================\n");
                    SafeWriteToFile(hAppend, g_state.UserComment);
                    SafeWriteToFile(hAppend, "\n");
                    CloseHandle(hAppend);
                }
            }
        }

        // Upload the crash report if the user consented
#ifdef ZENGINE_CRASH_UPLOAD
        if (g_state.UserConsentUpload)
        {
            // TODO(jeanphilippekernel): Implement crash report upload functionality here.
            // cstring                               upload_url = "https://yourserver.com/upload_crash_report";
            // winrt::Windows::Foundation::Uri       uri{winrt::to_hstring(upload_url)};

            // winrt::Windows::Web::Http::HttpClient http_client;
            // http_client.DefaultRequestHeaders().UserAgent().ParseAdd(L"ZEngine/1.0");
            // http_client.PostAsync(uri, winrt::Windows::Web::Http::HttpStringContent(winrt::to_hstring(params->LogPath), winrt::Windows::Web::Http::HttpMediaTypeHeaderValue(L"text/plain"))).get();
            // http_client.PostAsync(uri, winrt::Windows::Web::Http::HttpStringContent(winrt::to_hstring(params->DumpPath), winrt::Windows::Web::Http::HttpMediaTypeHeaderValue(L"application/octet-stream"))).get();

            // http_client.Close();
        }
#endif // ZENGINE_CRASH_UPLOAD

        return 0;
    }

    void CrashHandler::Install(const char* app_name, const char* version, const char* crash_log_dir)
    {
#ifdef ZENGINE_CRASH_HANDLER_ENABLED
        if (g_state.Installed)
        {
            return;
        }

        StoreMetadata(app_name, version, crash_log_dir);

        _CrtSetReportMode(_CRT_ASSERT, 0);
        _set_purecall_handler(PureCallHandler);
        _set_invalid_parameter_handler(InvalidParameterHandler);
        _set_new_handler(OutOfMemoryHandler);
        signal(SIGABRT, SignalAbrtHandler);

        g_previous_exception_filter = SetUnhandledExceptionFilter(UnhandledExceptionFilter);
        CreateDirectoryA(crash_log_dir, nullptr);
        g_state.Installed = true;
#endif
    }

    void CrashHandler::StoreMetadata(const char* app_name, const char* version, const char* crash_log_dir)
    {
        snprintf(g_state.AppName, kMaxNameLen, "%s", app_name);
        snprintf(g_state.Version, kMaxNameLen, "%s", version);
        snprintf(g_state.CrashLogDir, kMaxPathLen, "%s", crash_log_dir);
    }

    void CrashHandler::SetPreCrashCallback(PreCrashFn fn, void* ctx)
    {
        g_state.PreCrashFn  = fn;
        g_state.PreCrashCtx = ctx;
    }

    void CrashHandler::Uninstall()
    {
#ifdef ZENGINE_CRASH_HANDLER_ENABLED
        if (!g_state.Installed)
        {
            return;
        }

        SetUnhandledExceptionFilter(g_previous_exception_filter);
        _set_purecall_handler(nullptr);
        _set_invalid_parameter_handler(nullptr);
        _set_new_handler(nullptr);
        signal(SIGABRT, SIG_DFL);

        g_state.Installed = false;
#endif
    }

    [[noreturn]] void CrashHandler::OnCrash(const char* signal_or_exception, void* ctx)
    {
        static std::atomic<bool> s_in_crash_handler{false};
        if (s_in_crash_handler.exchange(true, std::memory_order_acq_rel))
        {
            // Recursive crash � abort immediately with a minimal message.
            fputs("[CrashHandler] FATAL: crash handler reentered. Aborting.\n", stderr);
            _Exit(EXIT_FAILURE);
        }

        EXCEPTION_POINTERS* exception_info      = reinterpret_cast<EXCEPTION_POINTERS*>(ctx);
        char                crash_message[1024] = {0};
        if (exception_info && exception_info->ExceptionRecord)
        {
            DWORD code = exception_info->ExceptionRecord->ExceptionCode;
            // Only reformat for real OS exceptions. Application-defined codes
            // (0xE0000000–0xEFFFFFFF) carry a caller-supplied message in
            // signal_or_exception already — overwriting it would lose context.
            if ((code & 0xF0000000) != 0xE0000000)
            {
                ULONG_PTR crash_address = (code == EXCEPTION_ACCESS_VIOLATION) ? exception_info->ExceptionRecord->ExceptionInformation[1] : 0;
                ExceptionToString(crash_address, code, crash_message, sizeof(crash_message));
                signal_or_exception = crash_message;
            }
        }

        WorkerThreadParams params;
        snprintf(params.SignalOrException, sizeof(params.SignalOrException), "%s", signal_or_exception ? signal_or_exception : "");
        params.ExceptionInfo   = exception_info;
        params.FaultedThreadId = GetCurrentThreadId();
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &params.FaultedThreadHandle, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
            params.FaultedThreadHandle = GetCurrentThread();

        {
            SYSTEMTIME st = {};
            GetSystemTime(&st);
            char t_str[32] = {};
            snprintf(t_str, sizeof(t_str), "%04d-%02d-%02d_%02d-%02d-%02d", (int) st.wYear, (int) st.wMonth, (int) st.wDay, (int) st.wHour, (int) st.wMinute, (int) st.wSecond);
            snprintf(params.LogPath, sizeof(params.LogPath), "%s\\crash_%s.log", g_state.CrashLogDir, t_str);
            snprintf(params.DumpPath, sizeof(params.DumpPath), "%s\\crash_%s.dmp", g_state.CrashLogDir, t_str);
        }

        HANDLE worker_thread = CreateThread(nullptr, kCrashWorkerStackSize, CrashWorkerThreadFn, &params, 0, nullptr);
        if (worker_thread)
        {
            DWORD wait_result = WaitForSingleObject(worker_thread, INFINITE);
            CloseHandle(worker_thread);
        }
        else
        {
            CrashWorkerThreadFn(&params);
        }
        if (params.FaultedThreadHandle && params.FaultedThreadHandle != GetCurrentThread())
            CloseHandle(params.FaultedThreadHandle);
        _Exit(EXIT_FAILURE);
    }

    [[noreturn]] void CrashHandler::OnAssertionFailure(const char* file, int line, const char* message)
    {
        char assertion_message[1024];
        snprintf(assertion_message, sizeof(assertion_message), "Assertion Failure: %s\nFile: %s\nLine: %d", message ? message : "(null)", file ? file : "(null)", line);
        __try
        {
            RaiseException(0xE0000001, 0, 0, nullptr);
        }
        __except (OnCrash(assertion_message, GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _Exit(EXIT_FAILURE);
    }
} // namespace ZEngine::CrashHandlers