#include <Engine.h>
#include <GameApplication.h>

namespace ZEngine::Applications
{
    void GameApplication::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        Arena = arena;

        State = ZPushStructCtor(Arena, ApplicationState);

        OnInitializing();
        OverrideWindowConfiguration();

        Engine::Initialize(Arena, &WindowCfg, this);

        OnInitialized();
    }

    void GameApplication::Run()
    {
        Engine::Run();
    }

    void GameApplication::Update(Core::TimeStep dt)
    {
        if (CameraController)
        {
            CameraController->Update(dt);
        }

        OnUpdate(dt);
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

    void GameApplication::Render()
    {
        RenderTargetResizeRequest request = {};
        if (State->RenderTargetResizeRequests.Pop(request))
        {
            RenderPipeline->ResizeRenderTarget(request.Width, request.Height);
        }

        RenderPipeline->BeginFrame();

        OnPreRender();
        RenderPipeline->RenderScene(CameraController->GetCamera(), CurrentScene);
        OnPostRender();

        if (EnableRenderOverlay)
        {
            RenderPipeline->BeginOverlayFrame();
            OnRenderUI();
            RenderPipeline->EndOverlayFrame();
        }

        RenderPipeline->EndFrame();
    }

    void GameApplication::Shutdown()
    {
        OnClosing();

        Engine::Dispose();

        OnClosed();
    }
} // namespace ZEngine::Applications
