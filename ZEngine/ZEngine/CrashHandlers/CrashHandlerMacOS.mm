#include <ZEngine/CrashHandlers/CrashHandlerInternal.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#import  <AppKit/AppKit.h>
#import  <CoreGraphics/CoreGraphics.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Objective-C class declarations must live at global scope (not inside a C++
// namespace).  The C++ crash handler calls ShowCrashDialogCocoa() which is a
// plain C linkage bridge declared below.
// ─────────────────────────────────────────────────────────────────────────────

@interface ZEngineCrashWindowController : NSObject
@property (nonatomic, assign) BOOL      userDidSend;
@property (nonatomic, copy)   NSString* userComment;
- (instancetype)initWithAppName:(NSString*)appName
                         signal:(NSString*)signal
                        logText:(NSString*)logText;
- (void)runModal;
@end

@implementation ZEngineCrashWindowController
{
    NSWindow*   _window;
    NSTextView* _commentView;
    NSTextView* _logView;
}

- (instancetype)initWithAppName:(NSString*)appName
                         signal:(NSString*)signal
                        logText:(NSString*)logText
{
    self = [super init];
    if (!self) return nil;
    _userDidSend = NO;
    _userComment = @"";

    //Window
    const CGFloat W   = 720.0;
    const CGFloat H   = 510.0;
    const CGFloat pad = 20.0;

    NSRect frame = NSMakeRect(0, 0, W, H);
    _window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [_window setTitle:[NSString stringWithFormat:@"%@ Crash Reporter", appName]];
    [_window center];
    [_window setReleasedWhenClosed:NO];

    NSView* content = _window.contentView;
    CGFloat y = pad;

    //Buttons (bottom row)
    NSButton* sendBtn = [NSButton buttonWithTitle:@"Send and Close"
                                           target:self
                                           action:@selector(onSend:)];
    sendBtn.bezelStyle    = NSBezelStyleRounded;
    sendBtn.keyEquivalent = @"\r";
    sendBtn.frame         = NSMakeRect(W - pad - 160, y, 160, 32);
    [content addSubview:sendBtn];

    NSButton* closeBtn = [NSButton buttonWithTitle:@"Close Without Sending"
                                            target:self
                                            action:@selector(onClose:)];
    closeBtn.bezelStyle    = NSBezelStyleRounded;
    closeBtn.keyEquivalent = @"\033";
    closeBtn.frame         = NSMakeRect(pad, y, 190, 32);
    [content addSubview:closeBtn];

    y += 32 + 16;

    //Log scroll view (read-only)
    const CGFloat logH = 200.0;
    NSScrollView* logScroll = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(pad, y, W - pad * 2, logH)];
    logScroll.borderType          = NSBezelBorder;
    logScroll.hasVerticalScroller = YES;
    logScroll.autohidesScrollers  = YES;

    _logView                = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, W - pad * 2, logH)];
    _logView.editable       = NO;
    _logView.selectable     = YES;
    _logView.string         = logText;
    _logView.font           = [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    _logView.backgroundColor= [NSColor colorWithWhite:0.12 alpha:1.0];
    _logView.textColor      = [NSColor colorWithWhite:0.85 alpha:1.0];
    _logView.drawsBackground= YES;
    _logView.automaticQuoteSubstitutionEnabled = NO;
    _logView.automaticDashSubstitutionEnabled  = NO;

    logScroll.documentView = _logView;
    [content addSubview:logScroll];
    [_logView scrollRangeToVisible:NSMakeRange(_logView.string.length, 0)];

    y += logH + 8;

    //Log caption
    NSTextField* logLabel = [NSTextField labelWithString:
        @"Crash reports comprise diagnostics files and the following summary information:"];
    logLabel.font          = [NSFont systemFontOfSize:11.0];
    logLabel.textColor     = [NSColor secondaryLabelColor];
    logLabel.frame         = NSMakeRect(pad, y, W - pad * 2, 18);
    logLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [content addSubview:logLabel];

    y += 18 + 12;

    //Editable comment text view
    const CGFloat commentH = 90.0;
    NSScrollView* commentScroll = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(pad, y, W - pad * 2, commentH)];
    commentScroll.borderType          = NSBezelBorder;
    commentScroll.hasVerticalScroller = YES;
    commentScroll.autohidesScrollers  = YES;

    _commentView          = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, W - pad * 2, commentH)];
    _commentView.editable = YES;
    _commentView.richText = NO;
    _commentView.font     = [NSFont systemFontOfSize:13.0];
    _commentView.automaticQuoteSubstitutionEnabled = NO;
    _commentView.automaticDashSubstitutionEnabled  = NO;

    commentScroll.documentView = _commentView;
    [content addSubview:commentScroll];

    y += commentH + 4;

    //Comment hint
    NSTextField* commentHint = [NSTextField labelWithString:
        @"Please provide detailed information about what you were doing when the crash occurred."];
    commentHint.font      = [NSFont systemFontOfSize:11.5];
    commentHint.textColor = [NSColor secondaryLabelColor];
    commentHint.frame     = NSMakeRect(pad, y, W - pad * 2, 18);
    [content addSubview:commentHint];

    y += 18 + 14;

    //Description paragraph
    NSString* desc = [NSString stringWithFormat:
        @"The application encountered an unexpected error and could not continue.\n\nSignal: %@", signal];
    NSTextField* descLabel = [NSTextField wrappingLabelWithString:desc];
    descLabel.font         = [NSFont systemFontOfSize:12.5];
    descLabel.frame        = NSMakeRect(pad, y, W - pad * 2, 36);
    [content addSubview:descLabel];

    y += 36 + 10;

    //Header label
    NSTextField* headerLabel = [NSTextField labelWithString:
        [NSString stringWithFormat:@"An application has crashed: %@", appName]];
    headerLabel.font  = [NSFont boldSystemFontOfSize:15.0];
    headerLabel.frame = NSMakeRect(pad, y, W - pad * 2, 22);
    [content addSubview:headerLabel];

    return self;
}

