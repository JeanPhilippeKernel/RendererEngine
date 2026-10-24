#pragma once
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Controllers/ICameraController.h>
#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/TimeStep.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <ZEngine/Windows/WindowConfiguration.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine
{
    struct EngineContext;
}

namespace ZEngine::Applications
{
    struct RenderTargetResizeRequest
    {
        uint32_t Width  = 0;
        uint32_t Height = 0;
    };

    struct ApplicationState
    {
        Core::Containers::MPSCQueue<RenderTargetResizeRequest, 128> RenderTargetResizeRequests = {};
    };

    ZDEFINE_PTR(ApplicationState);

    struct GameApplication
    {
        virtual ~GameApplication()                            = default;

        bool                              EnableRenderOverlay = false;
        cstring                           ConfigFile          = nullptr;
        cstring                           WorkingSpacePath    = nullptr;
        Core::VFS::IVFSBackend*           VFSBackend          = nullptr;
        Windows::WindowConfiguration      WindowCfg           = {};

        Core::Memory::MemoryManager*      Memory              = nullptr;
        Windows::CoreWindowPtr            CurrentWindow       = nullptr;
        ApplicationStatePtr               State               = nullptr;
        AppRenderPipelinePtr              RenderPipeline      = nullptr;
        Controllers::ICameraControllerPtr CameraController    = nullptr;
        Rendering::Scenes::RenderScenePtr CurrentScene        = nullptr;

        void                              Initialize(Core::Memory::MemoryManager* memory);
        void                              Update(Core::TimeStep dt);
        virtual void                      ProcessEvent(Core::CoreEvent&);
        void                              Run();
        virtual void                      PrepareScene(RenderFrameState&);
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

        /// Main-thread user close intent, not teardown. Return false to defer or
        /// cancel (for example, while awaiting unsaved-document consent).
        virtual bool                      OnCloseRequested()
        {
            return true;
        }

        /// Main-thread simulation policy, evaluated before any runtime ECS wave,
        /// deferred command flush or Actor::OnTick. Update/UI/render synchronization
        /// remain active while false. Applications run simulation by default.
        [[nodiscard]] virtual bool ShouldSimulate() const
        {
            return true;
        }

        /// Called after render/pool workers and queued main-thread callbacks stop,
        /// before ECS and asset/device services shut down. Applications owning a
        /// replacement world retire it here and restore engine-owned ECS pointers.
        virtual void OnWorldShutdown(EngineContext&) {}

    private:
        uint32_t m_render_target_width           = 0;
        uint32_t m_render_target_height          = 0;
        uint64_t m_render_target_resize_sequence = 0;
    };
    ZDEFINE_PTR(GameApplication);

} // namespace ZEngine::Applications
