#pragma once

#include <vulkan/vulkan.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <Window/CoreWindow.h>
#include <Window/WindowConfiguration.h>

namespace ZEngine::Window::GLFWWindow
{
    struct VulkanWindowFrame
    {
        int32_t         FrameIndex{-1};
        VkCommandPool   GraphicCommandPool{VK_NULL_HANDLE};
        VkCommandPool   TransferCommandPool{VK_NULL_HANDLE};
        VkCommandBuffer GraphicCommandBuffer{VK_NULL_HANDLE};
        std::vector<VkCommandBuffer> GraphicCommandBuffers;
        VkCommandBuffer TransferCommandBuffer{VK_NULL_HANDLE};
        VkFence         Fence{VK_NULL_HANDLE};
        VkImage         Backbuffer{VK_NULL_HANDLE};
        VkImageView     BackbufferView{VK_NULL_HANDLE};
        VkFramebuffer   Framebuffer{VK_NULL_HANDLE};
        VkSemaphore     ImageAcquiredSemaphore{VK_NULL_HANDLE};
        VkSemaphore     RenderCompleteSemaphore{VK_NULL_HANDLE};
    };

    struct WindowFramePrepareInfo
    {
        uint32_t FrameIndex;
        VkQueue  GraphicQueue;
        std::vector<VkCommandBuffer>& CommandBufferQueue;
    };

    class VulkanWindow : public CoreWindow
    {
    public:
        VulkanWindow(const WindowConfiguration& configuration);
        virtual ~VulkanWindow();

        uint32_t                      GetHeight() const override;
        uint32_t                      GetWidth() const override;
        std::string_view              GetTitle() const override;
        bool                          IsMinimized() const override;
        void                          SetTitle(std::string_view title) override;
        bool                          IsVSyncEnable() const override;
        void                          SetVSync(bool value) override;
        void                          SetCallbackFunction(const EventCallbackFn& callback) override;
        void*                         GetNativeWindow() const override;
        virtual const WindowProperty& GetWindowProperty() const override;

        virtual void  Initialize() override;
        virtual void  Deinitialize() override;
        virtual void  PollEvent() override;
        virtual float GetTime() override;
        virtual void  Update(Core::TimeStep delta_time) override;
        virtual void  Render() override;

        uint32_t                                       GetSwapChainMinImageCount() const;
        const std::vector<VkImage>&                    GetSwapChainImageCollection() const;
        const std::vector<VkImageView>&                GetSwapChainImageViewCollection() const;
        const std::vector<VkFramebuffer>&              GetFramebufferCollection() const;
        int32_t                                        GetCurrentWindowFrameIndex() const;
        VulkanWindowFrame&                             GetCurrentWindowFrame();
        const VulkanWindowFrame&                       GetCurrentWindowFrame() const;
        void                                           IncrementWindowFrameIndex(int32_t step = 1);
        std::vector<VulkanWindowFrame>&                GetWindowFrameCollection();
        VulkanWindowFrame&                             GetWindowFrame(uint32_t index);
        const VkExtent2D&                              GetSwapchainExtent() const;

        void RecreateSwapChain(VkSwapchainKHR old_swapchain, Hardwares::VulkanDevice& device);

        VkSurfaceKHR       GetSurface() const;
        VkSurfaceFormatKHR GetSurfaceFormat() const;
        VkPresentModeKHR   GetPresentMode() const;
        VkSwapchainKHR     GetSwapChain() const;
        VkRenderPass       GetRenderPass() const;

        bool HasSwapChainRebuilt() const;

    public:
        bool OnEvent(Event::CoreEvent& event) override;

    protected:
        virtual bool OnKeyPressed(Event::KeyPressedEvent&) override;
        virtual bool OnKeyReleased(Event::KeyReleasedEvent&) override;

        virtual bool OnMouseButtonPressed(Event::MouseButtonPressedEvent&) override;
        virtual bool OnMouseButtonReleased(Event::MouseButtonReleasedEvent&) override;
        virtual bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&) override;
        virtual bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&) override;

        virtual bool OnTextInputRaised(Event::TextInputEvent&) override;

        virtual bool OnWindowClosed(Event::WindowClosedEvent&) override;
        virtual bool OnWindowResized(Event::WindowResizedEvent&) override;
        virtual bool OnWindowMinimized(Event::WindowMinimizedEvent&) override;
        virtual bool OnWindowMaximized(Event::WindowMaximizedEvent&) override;
        virtual bool OnWindowRestored(Event::WindowRestoredEvent&) override;

    private:
        void MarkVulkanInternalObjectDirty(const Hardwares::VulkanDevice& device);

        static void __OnGlfwWindowClose(GLFWwindow*);
        static void __OnGlfwWindowResized(GLFWwindow*, int width, int height);
        static void __OnGlfwWindowMaximized(GLFWwindow*, int maximized);
        static void __OnGlfwWindowMinimized(GLFWwindow*, int minimized);

        static void __OnGlfwMouseButtonRaised(GLFWwindow*, int button, int action, int mods);
        static void __OnGlfwMouseScrollRaised(GLFWwindow*, double xoffset, double yoffset);
        static void __OnGlfwCursorMoved(GLFWwindow*, double xpos, double ypos);
        static void __OnGlfwTextInputRaised(GLFWwindow*, unsigned int character);

        static void __OnGlfwKeyboardRaised(GLFWwindow*, int key, int scancode, int action, int mods);

        static void __OnGlfwFrameBufferSizeChanged(GLFWwindow*, int width, int height);

    private:
        GLFWwindow* m_native_window{nullptr};

        VkSurfaceKHR                    m_vulkan_surface{VK_NULL_HANDLE};
        VkPresentModeKHR                m_vulkan_present_mode;
        VkSurfaceCapabilitiesKHR        m_surface_capabilities;
        std::vector<VkSurfaceFormatKHR> m_surface_format_collection;
        std::vector<VkPresentModeKHR>   m_present_mode_collection;
        VkExtent2D                      m_vulkan_extent_2d;
        VkSwapchainKHR                  m_swapchain{VK_NULL_HANDLE};
        VkSurfaceFormatKHR              m_surface_format;
        uint32_t                        m_min_image_count{0};
        VkImage                         m_depth_image{VK_NULL_HANDLE};
        VkDeviceMemory                  m_depth_image_device_memory{VK_NULL_HANDLE};
        VkImageView                     m_depth_image_view{VK_NULL_HANDLE};
        std::vector<VkImage>            m_swapchain_image_collection;
        std::vector<VkImageView>        m_swapchain_image_view_collection;
        std::vector<VkFramebuffer>      m_framebuffer_collection;
        VkRenderPass                    m_render_pass{VK_NULL_HANDLE};

        int32_t                        m_current_frame_index{-1};
        std::vector<VulkanWindowFrame> m_frame_collection;

        bool m_has_swap_chain_rebuilt{false};

    private:
        std::vector<VkCommandBuffer> m_command_buffer_collection;
    };

} // namespace ZEngine::Window::GLFWWindow
