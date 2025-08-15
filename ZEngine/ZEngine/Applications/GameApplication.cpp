#include <pch.h>
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
        OnUpdate(dt);
    }

    void GameApplication::ProcessEvent(Core::CoreEvent& e)
    {
        if (CurrentWindow)
        {
            CurrentWindow->OnEvent(e);
        }
        OnEvent(e);
    }

    void GameApplication::Render()
    {
        RenderPipeline->BeginFrame();

        OnPreRender();
        RenderPipeline->RenderScene();
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