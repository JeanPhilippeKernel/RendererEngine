#include <ZEngine/CrashHandlers/CrashHandlerInternal.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ZEngine::CrashHandlers
{
    using ZEngine::Helpers::secure_strlen;

    static constexpr int kTrackedSignals[]   = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP, SIGABRT, SIGSYS};
    static constexpr int kNumTrackedSignals  = sizeof(kTrackedSignals) / sizeof(kTrackedSignals[0]);
    static constexpr int kMaxBacktraceFrames = 64;

    struct WorkerThreadParams
    {
        int     SignalNumber                        = 0;
        cstring SignalOrException                   = nullptr;
        void*   BacktraceAddrs[kMaxBacktraceFrames] = {};
        int     BacktraceSize                       = 0;
        char    LogPath[kMaxPathLen]                = {};
        bool    BacktraceFromSignal                 = false;
    };

    static constexpr size_t   kAltStackSize            = 32 * 1024;
    static struct sigaction   s_previous_actions[NSIG] = {};
    static char               s_alt_stack_mem[kAltStackSize];
    static int                s_crash_pipe[2] = {-1, -1};
    static WorkerThreadParams s_crash_params  = {};
    static pthread_t          s_worker_thread;

    static const char*        SignalToString(int sig)
    {
        switch (sig)
        {
            case SIGSEGV:
                return "Segmentation Fault (SIGSEGV)";
            case SIGBUS:
                return "Bus Error (SIGBUS)";
            case SIGFPE:
                return "Floating Point Exception (SIGFPE)";
            case SIGILL:
                return "Illegal Instruction (SIGILL)";
            case SIGTRAP:
                return "Trace Trap (SIGTRAP)";
            case SIGABRT:
                return "Abort Signal (SIGABRT)";
            case SIGSYS:
                return "Bad System Call (SIGSYS)";
            default:
                return "Unknown Signal";
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
        if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY"))
            return false;
        return true;
    }

    typedef void  GtkWidget;
    typedef void  GtkTextBuffer;
    typedef void  GtkStyleContext;
    typedef void  GtkStyleProvider;
    typedef void  PangoFontDescription;
    typedef void  GtkCssProvider;
    typedef char  gchar;
    typedef int   gint;
    typedef int   gboolean;
    typedef void* gpointer;
    typedef long  gssize;

    // GtkTextIter is a value type — its ABI size on 64-bit Linux is 80 bytes.
    struct GtkTextIter
    {
        alignas(8) char _[80];
    };

    enum : unsigned int
    {
        kGtkWindowToplevel   = 0,
        kGtkOrientationHoriz = 0,
        kGtkOrientationVert  = 1,
        kGtkPolicyAutomatic  = 1,
        kGtkWrapNone         = 0,
        kGtkWrapWordChar     = 3,
        kGtkWinPosCenter     = 1,
        kGtkButtonBoxEnd     = 4, // GTK_BUTTONBOX_END (SPREAD=1,EDGE=2,START=3,END=4)
        kGtkStyleProviderApp = 600,
        kGConnectSwapped     = 2,       // G_CONNECT_SWAPPED
        kGSignalMatchFunc    = 1u << 3, // G_SIGNAL_MATCH_FUNC (ID=1,DETAIL=2,CLOSURE=4,FUNC=8,DATA=16)
        kGSignalMatchData    = 1u << 4, // G_SIGNAL_MATCH_DATA
    };

    struct GtkFns
    {
        gboolean (*init_check)(int*, char***);
        GtkWidget* (*window_new)(int);
        void (*window_set_title)(GtkWidget*, const char*);
        void (*window_set_default_size)(GtkWidget*, int, int);
        void (*window_set_resizable)(GtkWidget*, gboolean);
        void (*window_set_position)(GtkWidget*, int);
        void (*window_set_default)(GtkWidget*, GtkWidget*);
        void (*container_set_border_width)(GtkWidget*, unsigned int);
        void (*container_add)(GtkWidget*, GtkWidget*);
        GtkWidget* (*box_new)(int, int);
        void (*box_pack_start)(GtkWidget*, GtkWidget*, gboolean, gboolean, unsigned int);
        void (*box_set_spacing)(GtkWidget*, int);
        GtkWidget* (*label_new)(const char*);
        void (*label_set_markup)(GtkWidget*, const char*);
        void (*label_set_xalign)(GtkWidget*, float);
        void (*label_set_line_wrap)(GtkWidget*, gboolean);
        GtkWidget* (*scrolled_window_new)(void*, void*);
        void (*scrolled_window_set_policy)(GtkWidget*, int, int);
        GtkWidget* (*text_view_new)();
        void (*text_view_set_wrap_mode)(GtkWidget*, int);
        void (*text_view_set_editable)(GtkWidget*, gboolean);
        void (*text_view_set_cursor_visible)(GtkWidget*, gboolean);
        GtkTextBuffer* (*text_view_get_buffer)(GtkWidget*);
        gboolean (*text_view_scroll_to_iter)(GtkWidget*, GtkTextIter*, double, gboolean, double, double);
        void (*text_buffer_get_end_iter)(GtkTextBuffer*, GtkTextIter*);
        void (*text_buffer_insert)(GtkTextBuffer*, GtkTextIter*, const char*, gint);
        void (*text_buffer_get_bounds)(GtkTextBuffer*, GtkTextIter*, GtkTextIter*);
        gchar* (*text_buffer_get_text)(GtkTextBuffer*, GtkTextIter*, GtkTextIter*, gboolean);
        void (*widget_set_size_request)(GtkWidget*, int, int);
        void (*widget_set_can_default)(GtkWidget*, gboolean);
        void (*widget_show_all)(GtkWidget*);
        void (*widget_destroy)(GtkWidget*);
        GtkStyleContext* (*widget_get_style_context)(GtkWidget*);
        void (*style_context_add_class)(GtkStyleContext*, const char*);
        void (*style_context_add_provider)(GtkStyleContext*, GtkStyleProvider*, unsigned int);
        GtkWidget* (*button_box_new)(int);
        void (*button_box_set_layout)(GtkWidget*, int);
        GtkWidget* (*button_new_with_label)(const char*);
        PangoFontDescription* (*pango_font_desc_from_string)(const char*);
        void (*widget_override_font)(GtkWidget*, PangoFontDescription*);
        void (*pango_font_desc_free)(PangoFontDescription*);
        GtkCssProvider* (*css_provider_new)();
        gboolean (*css_provider_load_from_data)(GtkCssProvider*, const char*, gssize, void*);
        void (*g_object_unref)(gpointer);
        unsigned long (*g_signal_connect_data)(gpointer, const char*, void*, gpointer, void*, unsigned int);
        unsigned int (*g_signal_handlers_disconnect_matched)(gpointer, unsigned int, unsigned int, unsigned int, void*, void*, void*);
        gchar* (*g_markup_escape_text)(const char*, gssize);
        void (*g_free)(gpointer);
        void (*main_loop)();
        void (*main_quit)();
    };

    static bool LoadGtkFns(void* handle, GtkFns& g)
    {
#define GLOAD(sym, field)                      \
    *(void**) (&g.field) = dlsym(handle, sym); \
    if (!g.field)                              \
    return false

        GLOAD("gtk_init_check", init_check);
        GLOAD("gtk_window_new", window_new);
        GLOAD("gtk_window_set_title", window_set_title);
        GLOAD("gtk_window_set_default_size", window_set_default_size);
        GLOAD("gtk_window_set_resizable", window_set_resizable);
        GLOAD("gtk_window_set_position", window_set_position);
        GLOAD("gtk_window_set_default", window_set_default);
        GLOAD("gtk_container_set_border_width", container_set_border_width);
        GLOAD("gtk_container_add", container_add);
        GLOAD("gtk_box_new", box_new);
        GLOAD("gtk_box_pack_start", box_pack_start);
        GLOAD("gtk_box_set_spacing", box_set_spacing);
        GLOAD("gtk_label_new", label_new);
        GLOAD("gtk_label_set_markup", label_set_markup);
        GLOAD("gtk_label_set_xalign", label_set_xalign);
        GLOAD("gtk_label_set_line_wrap", label_set_line_wrap);
        GLOAD("gtk_scrolled_window_new", scrolled_window_new);
        GLOAD("gtk_scrolled_window_set_policy", scrolled_window_set_policy);
        GLOAD("gtk_text_view_new", text_view_new);
        GLOAD("gtk_text_view_set_wrap_mode", text_view_set_wrap_mode);
        GLOAD("gtk_text_view_set_editable", text_view_set_editable);
        GLOAD("gtk_text_view_set_cursor_visible", text_view_set_cursor_visible);
        GLOAD("gtk_text_view_get_buffer", text_view_get_buffer);
        GLOAD("gtk_text_view_scroll_to_iter", text_view_scroll_to_iter);
        GLOAD("gtk_text_buffer_get_end_iter", text_buffer_get_end_iter);
        GLOAD("gtk_text_buffer_insert", text_buffer_insert);
        GLOAD("gtk_text_buffer_get_bounds", text_buffer_get_bounds);
        GLOAD("gtk_text_buffer_get_text", text_buffer_get_text);
        GLOAD("gtk_widget_set_size_request", widget_set_size_request);
        GLOAD("gtk_widget_set_can_default", widget_set_can_default);
        GLOAD("gtk_widget_show_all", widget_show_all);
        GLOAD("gtk_widget_destroy", widget_destroy);
        GLOAD("gtk_widget_get_style_context", widget_get_style_context);
        GLOAD("gtk_style_context_add_class", style_context_add_class);
        GLOAD("gtk_style_context_add_provider", style_context_add_provider);
        GLOAD("gtk_button_box_new", button_box_new);
        GLOAD("gtk_button_box_set_layout", button_box_set_layout);
        GLOAD("gtk_button_new_with_label", button_new_with_label);
        GLOAD("pango_font_description_from_string", pango_font_desc_from_string);
        GLOAD("gtk_widget_override_font", widget_override_font);
        GLOAD("pango_font_description_free", pango_font_desc_free);
        GLOAD("gtk_css_provider_new", css_provider_new);
        GLOAD("gtk_css_provider_load_from_data", css_provider_load_from_data);
        GLOAD("g_object_unref", g_object_unref);
        GLOAD("g_signal_connect_data", g_signal_connect_data);
        GLOAD("g_signal_handlers_disconnect_matched", g_signal_handlers_disconnect_matched);
        GLOAD("g_markup_escape_text", g_markup_escape_text);
        GLOAD("g_free", g_free);
        GLOAD("gtk_main", main_loop);
        GLOAD("gtk_main_quit", main_quit);
#undef GLOAD
        return true;
    }

    static bool ShowCrashDialogGTK(const GtkFns& gtk, cstring app_name, cstring signal_description, cstring log_path, char* out_comment, size_t comment_cap)
    {
        int argc = 0;
        if (!gtk.init_check(&argc, nullptr))
            return false;

        GtkWidget* window = gtk.window_new(kGtkWindowToplevel);
        gtk.window_set_title(window, "Application Crash");
        gtk.window_set_default_size(window, 720, 510);
        gtk.window_set_resizable(window, 0);
        gtk.window_set_position(window, kGtkWinPosCenter);
        gtk.container_set_border_width(window, 20);

        GtkWidget* vbox = gtk.box_new(kGtkOrientationVert, 0);
        gtk.container_add(window, vbox);

        char header_text[512];
        {
            gchar* escaped = gtk.g_markup_escape_text(app_name, -1);
            snprintf(header_text, sizeof(header_text), "<b>An application has crashed: %s</b>", escaped);
            gtk.g_free(escaped);
        }
        GtkWidget* header_label = gtk.label_new(nullptr);
        gtk.label_set_markup(header_label, header_text);
        gtk.label_set_xalign(header_label, 0.0f);
        gtk.box_pack_start(vbox, header_label, 0, 0, 0);

        char desc_text[512];
        snprintf(desc_text, sizeof(desc_text), "The application encountered an unexpected error and could not continue.\n\nSignal: %s", signal_description);
        GtkWidget* desc_label = gtk.label_new(desc_text);
        gtk.label_set_xalign(desc_label, 0.0f);
        gtk.label_set_line_wrap(desc_label, 1);
        gtk.box_pack_start(vbox, desc_label, 0, 0, 10);

        GtkWidget* comment_hint = gtk.label_new("Describe what you were doing when the crash occurred (optional):");
        gtk.label_set_xalign(comment_hint, 0.0f);
        gtk.box_pack_start(vbox, comment_hint, 0, 0, 6);

        GtkWidget* comment_scroll = gtk.scrolled_window_new(nullptr, nullptr);
        gtk.scrolled_window_set_policy(comment_scroll, kGtkPolicyAutomatic, kGtkPolicyAutomatic);
        gtk.widget_set_size_request(comment_scroll, -1, 90);
        GtkWidget* comment_view = gtk.text_view_new();
        gtk.text_view_set_wrap_mode(comment_view, kGtkWrapWordChar);
        gtk.container_add(comment_scroll, comment_view);
        gtk.box_pack_start(vbox, comment_scroll, 0, 0, 0);

        GtkWidget* log_caption = gtk.label_new("Crash log:");
        gtk.label_set_xalign(log_caption, 0.0f);
        gtk.style_context_add_class(gtk.widget_get_style_context(log_caption), "dim-label");
        gtk.box_pack_start(vbox, log_caption, 0, 0, 8);

        GtkWidget* log_scroll = gtk.scrolled_window_new(nullptr, nullptr);
        gtk.scrolled_window_set_policy(log_scroll, kGtkPolicyAutomatic, kGtkPolicyAutomatic);
        gtk.widget_set_size_request(log_scroll, -1, 200);
        GtkWidget* log_view = gtk.text_view_new();
        gtk.text_view_set_editable(log_view, 0);
        gtk.text_view_set_cursor_visible(log_view, 0);
        gtk.text_view_set_wrap_mode(log_view, kGtkWrapNone);

        PangoFontDescription* mono_font = gtk.pango_font_desc_from_string("Monospace 10");
        gtk.widget_override_font(log_view, mono_font);
        gtk.pango_font_desc_free(mono_font);

        GtkCssProvider* css = gtk.css_provider_new();
        gtk.css_provider_load_from_data(css, "textview { background-color: #1e1e1e; color: #d4d4d4; }", -1, nullptr);
        gtk.style_context_add_provider(gtk.widget_get_style_context(log_view), (GtkStyleProvider*) css, kGtkStyleProviderApp);
        gtk.g_object_unref(css);

        {
            FILE* f = fopen(log_path, "r");
            if (f)
            {
                char           chunk[4096];
                size_t         n;
                GtkTextBuffer* buf = gtk.text_view_get_buffer(log_view);
                while ((n = fread(chunk, 1, sizeof(chunk) - 1, f)) > 0)
                {
                    chunk[n]        = '\0';
                    GtkTextIter end = {};
                    gtk.text_buffer_get_end_iter(buf, &end);
                    gtk.text_buffer_insert(buf, &end, chunk, (gint) n);
                }
                fclose(f);
            }
        }
        gtk.container_add(log_scroll, log_view);
        gtk.box_pack_start(vbox, log_scroll, 1, 1, 0);

        GtkWidget* btn_box = gtk.button_box_new(kGtkOrientationHoriz);
        gtk.button_box_set_layout(btn_box, kGtkButtonBoxEnd);
        gtk.box_set_spacing(btn_box, 8);
        gtk.box_pack_start(vbox, btn_box, 0, 0, 12);

        GtkWidget* close_btn = gtk.button_new_with_label("Close Without Sending");
        GtkWidget* send_btn  = gtk.button_new_with_label("Send and Close");
        gtk.widget_set_can_default(send_btn, 1);
        gtk.box_pack_start(btn_box, close_btn, 0, 0, 0);
        gtk.box_pack_start(btn_box, send_btn, 0, 0, 0);

        struct State
        {
            bool          did_send;
            GtkWidget*    window;
            GtkWidget*    comment_view;
            const GtkFns* gtk_ptr;
        };
        State state = {false, window, comment_view, &gtk};

        gtk.g_signal_connect_data(
            close_btn,
            "clicked",
            (void*) +[](State* s) {
                s->did_send = false;
                s->gtk_ptr->main_quit();
            },
            &state,
            nullptr,
            kGConnectSwapped);

        gtk.g_signal_connect_data(
            send_btn,
            "clicked",
            (void*) +[](State* s) {
                s->did_send = true;
                s->gtk_ptr->main_quit();
            },
            &state,
            nullptr,
            kGConnectSwapped);

        gtk.g_signal_connect_data(window, "destroy", (void*) gtk.main_quit, nullptr, nullptr, 0);

        gtk.widget_show_all(window);
        gtk.window_set_default(window, send_btn);

        // scroll to bottom only after show_all — widget must be realized first
        {
            GtkTextBuffer* buf  = gtk.text_view_get_buffer(log_view);
            GtkTextIter    last = {};
            gtk.text_buffer_get_end_iter(buf, &last);
            gtk.text_view_scroll_to_iter(log_view, &last, 0.0, 0, 0.0, 1.0);
        }

        gtk.main_loop();

        if (state.did_send)
        {
            GtkTextBuffer* buf      = gtk.text_view_get_buffer(comment_view);
            GtkTextIter    start_it = {}, end_it = {};
            gtk.text_buffer_get_bounds(buf, &start_it, &end_it);
            gchar* text = gtk.text_buffer_get_text(buf, &start_it, &end_it, 0);
            if (text && text[0] != '\0')
                snprintf(out_comment, comment_cap, "%s", text);
            gtk.g_free(text);
        }

        // disconnect before destroy — gtk_main() has returned; firing main_quit on a dead loop warns
        gtk.g_signal_handlers_disconnect_matched(window, kGSignalMatchFunc | kGSignalMatchData, 0, 0, nullptr, (void*) gtk.main_quit, nullptr);
        gtk.widget_destroy(window);
        return state.did_send;
    }

    static bool ShowCrashDialog(cstring log_path, cstring signal_description)
    {
        void* handle = dlopen("libgtk-3.so.0", RTLD_LAZY | RTLD_LOCAL);
        if (handle)
        {
            GtkFns gtk = {};
            if (LoadGtkFns(handle, gtk))
            {
                bool sent = ShowCrashDialogGTK(gtk, g_state.AppName, signal_description, log_path, g_state.UserComment, kMaxCommentLen);
                dlclose(handle);
                return sent;
            }
            dlclose(handle);
        }

        char cmd[kMaxPathLen * 4];

        if (system("command -v zenity >/dev/null 2>&1") == 0)
        {
            snprintf(
                cmd,
                sizeof(cmd),
                "zenity --text-info --title=\"Application Crash\""
                " --filename=\"%s\" --width=600 --height=400"
                " --ok-label=\"Send Report\" --cancel-label=\"Don't Send\""
                " 2>/dev/null",
                log_path);
            if (system(cmd) != 0)
                return false;

            snprintf(
                cmd,
                sizeof(cmd),
                "zenity --entry --title=\"Add Comment\""
                " --text=\"Optional: describe what you were doing:\""
                " --entry-text=\"\" 2>/dev/null");
            FILE* pipe = popen(cmd, "r");
            if (pipe)
            {
                fgets(g_state.UserComment, kMaxCommentLen, pipe);
                pclose(pipe);
                size_t len = secure_strlen(g_state.UserComment);
                if (len > 0 && g_state.UserComment[len - 1] == '\n')
                    g_state.UserComment[len - 1] = '\0';
            }
            return true;
        }

        if (system("command -v kdialog >/dev/null 2>&1") == 0)
        {
            snprintf(
                cmd,
                sizeof(cmd),
                "kdialog --yesno \"The application encountered an unexpected error."
                "\\n\\nSignal: %s\\n\\nCrash report: %s\""
                " --title \"Application Crash\" 2>/dev/null",
                signal_description,
                log_path);
            if (system(cmd) != 0)
                return false;

            snprintf(
                cmd,
                sizeof(cmd),
                "kdialog --inputbox \"Optional: describe what you were doing:\""
                " \"\" --title \"Add Comment\" 2>/dev/null");
            FILE* pipe = popen(cmd, "r");
            if (pipe)
            {
                fgets(g_state.UserComment, kMaxCommentLen, pipe);
                pclose(pipe);
                size_t len = secure_strlen(g_state.UserComment);
                if (len > 0 && g_state.UserComment[len - 1] == '\n')
                    g_state.UserComment[len - 1] = '\0';
            }
            return true;
        }

        snprintf(
            cmd,
            sizeof(cmd),
            "xmessage -center \"The application encountered an unexpected error."
            "\\n\\nSignal: %s\\n\\nCrash report: %s\" 2>/dev/null",
            signal_description,
            log_path);
        system(cmd);
        return false;
    }

    static void WriteSystemInfo(int fd)
    {
        struct utsname uts = {};
        uname(&uts);

        char  cpu_model[256] = "(unknown)";
        FILE* cpuinfo        = fopen("/proc/cpuinfo", "r");
        if (cpuinfo)
        {
            char line[256];
            while (fgets(line, sizeof(line), cpuinfo))
            {
                if (strncmp(line, "model name", 10) == 0)
                {
                    const char* colon = strchr(line, ':');
                    if (colon)
                    {
                        ++colon;
                        while (*colon == ' ' || *colon == '\t')
                            ++colon;
                        snprintf(cpu_model, sizeof(cpu_model), "%s", colon);
                        size_t len = secure_strlen(cpu_model);
                        if (len > 0 && cpu_model[len - 1] == '\n')
                            cpu_model[len - 1] = '\0';
                    }
                    break;
                }
            }
            fclose(cpuinfo);
        }

        char line_buf[512];
        snprintf(line_buf, sizeof(line_buf), "OS:        %s %s (%s)\n", uts.sysname, uts.release, uts.version);
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

    struct ModuleCallbackData
    {
        int fd = -1;
    };

    static int WriteModuleCallback(struct dl_phdr_info* info, size_t /*size*/, void* data)
    {
        auto* cb = static_cast<ModuleCallbackData*>(data);
        char  line_buf[512];
        int   written = snprintf(line_buf, sizeof(line_buf), "\t%s | Load Addr: %p\n", (info->dlpi_name && info->dlpi_name[0]) ? info->dlpi_name : "<main>", reinterpret_cast<void*>(info->dlpi_addr));
        if (written > 0)
            SafeWrite(cb->fd, line_buf);
        return 0;
    }

    static void* CrashWorkerThreadFn(void*)
    {
        uint8_t byte = 0;
        ssize_t r;
        do
        {
            r = read(s_crash_pipe[0], &byte, 1);
        } while (r == -1 && errno == EINTR);

        if (byte == 0)
            return nullptr;

        WorkerThreadParams* params = &s_crash_params;
        if (!params->BacktraceFromSignal && params->BacktraceSize == 0)
            params->BacktraceSize = backtrace(params->BacktraceAddrs, kMaxBacktraceFrames);

        {
            time_t     t_now     = time(nullptr);
            struct tm* t_utc     = gmtime(&t_now);
            char       t_str[32] = {};
            strftime(t_str, sizeof(t_str), "%Y-%m-%d_%H-%M-%S", t_utc);
            snprintf(params->LogPath, sizeof(params->LogPath), "%s/crash_%s.log", g_state.CrashLogDir, t_str);
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
                &callback_thread,
                nullptr,
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
                // PreCrashFn timed out — detach so the thread cleans up when it
                // eventually exits.  Do not destroy mutex/cond here; _exit()
                // terminates all threads before they can access freed stack memory.
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
            SafeWrite(log_fd, "Loaded Modules:\n");
            SafeWrite(log_fd, "==================================================================\n");
            ModuleCallbackData cb_data{log_fd};
            dl_iterate_phdr(WriteModuleCallback, &cb_data);

            SafeWrite(log_fd, "==================================================================\n");
            SafeWrite(log_fd, "End of Report\n");
            SafeWrite(log_fd, "==================================================================\n");
            close(log_fd);
        }

        if (HasGUISession())
        {
#ifdef ZENGINE_CRASH_UPLOAD
            if (ShowCrashDialog(params->LogPath, params->SignalOrException))
                g_state.UserConsentUpload = true;
            // TODO(jeanphilippekernel): implement crash report upload
#else
            ShowCrashDialog(params->LogPath, params->SignalOrException);
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
        snprintf(stderr_buf, sizeof(stderr_buf), "\n[ZEngine] CRASH: %s\nReport: %s\n", params->SignalOrException, params->LogPath);
        fputs(stderr_buf, stderr);

        _exit(EXIT_FAILURE);
        return nullptr;
    }

    static void SignalHandler(int sig, siginfo_t* /*info*/, void* ctx)
    {
        s_crash_params.SignalNumber        = sig;
        s_crash_params.SignalOrException   = SignalToString(sig);

        // Capture the crashing thread's backtrace here, in the signal handler,
        // where the crashing thread's stack is live.  The worker thread's stack
        // would be meaningless in a crash log.
        s_crash_params.BacktraceSize       = backtrace(s_crash_params.BacktraceAddrs, kMaxBacktraceFrames);
        s_crash_params.BacktraceFromSignal = true;

        // If the ucontext is available, overwrite frame 0 with the actual fault
        // address so the log points at the instruction that faulted, not at the
        // signal trampoline.
        if (ctx)
        {
            auto* uc = static_cast<ucontext_t*>(ctx);
#if defined(__x86_64__)
            if (s_crash_params.BacktraceSize > 0)
                s_crash_params.BacktraceAddrs[0] = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
            if (s_crash_params.BacktraceSize > 0)
                s_crash_params.BacktraceAddrs[0] = reinterpret_cast<void*>(uc->uc_mcontext.pc);
#endif
        }

        uint8_t byte = 1;
        write(s_crash_pipe[1], &byte, 1);
        sigset_t all;
        sigfillset(&all);
        while (true)
            sigsuspend(&all);
    }

    void CrashHandler::Install(const char* app_name, const char* version, const char* crash_log_dir)
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
        g_state.Installed                 = false;
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

    [[noreturn]] void CrashHandler::OnCrash(const char* signal_or_exception, void* /*ctx*/)
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
        s_crash_params.BacktraceSize     = backtrace(s_crash_params.BacktraceAddrs, kMaxBacktraceFrames);

        s_crash_params.SignalOrException = signal_or_exception;
        uint8_t byte                     = 1;
        write(s_crash_pipe[1], &byte, 1);
        pthread_join(s_worker_thread, nullptr);
        _exit(EXIT_FAILURE);
    }

    [[noreturn]] void CrashHandler::OnAssertionFailure(const char* file, int line, const char* message)
    {
        char assertion_message[1024];
        snprintf(assertion_message, sizeof(assertion_message), "Assertion Failure: %s\nFile: %s\nLine: %d", message ? message : "(null)", file ? file : "(null)", line);
        OnCrash(assertion_message, nullptr);
    }
} // namespace ZEngine::CrashHandlers
