#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <Window/CoreWindow.h>
#include <Window/WindowConfiguration.h>
#include <Rendering/Swapchain.h>

namespace ZEngine::Window::GLFWWindow
{
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
        virtual void  InitializeLayer() override;
        virtual void  Deinitialize() override;
        virtual void  PollEvent() override;
        virtual float GetTime() override;
        virtual void  Update(Core::TimeStep delta_time) override;
        virtual void  Render() override;

        Ref<Rendering::Swapchain> GetSwapchain() const override;

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
        GLFWwindow*                               m_native_window{nullptr};
        Ref<Rendering::Swapchain>                 m_swapchain;
    };

} // namespace ZEngine::Window::GLFWWindow
