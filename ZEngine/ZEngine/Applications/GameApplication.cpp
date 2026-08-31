#include <GLFW/glfw3.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Logging/LoggerDefinition.h>

namespace ZEngine::Applications
{
    void GameApplication::Initialize(Core::Memory::MemoryManager* memory)
    {
        Memory = memory;

        State  = ZPushStructCtor(&Memory->MainArena, ApplicationState);

        OnInitializing();
        OverrideWindowConfiguration();

        Engine::Initialize(Memory, &WindowCfg, this);

        if (VFSBackend)
        {
            if (Engine::GetContext()->VFS->Mount(VFSBackend, Core::VFS::VFSPath::Root(), 0).Failed())
            {
                ZENGINE_CORE_ERROR("GameApplication: failed to mount VFSBackend")
            }
        }

        RenderPipeline = ZPushStructCtor(&Memory->MainArena, AppRenderPipeline);
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
        Engine::Dispose();
    }
} // namespace ZEngine::Applications