- (void)onSend:(id)sender
{
    _userDidSend = YES;
    _userComment = [_commentView.string copy];
    [NSApp stopModalWithCode:NSModalResponseOK];
    [_window orderOut:nil];
}

- (void)onClose:(id)sender
{
    _userDidSend = NO;
    [NSApp stopModalWithCode:NSModalResponseCancel];
    [_window orderOut:nil];
}

- (void)runModal
{
    [_window makeKeyAndOrderFront:nil];
    [NSApp runModalForWindow:_window];
}

@end

// ─────────────────────────────────────────────────────────────────────────────
// C++ bridge — called from inside the ZEngine::CrashHandlers namespace below.
// Returns true if the user clicked "Send and Close".
// Writes the user comment into out_comment (up to comment_cap bytes).
// ─────────────────────────────────────────────────────────────────────────────
static bool ShowCrashDialogCocoa(cstring app_name,
                                 cstring signal_description,
                                 cstring log_path,
                                 char*   out_comment,
                                 size_t  comment_cap,
                                 bool    from_signal)
{
    @autoreleasepool
    {
        NSString* appName = [NSString stringWithUTF8String:app_name[0] ? app_name : "ZEngine"];
        NSString* signal  = [NSString stringWithUTF8String:signal_description];
        NSString* nsPath  = [NSString stringWithUTF8String:log_path];
        NSString* logText = [NSString stringWithContentsOfFile:nsPath
                                                      encoding:NSUTF8StringEncoding
                                                         error:nil];
        if (!logText) logText = @"(log unavailable)";

        // Two call paths reach here:
        //
        //  A) OnCrash() path (from_signal == false):
        //     OnCrash() was called from the main thread and is pumping
        //     [NSRunLoop mainRunLoop] in a tight loop.  dispatch_sync to the
        //     main queue is safe — the main thread will drain it.
        //
        //  B) Signal path (from_signal == true):
        //     The crashing thread is parked in sigsuspend; the worker thread
        //     called us.  No one is pumping the main queue, so dispatch_sync
        //     would deadlock.  Instead we create a fresh NSThread and bootstrap
        //     NSApp there.  In a crash scenario the OS does not enforce AppKit
        //     thread affinity, so this works reliably on macOS 10.12+.
        __block bool      didSend = false;
        __block NSString* comment = @"";

        if (!from_signal)
        {
            // Path A: OnCrash() is pumping the main run loop — dispatch_sync is safe.
            dispatch_sync(dispatch_get_main_queue(), ^{
                @autoreleasepool
                {
                    if (NSApp == nil)
                    {
                        [NSApplication sharedApplication];
                        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                        [NSApp finishLaunching];
                    }
                    else
                    {
                        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                    }
                    ZEngineCrashWindowController* ctrl =
                        [[ZEngineCrashWindowController alloc] initWithAppName:appName
                                                                       signal:signal
                                                                      logText:logText];
                    [NSApp activateIgnoringOtherApps:YES];
                    [ctrl runModal];
                    didSend = ctrl.userDidSend;
                    comment = ctrl.userComment;
                }
            });
        }
        else
        {
            // Path B: signal path — the crashing thread is parked in sigsuspend.
            // NSWindow must be created and shown on the main thread (macOS 14+ strictly
            // enforces this). Use dispatch_async to the main queue, then spin a private
            // CFRunLoop on this worker thread to drain it until the dialog closes.
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            dispatch_async(dispatch_get_main_queue(), ^{
                @autoreleasepool
                {
                    if (NSApp == nil)
                    {
                        [NSApplication sharedApplication];
                        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                        [NSApp finishLaunching];
                    }
                    else
                    {
                        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                    }
                    ZEngineCrashWindowController* ctrl =
                        [[ZEngineCrashWindowController alloc] initWithAppName:appName
                                                                       signal:signal
                                                                      logText:logText];
                    [NSApp activateIgnoringOtherApps:YES];
                    [ctrl runModal];
                    didSend = ctrl.userDidSend;
                    comment = ctrl.userComment;
                    dispatch_semaphore_signal(sem);
                }
            });
            // Pump the main run loop from this worker thread so the dispatch_async
            // block above can execute (the main thread is stuck in sigsuspend).
            while (dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) != 0)
                [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }

        if (didSend && comment.length > 0)
            snprintf(out_comment, comment_cap, "%s", comment.UTF8String);

        return didSend;
    }
}

