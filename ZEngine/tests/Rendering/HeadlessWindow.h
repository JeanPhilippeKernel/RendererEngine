#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <ZEngine/ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <future>
#include <string>

namespace ZEngine::Tests
{
    // CoreWindow backed by VK_EXT_headless_surface — no display, no GPU output.
    // SetSize() lets tests simulate resize and minimize events.
    class HeadlessWindow : public Windows::CoreWindow
    {
    public:
        HeadlessWindow(Core::Memory::ArenaAllocator* arena, uint32_t width, uint32_t height) : m_width(width), m_height(height)
        {
            RequiredExtensionLayers.init(arena, 2);
            RequiredExtensionLayers.push(VK_KHR_SURFACE_EXTENSION_NAME);
            RequiredExtensionLayers.push(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
        }

        // Returns false on Apple (MoltenVK incomplete headless support) and when
        // the extension is absent from the loader.
        static bool IsSupported()
        {
#ifdef __APPLE__
            return false;
#else
            uint32_t count = 0;
            if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
                return false;
            auto* props = new VkExtensionProperties[count];
            vkEnumerateInstanceExtensionProperties(nullptr, &count, props);
            bool found = false;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (std::string_view(props[i].extensionName) == VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME)
                {
                    found = true;
                    break;
                }
            }
            delete[] props;
            return found;
#endif
        }

        void SetSize(uint32_t w, uint32_t h)
        {
            m_width  = w;
            m_height = h;
        }

        uint32_t GetWidth() const override
        {
            return m_width;
        }
        uint32_t GetHeight() const override
        {
            return m_height;
        }
        Core::Containers::StringView GetTitle() const override
        {
            return "headless";
        }
        void SetTitle(Core::Containers::StringView) override {}
        bool IsMinimized() const override
        {
            return m_width == 0 || m_height == 0;
        }
        bool IsVSyncEnable() const override
        {
            return false;
        }
        void                           SetVSync(bool) override {}
        void                           SetCallbackFunction(const EventCallbackFn&) override {}
        const Windows::WindowProperty& GetWindowProperty() const override
        {
            return m_prop;
        }
        void* GetNativeWindow() const override
        {
            return nullptr;
        }

        bool CreateSurface(void* instance, void** out_surface) override
        {
            VkHeadlessSurfaceCreateInfoEXT ci = {
                .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT,
                .pNext = nullptr,
                .flags = 0,
            };
            auto fn = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(vkGetInstanceProcAddr(static_cast<VkInstance>(instance), "vkCreateHeadlessSurfaceEXT"));
            if (!fn)
                return false;
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (fn(static_cast<VkInstance>(instance), &ci, nullptr, &surface) != VK_SUCCESS)
                return false;
            *out_surface = surface;
            return true;
        }

        std::future<std::string> OpenFileDialogAsync(std::span<std::string_view>) override
        {
            return std::async(std::launch::deferred, [] { return std::string{}; });
        }

        void  PollEvent() override {}
        float GetTime() override
        {
            return 0.f;
        }
        float GetDeltaTime() override
        {
            return 0.f;
        }

        bool OnWindowClosed(Windows::Events::WindowClosedEvent&) override
        {
            return false;
        }
        bool OnWindowResized(Windows::Events::WindowResizedEvent&) override
        {
            return false;
        }
        bool OnWindowMinimized(Windows::Events::WindowMinimizedEvent&) override
        {
            return false;
        }
        bool OnWindowMaximized(Windows::Events::WindowMaximizedEvent&) override
        {
            return false;
        }
        bool OnWindowRestored(Windows::Events::WindowRestoredEvent&) override
        {
            return false;
        }
        bool OnEvent(Core::CoreEvent&) override
        {
            return false;
        }

    private:
        uint32_t                m_width  = 0;
        uint32_t                m_height = 0;
        Windows::WindowProperty m_prop   = {};
    };

} // namespace ZEngine::Tests
