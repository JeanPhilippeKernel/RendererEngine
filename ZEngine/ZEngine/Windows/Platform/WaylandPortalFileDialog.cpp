#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Windows/Platform/WaylandPortalFileDialog.h>

#if defined(__linux__)

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <gio/gio.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>
#include "xdg-foreign-unstable-v2-client-protocol.h"

namespace ZEngine::Windows::Platform
{
    namespace
    {
        constexpr char kPortalService[]       = "org.freedesktop.portal.Desktop";
        constexpr char kPortalObjectPath[]    = "/org/freedesktop/portal/desktop";
        constexpr char kPortalFileChooser[]   = "org.freedesktop.portal.FileChooser";
        constexpr char kPortalRequest[]       = "org.freedesktop.portal.Request";
        constexpr char kPortalResponse[]      = "Response";
        constexpr char kSupportedFilesLabel[] = "Supported files";

        struct PortalRequest
        {
            GMainLoop*  Loop = nullptr;
            std::string Path;
            std::string SelectedPath;
            uint32_t    Response = 2;
        };

        std::atomic_uint64_t s_portal_token = 0;

        void                 LogError(const char* operation, GError* error)
        {
            ZENGINE_CORE_ERROR("[FileDialog] {}: {}", operation, error ? error->message : "unknown error")
            if (error)
                g_error_free(error);
        }

        GVariant* BuildPortalOptions(std::span<const std::string> extensions, std::string_view default_directory)
        {
            GVariantBuilder options;
            g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));