namespace ZEngine::CrashHandlers
{
    using ZEngine::Helpers::secure_strlen;

    static constexpr int kTrackedSignals[]   = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP, SIGABRT, SIGSYS};
    static constexpr int kNumTrackedSignals  = sizeof(kTrackedSignals) / sizeof(kTrackedSignals[0]);
    static constexpr int kMaxBacktraceFrames = 64;

    struct WorkerThreadParams
    {
        int         SignalNumber                        = 0;
        char        SignalOrException[512]              = {};
        void*       BacktraceAddrs[kMaxBacktraceFrames] = {};
        int         BacktraceSize                       = 0;
        char        LogPath[kMaxPathLen]                = {};
        bool        BacktraceFromSignal                 = false;
    };

    static constexpr size_t   kAltStackSize = 32 * 1024;
    static struct sigaction   s_previous_actions[NSIG] = {};
    static char               s_alt_stack_mem[kAltStackSize];
    static int                s_crash_pipe[2] = {-1, -1};
    static WorkerThreadParams s_crash_params  = {};
    static pthread_t          s_worker_thread;

    static const char* SignalToString(int sig)
    {
        switch (sig)
        {
            case SIGSEGV: return "Segmentation Fault (SIGSEGV)";
            case SIGBUS:  return "Bus Error (SIGBUS)";
            case SIGFPE:  return "Floating Point Exception (SIGFPE)";
            case SIGILL:  return "Illegal Instruction (SIGILL)";
            case SIGTRAP: return "Trace Trap (SIGTRAP)";
            case SIGABRT: return "Abort Signal (SIGABRT)";
            case SIGSYS:  return "Bad System Call (SIGSYS)";
            default:      return "Unknown Signal";
        }
    }

    static void SafeWrite(int fd, cstring str)
    {
        if (fd < 0 || !str)
            return;
        size_t len = secure_strlen(str);
        while (len > 0)
        {
            ssize_t written = write(fd, str, len);
            if (written <= 0)
                break;
            len -= static_cast<size_t>(written);
            str += written;
        }
    }

    static bool HasGUISession()
    {
        if (getenv("ZENGINE_CRASH_NO_DIALOG"))
            return false;
        if (getenv("CI"))
            return false;
        // Check for a live window server connection. This works whether the
        // process was launched from a terminal, an IDE, or directly by launchd.
        // It returns false in headless SSH sessions and pure CI environments.
        return CGMainDisplayID() != 0;
    }

    static void WriteSystemInfo(int fd)
    {
        struct utsname uts = {};
        uname(&uts);

        // macOS marketing version from sysctl kern.osproductversion
        char os_version[64] = {};
        {
            size_t len = sizeof(os_version);
            sysctlbyname("kern.osproductversion", os_version, &len, nullptr, 0);
        }

        // CPU brand string from sysctl machdep.cpu.brand_string (x86_64) or
        // hw.targettype / hw.model (arm64)
        char cpu_model[256] = "(unknown)";
        {
            size_t len = sizeof(cpu_model);
            if (sysctlbyname("machdep.cpu.brand_string", cpu_model, &len, nullptr, 0) != 0)
                sysctlbyname("hw.model", cpu_model, &len, nullptr, 0);
        }

        char line_buf[512];
        snprintf(line_buf, sizeof(line_buf), "OS:        macOS %s (%s %s)\n",
                 os_version[0] ? os_version : "unknown", uts.sysname, uts.release);
        SafeWrite(fd, line_buf);
        snprintf(line_buf, sizeof(line_buf), "Arch:      %s\n", uts.machine);
        SafeWrite(fd, line_buf);
        snprintf(line_buf, sizeof(line_buf), "CPU:       %s\n", cpu_model);
        SafeWrite(fd, line_buf);
    }

    static void WriteStackTrace(int fd, void* const* addrs, int count)
    {
        char** symbols = backtrace_symbols(addrs, count);
        char   line_buf[512];
        for (int i = 0; i < count; ++i)
        {
            Dl_info info    = {};
            int     written = 0;
            if (dladdr(addrs[i], &info) && info.dli_sname)
            {
                int         status    = 0;
                char*       demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
                const char* name      = (status == 0 && demangled) ? demangled : info.dli_sname;
                written               = snprintf(line_buf, sizeof(line_buf), "\t#%-2d  %s  [%p]\n", i, name, addrs[i]);
                free(demangled);
            }
            else if (symbols && symbols[i])
                written = snprintf(line_buf, sizeof(line_buf), "\t#%-2d  %s\n", i, symbols[i]);
            else
                written = snprintf(line_buf, sizeof(line_buf), "\t#%-2d  [%p]\n", i, addrs[i]);
            if (written > 0)
                SafeWrite(fd, line_buf);
        }
        free(symbols);
    }

    static void WriteModuleList(int fd)
    {
        uint32_t count = _dyld_image_count();
        char     line_buf[512];
        for (uint32_t i = 0; i < count; ++i)
        {
            const char* name = _dyld_get_image_name(i);
            if (!name) continue;
            int written = snprintf(line_buf, sizeof(line_buf), "\t%s\n", name);
            if (written > 0)
                SafeWrite(fd, line_buf);
        }
    }

    static void* CrashWorkerThreadFn(void*)
    {
        uint8_t byte = 0;
        ssize_t r;
        do { r = read(s_crash_pipe[0], &byte, 1); } while (r == -1 && errno == EINTR);

        if (byte == 0)
            return nullptr;

        WorkerThreadParams* params = &s_crash_params;
        if (!params->BacktraceFromSignal && params->BacktraceSize == 0)
            params->BacktraceSize = backtrace(params->BacktraceAddrs, kMaxBacktraceFrames);

        {
            time_t     t_now = time(nullptr);
            struct tm* t_utc = gmtime(&t_now);
            char       t_str[32] = {};
            strftime(t_str, sizeof(t_str), "%Y-%m-%d_%H-%M-%S", t_utc);
            snprintf(params->LogPath, sizeof(params->LogPath), "%s/crash_%s.log",
                     g_state.CrashLogDir, t_str);
        }

        if (g_state.PreCrashFn)
        {
            struct CallbackSync
            {
                pthread_mutex_t          mutex;
                pthread_cond_t           cond;
                bool                     done;
                CrashHandler::PreCrashFn fn;
                void*                    ctx;
            };

            CallbackSync sync;
            pthread_mutex_init(&sync.mutex, nullptr);
            pthread_cond_init(&sync.cond, nullptr);
            sync.done = false;
            sync.fn   = g_state.PreCrashFn;
            sync.ctx  = g_state.PreCrashCtx;

            pthread_t callback_thread;
            pthread_create(
                &callback_thread, nullptr,
                [](void* p) -> void* {
                    auto* s = static_cast<CallbackSync*>(p);
                    s->fn(s->ctx);
                    pthread_mutex_lock(&s->mutex);
                    s->done = true;
                    pthread_cond_signal(&s->cond);
                    pthread_mutex_unlock(&s->mutex);
                    return nullptr;
                },
                &sync);

            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += kCallbackTimeoutSec;

            pthread_mutex_lock(&sync.mutex);
            while (!sync.done)
            {
                if (pthread_cond_timedwait(&sync.cond, &sync.mutex, &deadline) == ETIMEDOUT)
                    break;
            }
            bool completed = sync.done;
            pthread_mutex_unlock(&sync.mutex);

            if (completed)
            {
                pthread_join(callback_thread, nullptr);
                pthread_mutex_destroy(&sync.mutex);
                pthread_cond_destroy(&sync.cond);
            }
            else
            {
                pthread_detach(callback_thread);
            }
        }

        int log_fd = open(params->LogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd >= 0)
        {
            char       time_buf[64] = {};
            time_t     now          = time(nullptr);
            struct tm* tm_info      = gmtime(&now);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S UTC", tm_info);
            char log_buf[512];

            SafeWrite(log_fd, "==================================================================\n");
            SafeWrite(log_fd, "ZEngine Crash Report\n");
            SafeWrite(log_fd, "==================================================================\n");
            snprintf(log_buf, sizeof(log_buf), "App:       %s %s\n", g_state.AppName, g_state.Version);
            SafeWrite(log_fd, log_buf);
            snprintf(log_buf, sizeof(log_buf), "Date:      %s\n", time_buf);
            SafeWrite(log_fd, log_buf);
            WriteSystemInfo(log_fd);
            snprintf(log_buf, sizeof(log_buf), "Signal:    %s\n\n", params->SignalOrException);
            SafeWrite(log_fd, log_buf);

            SafeWrite(log_fd, "==================================================================\n");
            SafeWrite(log_fd, "Stack Trace (most recent call first):\n");
            SafeWrite(log_fd, "==================================================================\n");
            WriteStackTrace(log_fd, params->BacktraceAddrs, params->BacktraceSize);

            SafeWrite(log_fd, "\n==================================================================\n");
            SafeWrite(log_fd, "Loaded Images:\n");
            SafeWrite(log_fd, "==================================================================\n");
            WriteModuleList(log_fd);

            SafeWrite(log_fd, "==================================================================\n");
            SafeWrite(log_fd, "End of Report\n");
            SafeWrite(log_fd, "==================================================================\n");
            close(log_fd);
        }

        if (HasGUISession())
        {
            bool sent = ShowCrashDialogCocoa(g_state.AppName,
                                             params->SignalOrException,
                                             params->LogPath,
                                             g_state.UserComment,
                                             kMaxCommentLen,
                                             /*from_signal=*/params->BacktraceFromSignal);
#ifdef ZENGINE_CRASH_UPLOAD
            if (sent)
                g_state.UserConsentUpload = true;
            // TODO(jeanphilippekernel): Implement crash report upload functionality here.
#else
            (void) sent;
#endif
        }

        if (g_state.UserComment[0] != '\0')
        {
            int comment_fd = open(params->LogPath, O_WRONLY | O_APPEND, 0644);
            if (comment_fd >= 0)
            {
                SafeWrite(comment_fd, "\n==================================================================\n");
                SafeWrite(comment_fd, "User Comment:\n");
                SafeWrite(comment_fd, "==================================================================\n");
                SafeWrite(comment_fd, g_state.UserComment);
                SafeWrite(comment_fd, "\n");
                close(comment_fd);
            }
        }

        char stderr_buf[512];
        snprintf(stderr_buf, sizeof(stderr_buf), "\n[ZEngine] CRASH: %s\nReport: %s\n",
                 params->SignalOrException, params->LogPath);
        fputs(stderr_buf, stderr);

        _exit(EXIT_FAILURE);
        return nullptr;
    }

    static void SignalHandler(int sig, siginfo_t* /*info*/, void* ctx)
    {
        s_crash_params.SignalNumber      = sig;
        snprintf(s_crash_params.SignalOrException, sizeof(s_crash_params.SignalOrException), "%s", SignalToString(sig));

        s_crash_params.BacktraceSize       = backtrace(s_crash_params.BacktraceAddrs, kMaxBacktraceFrames);
        s_crash_params.BacktraceFromSignal = true;

        if (ctx)
        {
            auto* uc = static_cast<ucontext_t*>(ctx);
#if defined(__x86_64__)
            if (s_crash_params.BacktraceSize > 0)
                s_crash_params.BacktraceAddrs[0] = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rip);
#elif defined(__aarch64__)
            if (s_crash_params.BacktraceSize > 0)
                s_crash_params.BacktraceAddrs[0] = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__pc);
