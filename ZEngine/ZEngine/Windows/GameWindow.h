#pragma once
#include <GLFW/glfw3.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <ZEngine/Windows/WindowConfiguration.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Windows
{
    class GameWindow : public CoreWindow
    {
    public:
        GameWindow() {}
        virtual ~GameWindow();

        uint32_t                         GetHeight() const override;
        uint32_t                         GetWidth() const override;
        Core::Containers::StringView     GetTitle() const override;
        bool                             IsMinimized() const override;
        void                             SetTitle(Core::Containers::StringView title) override;
        bool                             IsVSyncEnable() const override;
        void                             SetVSync(bool value) override;
        void                             SetCallbackFunction(const EventCallbackFn& callback) override;
        void*                            GetNativeWindow() const override;
        virtual const WindowProperty&    GetWindowProperty() const override;

        virtual void                     Initialize(Core::Memory::ArenaAllocator* arena, const WindowConfiguration& cfg);

        virtual void                     PollEvent() override;
        virtual float                    GetTime() override;
        virtual float                    GetDeltaTime() override;

        virtual std::future<std::string> OpenFileDialogAsync(std::span<std::string_view> type_filters = {}, std::string_view default_dir = {}, std::string_view message = {}) override;

        virtual bool                     CreateSurface(void* instance, void** out_window_surface) override;

    public:
        bool OnEvent(Core::CoreEvent& event) override;

    protected:
        virtual bool OnWindowClosed(Events::WindowClosedEvent&) override;
        virtual bool OnWindowResized(Events::WindowResizedEvent&) override;
        virtual bool OnWindowMinimized(Events::WindowMinimizedEvent&) override;
        virtual bool OnWindowMaximized(Events::WindowMaximizedEvent&) override;
        virtual bool OnWindowRestored(Events::WindowRestoredEvent&) override;

        static void  __OnGlfwWindowClose(GLFWwindow*);
        static void  __OnGlfwWindowResized(GLFWwindow*, int width, int height);
        static void  __OnGlfwWindowMaximized(GLFWwindow*, int maximized);
        static void  __OnGlfwWindowMinimized(GLFWwindow*, int minimized);

        static void  __OnGlfwMouseButtonRaised(GLFWwindow*, int button, int action, int mods);
        static void  __OnGlfwMouseScrollRaised(GLFWwindow*, double xoffset, double yoffset);
        static void  __OnGlfwCursorMoved(GLFWwindow*, double xpos, double ypos);
        static void  __OnGlfwTextInputRaised(GLFWwindow*, unsigned int character);

        static void  __OnGlfwKeyboardRaised(GLFWwindow*, int key, int scancode, int action, int mods);

        static void  __OnGlfwFrameBufferSizeChanged(GLFWwindow*, int width, int height);

    private:
        GLFWwindow* m_native_window = nullptr;
    };

} // namespace ZEngine::Windows