            const std::string handle_token = "zengine" + std::to_string(s_portal_token.fetch_add(1, std::memory_order_relaxed));
            g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(handle_token.c_str()));
            g_variant_builder_add(&options, "{sv}", "modal", g_variant_new_boolean(TRUE));

            if (!extensions.empty())
            {
                GVariantBuilder patterns;
                g_variant_builder_init(&patterns, G_VARIANT_TYPE("a(us)"));
                for (const std::string& extension : extensions)
                {
                    const std::string pattern = "*" + extension;
                    g_variant_builder_add(&patterns, "(us)", 0u, pattern.c_str());
                }

                GVariantBuilder filters;
                g_variant_builder_init(&filters, G_VARIANT_TYPE("a(sa(us))"));
                g_variant_builder_add(&filters, "(s@a(us))", kSupportedFilesLabel, g_variant_builder_end(&patterns));
                g_variant_builder_add(&options, "{sv}", "filters", g_variant_builder_end(&filters));
            }

            if (!default_directory.empty())
            {
                GVariantBuilder folder;
                g_variant_builder_init(&folder, G_VARIANT_TYPE("ay"));
                for (const unsigned char character : default_directory)
                    g_variant_builder_add(&folder, "y", character);
                g_variant_builder_add(&folder, "y", 0u);
                g_variant_builder_add(&options, "{sv}", "current_folder", g_variant_builder_end(&folder));
            }

            return g_variant_builder_end(&options);
        }

        void OnPortalResponse(GDBusConnection*, const gchar*, const gchar* object_path, const gchar*, const gchar*, GVariant* parameters, gpointer user_data)
        {
            auto* request = static_cast<PortalRequest*>(user_data);
            if (!object_path || request->Path != object_path)
                return;

            guint32   response = 1;
            GVariant* results  = nullptr;
            g_variant_get(parameters, "(u@a{sv})", &response, &results);
            request->Response = response;

            if (response == 0)
            {
                GVariant* uris = g_variant_lookup_value(results, "uris", G_VARIANT_TYPE("as"));
                if (uris)
                {
                    gsize   count  = 0;
                    gchar** values = g_variant_dup_strv(uris, &count);
                    if (count > 0)
                    {
                        gchar* native_path = g_filename_from_uri(values[0], nullptr, nullptr);
                        if (native_path)
                        {
                            request->SelectedPath = native_path;
                            g_free(native_path);
                        }
                    }
                    g_strfreev(values);
                    g_variant_unref(uris);
                }
            }

            g_variant_unref(results);
            g_main_loop_quit(request->Loop);
        }

        std::string ShellQuote(std::string_view value)
        {
            std::string quoted("'");
            for (char character : value)
            {
                if (character == '\'')
                    quoted += "'\"'\"'";
                else
                    quoted += character;
            }
            quoted += '\'';
            return quoted;
        }
    } // namespace

    WaylandPortalParent::~WaylandPortalParent()
    {
        if (m_exported)
            zxdg_exported_v2_destroy(m_exported);
        if (m_exporter)
            zxdg_exporter_v2_destroy(m_exporter);
        if (m_registry)
            wl_registry_destroy(m_registry);
        if (m_event_queue)
            wl_event_queue_destroy(m_event_queue);
    }

    const std::string& WaylandPortalParent::Handle() const
    {
        return m_handle;
    }

    bool WaylandPortalParent::Initialize(GLFWwindow* window)
    {
        if (!window || glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
            return false;

        wl_display* display = glfwGetWaylandDisplay();
        wl_surface* surface = glfwGetWaylandWindow(window);
        if (!display || !surface)
            return false;

        m_event_queue = wl_display_create_queue(display);
        if (!m_event_queue)
            return false;

        static const wl_registry_listener registry_listener = {
            .global        = &WaylandPortalParent::OnRegistryGlobal,
            .global_remove = nullptr,
        };
        static const zxdg_exported_v2_listener exported_listener = {
            .handle = &WaylandPortalParent::OnExportedHandle,
        };

        m_registry = wl_display_get_registry(display);
        if (!m_registry)
            return false;
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(m_registry), m_event_queue);
        wl_registry_add_listener(m_registry, &registry_listener, this);

        if (wl_display_roundtrip_queue(display, m_event_queue) < 0 || !m_exporter)
            return false;

        m_exported = zxdg_exporter_v2_export_toplevel(m_exporter, surface);
        if (!m_exported)
            return false;
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(m_exported), m_event_queue);
        zxdg_exported_v2_add_listener(m_exported, &exported_listener, this);

        return wl_display_roundtrip_queue(display, m_event_queue) >= 0 && !m_handle.empty();
    }

    void WaylandPortalParent::OnRegistryGlobal(void* context, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
    {
        auto* parent = static_cast<WaylandPortalParent*>(context);
        if (parent->m_exporter || std::strcmp(interface, zxdg_exporter_v2_interface.name) != 0)
            return;

        parent->m_exporter = static_cast<zxdg_exporter_v2*>(wl_registry_bind(registry, name, &zxdg_exporter_v2_interface, std::min(version, 1u)));
        if (parent->m_exporter)
            wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(parent->m_exporter), parent->m_event_queue);
    }

    void WaylandPortalParent::OnExportedHandle(void* context, zxdg_exported_v2*, const char* handle)
    {
        if (handle)
            static_cast<WaylandPortalParent*>(context)->m_handle = "wayland:" + std::string(handle);
    }

    std::unique_ptr<WaylandPortalParent> CreateWaylandPortalParent(GLFWwindow* window)
    {
        auto parent = std::unique_ptr<WaylandPortalParent>(new WaylandPortalParent{});
        if (!parent->Initialize(window))
            return {};
        return parent;
    }

    PortalFileDialogResult OpenPortalFileDialog(std::string_view parent_handle, std::span<const std::string> extensions, std::string_view default_directory, std::string_view title)
    {
        GMainContext* context = g_main_context_new();
        g_main_context_push_thread_default(context);

        GError*          error      = nullptr;
        GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!connection)
        {
            LogError("Unable to connect to the desktop portal", error);
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context);
            return {};
        }

        PortalRequest request;
        request.Loop                   = g_main_loop_new(context, FALSE);

        const guint       subscription = g_dbus_connection_signal_subscribe(connection, kPortalService, kPortalRequest, kPortalResponse, nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &OnPortalResponse, &request, nullptr);

        const std::string parent(parent_handle);
        const std::string dialog_title(title.empty() ? "Select a file" : title);
        GVariant*         reply = g_dbus_connection_call_sync(connection, kPortalService, kPortalObjectPath, kPortalFileChooser, "OpenFile", g_variant_new("(ss@a{sv})", parent.c_str(), dialog_title.c_str(), BuildPortalOptions(extensions, default_directory)), G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

        if (!reply)
        {
            LogError("Unable to open the desktop portal file picker", error);
        }
        else
        {
            const gchar* request_path = nullptr;
            g_variant_get(reply, "(&o)", &request_path);
            if (request_path)
                request.Path = request_path;
            g_variant_unref(reply);
            if (!request.Path.empty())
                g_main_loop_run(request.Loop);
            else
                ZENGINE_CORE_ERROR("[FileDialog] Desktop portal returned an invalid request path")
        }

        g_dbus_connection_signal_unsubscribe(connection, subscription);
        g_main_loop_unref(request.Loop);
        g_object_unref(connection);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
        if (request.Response == 0 && !request.SelectedPath.empty())
            return {PortalFileDialogStatus::Selected, std::move(request.SelectedPath)};
        if (request.Response == 1)
            return {PortalFileDialogStatus::Cancelled, {}};

        ZENGINE_CORE_ERROR("[FileDialog] Desktop portal returned an error response ({})", request.Response)
        return {};
    }

    std::string OpenLinuxFallbackFileDialog(unsigned long x11_parent_window, bool use_x11, std::span<const std::string> extensions, std::string_view default_directory, std::string_view title)
    {
        std::string filter;
        for (const std::string& extension : extensions)
        {
            if (!filter.empty())
                filter += ' ';
            filter += '*';
            filter += extension;
        }

        const std::string start_dir = default_directory.empty() ? "." : std::string(default_directory);
        const std::string dialog_title(title.empty() ? "Select a file" : title);

        std::string       command;
        if (system("command -v zenity >/dev/null 2>&1") == 0)
        {
            command  = use_x11 ? "GDK_BACKEND=x11 " : "";
            command += "zenity --file-selection --modal --title=" + ShellQuote(dialog_title);
            if (use_x11 && x11_parent_window != 0)
                command += " --attach=" + std::to_string(x11_parent_window);
            if (!default_directory.empty())
                command += " --filename=" + ShellQuote(start_dir + "/");
            if (!filter.empty())
                command += " --file-filter=" + ShellQuote(filter);
        }
        else if (system("command -v kdialog >/dev/null 2>&1") == 0)
        {
            command = "kdialog";
            if (use_x11 && x11_parent_window != 0)
                command += " --attach " + std::to_string(x11_parent_window);
            command += " --getopenfilename " + ShellQuote(start_dir);
            if (!filter.empty())
                command += " " + ShellQuote(filter);
        }
        else
        {
            ZENGINE_CORE_ERROR("[FileDialog] No Linux picker is available. Install xdg-desktop-portal, zenity, or kdialog")
            return {};
        }

        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe)
        {
            ZENGINE_CORE_ERROR("[FileDialog] Unable to launch the fallback picker")
            return {};
        }

        char        buffer[4096] = {};
        std::string selected_path;
        if (fgets(buffer, sizeof(buffer), pipe))
        {
            selected_path = buffer;
            if (!selected_path.empty() && selected_path.back() == '\n')
                selected_path.pop_back();
        }
        pclose(pipe);
        return selected_path;
    }
} // namespace ZEngine::Windows::Platform

#endif
