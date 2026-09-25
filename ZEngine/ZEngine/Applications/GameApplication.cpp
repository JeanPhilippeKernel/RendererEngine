#include <GLFW/glfw3.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Logging/LoggerDefinition.h>

namespace ZEngine::Applications
{
    void GameApplication::Initialize(Core::Memory::MemoryManager* memory)
    {
        Memory = memory;

        State  = ZPushStructCtor(&Memory->BootstrapArena, ApplicationState);

        OnInitializing();
        OverrideWindowConfiguration();

        Engine::Initialize(Memory, &WindowCfg, this);

        if (VFSBackend)
        {
            if (Engine::GetContext()->VFS->Mount(VFSBackend, Core::VFS::VFSPath::Root(), 0).Failed())
            {
                ZENGINE_CORE_ERROR("GameApplication: failed to mount VFSBackend")
            }
            else
            {
                // Only now is the project's own backend actually visible at "/" — the
                // watcher set up inside Engine::Initialize only reacts to changes from
                // here forward, so pre-existing assets need this explicit initial scan.
                static_cast<Core::VFS::VFSContext*>(Engine::GetContext()->VFS)->ScanProject();
            }
        }

        RenderPipeline = ZPushStructCtor(&Memory->BootstrapArena, AppRenderPipeline);
        RenderPipeline->Initialize(Engine::GetContext()->Device);

        OnInitialized();
    }

    void GameApplication::Run()
    {
        Engine::Run();
    }

    void GameApplication::Update(Core::TimeStep dt)
    {
        // Poll input once per frame before any system consumes it.
        auto* ctx = Engine::GetContext();
        if (ctx && ctx->InputManager && ctx->Window)
        {
            ctx->InputManager->Poll(static_cast<GLFWwindow*>(ctx->Window->GetNativeWindow()));
        }

        // OnUpdate runs first so subclasses (e.g. Editor) can gate the camera
        // controller (Resume/PauseEventProcessing) before Update consumes input.
        OnUpdate(dt);

        if (CameraController)
        {
            CameraController->Update(dt);
        }
    }

    void GameApplication::ProcessEvent(Core::CoreEvent& e)
    {
        if (CurrentWindow)
        {
            CurrentWindow->OnEvent(e);
        }

        if (CameraController)
        {
            CameraController->OnEvent(e);
        }

        OnEvent(e);
    }

    void GameApplication::PrepareScene(RenderFrameState& state)
    {
        RenderTargetResizeRequest request = {};
        // Panel layout can emit several intermediate extents while a dock or
        // window is dragged. Only the final extent is useful: each request
        // rebuilds viewport images after waiting for the device to become idle.
        while (State->RenderTargetResizeRequests.pop(request))
        {
            m_render_target_width  = request.Width;
            m_render_target_height = request.Height;
            ++m_render_target_resize_sequence;
        }

        state.Scene          = CurrentScene;
        state.Camera         = CameraController->GetCamera()->CaptureFrameData();
        state.RenderTargetW  = m_render_target_width;
        state.RenderTargetH  = m_render_target_height;
        state.ResizeSequence = m_render_target_resize_sequence;
    }

    void GameApplication::Shutdown()
    {
        if (CameraController)
            CameraController->PauseEventProcessing();
        Engine::Dispose();
    }
} // namespace ZEngine::Applications
