#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <Window/CoreWindow.h>
#include <Rendering/Graphics/GraphicContext.h>

namespace ZEngine::Window::GLFWWindow {

    class OpenGLWindow : public CoreWindow {
    public:
        OpenGLWindow(WindowProperty& prop);
        virtual ~OpenGLWindow();

        unsigned int GetHeight() const override {
            return m_property.Height;
        }
        unsigned int GetWidth() const override {
            return m_property.Width;
        }
        const std::string& GetTitle() const override {
            return m_property.Title;
        }

        bool IsVSyncEnable() const override {
            return m_property.VSync;
        }
        void SetVSync(bool value) override {
            m_property.VSync = value;
            if (value) {
                glfwSwapInterval(1);
            } else {
                glfwSwapInterval(0);
            }
        }

        void SetCallbackFunction(const EventCallbackFn& callback) override {
            m_property.CallbackFn = callback;
        }

        void* GetNativeWindow() const override {
            return m_native_window;
        }
        void* GetNativeContext() const override {
            return m_context->GetNativeContext();
        }

        virtual const WindowProperty& GetWindowProperty() const override {
            return m_property;
        }

        virtual void  Initialize() override;
        virtual void  PollEvent() override;
        virtual float GetTime() override {
            return (float) glfwGetTime();
        }
        virtual void Update(Core::TimeStep delta_time) override;
        virtual void Render() override;

    public:
        bool OnEvent(Event::CoreEvent& event) override {

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
        unsigned int                         m_desired_gl_context_major_version{4};
        unsigned int                         m_desired_gl_context_minor_version{5};
        GLFWwindow*                          m_native_window{nullptr};
        Rendering::Graphics::GraphicContext* m_context{nullptr};
    };

} // namespace ZEngine::Window::GLFWWindow
