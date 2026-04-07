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

    void GameApplication::PrepareScene(RenderPayload& payload)
    {
        RenderTargetResizeRequest request = {};
        if (State->RenderTargetResizeRequests.Pop(request))
        {
            payload.ResizeRenderTarget.value.store(true, std::memory_order_release);
            payload.RenderTargetW = request.Width;
            payload.RenderTargetH = request.Height;
        }

        payload.Scene  = CurrentScene;
        payload.Camera = CameraController->GetCamera();
    }

    void GameApplication::Shutdown()
    {
        OnClosing();

        Engine::Dispose();

        OnClosed();
    }
} // namespace ZEngine::Applications