#endif
        }

        uint8_t byte = 1;
        write(s_crash_pipe[1], &byte, 1);
        sigset_t all;
        sigfillset(&all);
        while (true)
            sigsuspend(&all);
    }

    void CrashHandler::Install(cstring app_name, cstring version, cstring crash_log_dir)
    {
#ifdef ZENGINE_CRASH_HANDLER_ENABLED
        if (g_state.Installed)
            return;

        StoreMetadata(app_name, version, crash_log_dir);
        mkdir(crash_log_dir, 0755);

        if (pipe(s_crash_pipe) != 0)
        {
            fputs("[CrashHandler] Failed to create crash notification pipe.\n", stderr);
            return;
        }

        stack_t alt_stack  = {};
        alt_stack.ss_sp    = s_alt_stack_mem;
        alt_stack.ss_size  = sizeof(s_alt_stack_mem);
        alt_stack.ss_flags = 0;
        sigaltstack(&alt_stack, nullptr);

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        pthread_create(&s_worker_thread, &attr, CrashWorkerThreadFn, nullptr);
        pthread_attr_destroy(&attr);

        struct sigaction sa = {};
        sa.sa_sigaction     = SignalHandler;
        sa.sa_flags         = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);

        for (int i = 0; i < kNumTrackedSignals; ++i)
            sigaction(kTrackedSignals[i], &sa, &s_previous_actions[kTrackedSignals[i]]);

        g_state.Installed = true;
