#pragma once
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Controllers/ICameraController.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/TimeStep.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <ZEngine/Windows/WindowConfiguration.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Applications
{
    struct RenderTargetResizeRequest
    {
        uint32_t Width  = 0;
        uint32_t Height = 0;
    };

    struct ApplicationState
    {
        Helpers::ThreadSafeQueue<RenderTargetResizeRequest> RenderTargetResizeRequests = {};
    };

    ZDEFINE_PTR(ApplicationState);

    struct GameApplication
    {

        bool                              EnableRenderOverlay = false;
        cstring                           ConfigFile          = nullptr;
        cstring                           WorkingSpacePath    = nullptr;
        Windows::WindowConfiguration      WindowCfg           = {};

        Core::Memory::MemoryManager*      Memory              = nullptr;
        Windows::CoreWindowPtr            CurrentWindow       = nullptr;
        ApplicationStatePtr               State               = nullptr;
        AppRenderPipelinePtr              RenderPipeline      = nullptr;
        Controllers::ICameraControllerPtr CameraController    = nullptr;
        Rendering::Scenes::RenderScenePtr CurrentScene        = nullptr;

        void                              Initialize(Core::Memory::MemoryManager* memory);
        void                              Update(Core::TimeStep dt);
        void                              ProcessEvent(Core::CoreEvent&);
        void                              Run();
        void                              PrepareScene(RenderPayload&);
        void                              Shutdown();

        virtual void                      OverrideWindowConfiguration() = 0;

        virtual void                      OnInitializing()              = 0;
        virtual void                      OnInitialized()               = 0;

        virtual void                      OnEvent(Core::CoreEvent&)     = 0;
        virtual void                      OnUpdate(float dt)            = 0;

        virtual void                      OnPreRender()                 = 0;
        virtual void                      OnPostRender()                = 0;
        virtual void                      OnRenderUI()                  = 0;

        virtual void                      OnClosing()                   = 0;
        virtual void                      OnClosed()                    = 0;
    };
    ZDEFINE_PTR(GameApplication);

} // namespace ZEngine::Applications
