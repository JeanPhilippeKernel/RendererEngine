#pragma once
#include <AppRenderPipeline.h>
#include <Core/Memory/Allocator.h>
#include <Core/TimeStep.h>
#include <Windows/CoreWindow.h>
#include <Windows/WindowConfiguration.h>
#include <ZEngineDef.h>

namespace ZEngine::Applications
{
    struct ApplicationState
    {
    };
    ZDEFINE_PTR(ApplicationState);

    struct GameApplication
    {

        bool                          EnableRenderOverlay = false;
        cstring                       ConfigFile          = nullptr;
        Windows::WindowConfiguration  WindowCfg           = {};

        Core::Memory::ArenaAllocator* Arena               = nullptr;
        Windows::CoreWindowPtr        CurrentWindow       = nullptr;
        ApplicationStatePtr           State               = nullptr;
        AppRenderPipelinePtr          RenderPipeline      = nullptr;

        void                          Initialize(Core::Memory::ArenaAllocator* arena);
        void                          Update(Core::TimeStep dt);
        void                          ProcessEvent(Core::CoreEvent&);
        void                          Run();
        void                          Render();
        void                          Shutdown();

        virtual void                  OverrideWindowConfiguration() = 0;

        virtual void                  OnInitializing()              = 0;
        virtual void                  OnInitialized()               = 0;

        virtual void                  OnEvent(Core::CoreEvent&)     = 0;
        virtual void                  OnUpdate(float dt)            = 0;

        virtual void                  OnPreRender()                 = 0;
        virtual void                  OnPostRender()                = 0;
        virtual void                  OnRenderUI()                  = 0;

        virtual void                  OnClosing()                   = 0;
        virtual void                  OnClosed()                    = 0;
    };
    ZDEFINE_PTR(GameApplication);

} // namespace ZEngine::Applications
