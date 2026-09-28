#pragma once

#if defined(__linux__)

#include <memory>
#include <span>
#include <string>
#include <string_view>

struct GLFWwindow;
struct wl_event_queue;
struct wl_registry;
struct zxdg_exported_v2;
struct zxdg_exporter_v2;

namespace ZEngine::Windows::Platform
{
    // Owns the xdg-foreign export for as long as the portal dialog is open.
    // Wayland invalidates the parent relation as soon as this object is destroyed.
    class WaylandPortalParent
    {
    public:
        ~WaylandPortalParent();

        WaylandPortalParent(const WaylandPortalParent&)                        = delete;
        WaylandPortalParent&             operator=(const WaylandPortalParent&) = delete;

        [[nodiscard]] const std::string& Handle() const;

    private:
        WaylandPortalParent() = default;

        bool                                        Initialize(GLFWwindow* window);

        static void                                 OnRegistryGlobal(void* context, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
        static void                                 OnExportedHandle(void* context, zxdg_exported_v2* exported, const char* handle);

        wl_event_queue*                             m_event_queue = nullptr;
        wl_registry*                                m_registry    = nullptr;
        zxdg_exporter_v2*                           m_exporter    = nullptr;
        zxdg_exported_v2*                           m_exported    = nullptr;
        std::string                                 m_handle;

        friend std::unique_ptr<WaylandPortalParent> CreateWaylandPortalParent(GLFWwindow* window);
    };

    [[nodiscard]] std::unique_ptr<WaylandPortalParent> CreateWaylandPortalParent(GLFWwindow* window);

    // Blocks the worker thread until the user accepts or cancels the portal.
    // The coroutine caller resumes on the engine's main thread afterwards.
    [[nodiscard]] std::string                          OpenWaylandPortalFileDialog(std::string_view parent_handle, std::span<const std::string> extensions, std::string_view default_directory, std::string_view title);
} // namespace ZEngine::Windows::Platform

#endif
