#pragma once

#include <vulkan/vulkan.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <Window/CoreWindow.h>
#include <Rendering/Graphics/GraphicContext.h>

namespace ZEngine::Window::GLFWWindow
{

    struct VulkanWindowFrame
    {
        VkCommandPool   CommandPool;
        VkCommandBuffer CommandBuffer;
        VkFence         Fence;
        VkImage         Backbuffer;
        VkImageView     BackbufferView;
        VkFramebuffer   Framebuffer;
    };

    struct VulkanWindowFrameSemaphore
    {
        VkSemaphore ImageAcquiredSemaphore;
        VkSemaphore RenderCompleteSemaphore;
    };

    class OpenGLWindow : public CoreWindow
    {
    public:
        OpenGLWindow(WindowProperty& prop, Hardwares::VulkanInstance& vulkan_instance);
        virtual ~OpenGLWindow();

        unsigned int GetHeight() const override
        {
            return m_property.Height;
        }

        unsigned int GetWidth() const override
        {
            return m_property.Width;
        }

        const std::string& GetTitle() const override
        {
            return m_property.Title;
        }

        bool IsMinimized() const 
        {
            return m_property.IsMinimized;
        }

        void SetTitle(std::string_view title) override
        {
            m_property.Title = title;
            glfwSetWindowTitle(m_native_window, m_property.Title.c_str());
        }

        bool IsVSyncEnable() const override
        {
            return m_property.VSync;
        }

        void SetVSync(bool value) override
        {
            m_property.VSync = value;
            if (value)
            {
                glfwSwapInterval(1);
            }
            else
            {
                glfwSwapInterval(0);
            }
        }

        void SetCallbackFunction(const EventCallbackFn& callback) override
        {
            m_property.CallbackFn = callback;
        }

        void* GetNativeWindow() const override
        {
            return m_native_window;
        }

        virtual const WindowProperty& GetWindowProperty() const override
        {
            return m_property;
        }

        virtual void  Initialize() override;
        virtual void  Deinitialize() override;
        virtual void  PollEvent() override;
        virtual float GetTime() override
        {
            return (float) glfwGetTime();
        }
        virtual void Update(Core::TimeStep delta_time) override;
        virtual void Render() override;

        uint32_t                                       GetSwapChainMinImageCount() const;
        const std::vector<VkImage>&                    GetSwapChainImageCollection() const;
        const std::vector<VkImageView>&                GetSwapChainImageViewCollection() const;
        const std::vector<VkFramebuffer>&              GetFramebufferCollection() const;
        int32_t                                        GetCurrentWindowFrameIndex() const;
        int32_t                                        GetCurrentWindowFrameSemaphoreIndex() const;
        void                                           IncrementWindowFrameSemaphoreIndex(int32_t step = 1);
        void                                           IncrementWindowFrameIndex(int32_t step = 1);
        std::vector<VulkanWindowFrame>&                GetWindowFrameCollection();
        VulkanWindowFrame&                             GetWindowFrame(uint32_t index);
        const std::vector<VulkanWindowFrameSemaphore>& GetWindowFrameSemaphoreCollection() const;
        VulkanWindowFrameSemaphore&                    GetWindowFrameSemaphore(uint32_t index);

        void RecreateSwapChain(VkSwapchainKHR old_swap_chain, const Hardwares::VulkanDevice& device);

        VkRenderPass       GetRenderPass() const;
        VkSurfaceKHR       GetSurface() const;
        VkSurfaceFormatKHR GetSurfaceFormat() const;
        VkPresentModeKHR   GetPresentMode() const;
        VkSwapchainKHR     GetSwapChain() const;


    public:
        bool OnEvent(Event::CoreEvent& event) override
        {

            Event::EventDispatcher event_dispatcher(event);
            event_dispatcher.Dispatch<Event::WindowClosedEvent>(std::bind(&OpenGLWindow::OnWindowClosed, this, std::placeholders::_1));
            event_dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&OpenGLWindow::OnWindowResized, this, std::placeholders::_1));

            event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&OpenGLWindow::OnKeyPressed, this, std::placeholders::_1));
            event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&OpenGLWindow::OnKeyReleased, this, std::placeholders::_1));

            event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&OpenGLWindow::OnMouseButtonPressed, this, std::placeholders::_1));
            event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&OpenGLWindow::OnMouseButtonReleased, this, std::placeholders::_1));
            event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&OpenGLWindow::OnMouseButtonMoved, this, std::placeholders::_1));
            event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&OpenGLWindow::OnMouseButtonWheelMoved, this, std::placeholders::_1));

            event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&OpenGLWindow::OnTextInputRaised, this, std::placeholders::_1));

            event_dispatcher.Dispatch<Event::WindowMinimizedEvent>(std::bind(&OpenGLWindow::OnWindowMinimized, this, std::placeholders::_1));
            event_dispatcher.Dispatch<Event::WindowMaximizedEvent>(std::bind(&OpenGLWindow::OnWindowMaximized, this, std::placeholders::_1));
            event_dispatcher.Dispatch<Event::WindowRestoredEvent>(std::bind(&OpenGLWindow::OnWindowRestored, this, std::placeholders::_1));

            return true;
        }

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
        std::vector<VkImage>            m_swapchain_image_collection;
        std::vector<VkImageView>        m_swapchain_image_view_collection;
        std::vector<VkFramebuffer>      m_framebuffer_collection;
        VkRenderPass                    m_render_pass{VK_NULL_HANDLE};

        int32_t                                 m_current_frame_index{-1};
        int32_t                                 m_current_frame_semaphore_index{-1};
        std::vector<VulkanWindowFrame>          m_frame_collection;
        std::vector<VulkanWindowFrameSemaphore> m_frame_semaphore_collection;
    };

} // namespace ZEngine::Window::GLFWWindow
