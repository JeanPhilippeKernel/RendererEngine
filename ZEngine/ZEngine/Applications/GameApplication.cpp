#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Engine.h>

namespace ZEngine::Applications
{
    void GameApplication::Initialize(Core::Memory::MemoryManager* memory)
    {
        Memory = memory;

        State  = ZPushStructCtor(&Memory->MainArena, ApplicationState);

        // TODO: move to engine context
        VFS.Initialize(&Memory->MainArena);
        OnInitializing();
        OverrideWindowConfiguration();

        Engine::Initialize(&Memory->MainArena, &WindowCfg, this);

        OnInitialized();
    }

    Core::VFS::IVFSContext* GameApplication::GetVFSContext()
    {
        return &VFS;
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