#endif
    }

    void CrashHandler::Uninstall()
    {
#ifdef ZENGINE_CRASH_HANDLER_ENABLED
        if (!g_state.Installed)
            return;

        for (int i = 0; i < kNumTrackedSignals; ++i)
            sigaction(kTrackedSignals[i], &s_previous_actions[kTrackedSignals[i]], nullptr);

        uint8_t byte = 0;
        write(s_crash_pipe[1], &byte, 1);
        pthread_join(s_worker_thread, nullptr);

        close(s_crash_pipe[0]);
        close(s_crash_pipe[1]);
        s_crash_pipe[0] = s_crash_pipe[1] = -1;

        g_state.Installed = false;
#endif
    }

    void CrashHandler::StoreMetadata(cstring app_name, cstring version, cstring crash_log_dir)
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

    [[noreturn]] void CrashHandler::OnCrash(cstring signal_or_exception, void* /*ctx*/)
    {
        static std::atomic<bool> s_in_crash_handler{false};
        if (s_in_crash_handler.exchange(true, std::memory_order_acq_rel))
        {
            fputs("[CrashHandler] FATAL: crash handler reentered. Aborting.\n", stderr);
            _exit(EXIT_FAILURE);
        }

        // Capture the call-site stack here, on the calling thread, before waking
        // the worker.  BacktraceFromSignal stays false so the worker skips its own
        // (meaningless) backtrace and uses these addresses instead.
        s_crash_params.BacktraceSize = backtrace(s_crash_params.BacktraceAddrs, kMaxBacktraceFrames);

        snprintf(s_crash_params.SignalOrException, sizeof(s_crash_params.SignalOrException), "%s", signal_or_exception ? signal_or_exception : "");
        uint8_t byte = 1;
        write(s_crash_pipe[1], &byte, 1);
        // Pump the main run loop so the dispatch_sync block posted by the
        // worker thread (which shows the Cocoa dialog) can execute here on
        // the main thread.  The worker calls _exit() when the dialog closes.
        while (true)
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    [[noreturn]] void CrashHandler::OnAssertionFailure(cstring file, int line, cstring message)
    {
        char assertion_message[1024];
        snprintf(assertion_message, sizeof(assertion_message),
                 "Assertion Failure: %s\nFile: %s\nLine: %d",
                 message ? message : "(null)", file ? file : "(null)", line);
        OnCrash(assertion_message, nullptr);
    }

} // namespace ZEngine::CrashHandlers
