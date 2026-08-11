#pragma once
#include <ZEngine/Controllers/ICameraController.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>

namespace ZEngine::Controllers
{
    struct FlyCameraController : public ICameraController
    {
        FlyCameraController()          = default;
        virtual ~FlyCameraController() = default;

        void                          Initialize(Input::InputManager* input_manager, Core::Memory::ArenaAllocator* arena);

        void                          Update(Core::TimeStep dt) override;
        bool                          OnEvent(Core::CoreEvent&) override;
        Rendering::Cameras::CameraPtr GetCamera() const override;
        Core::Maths::Vec3f            GetPosition() const override;
        void                          SetPosition(const Core::Maths::Vec3f&) override;
        void                          SetViewport(float logicalW, float logicalH) override;
        void                          SetViewportOrigin(float x, float y) override;
        void                          ResumeEventProcessing() override;
        void                          PauseEventProcessing() override;

    protected:
        Input::InputManager*             m_input            = nullptr;
        PaddedAtomic<bool>               m_active           = {.value = false};
        float                            m_viewportOriginX  = 0.0f;
        float                            m_viewportOriginY  = 0.0f;
        Rendering::Cameras::FlyCameraPtr m_camera           = nullptr;

        uint32_t                         m_slot_forward     = 0;
        uint32_t                         m_slot_right       = 0;
        uint32_t                         m_slot_up          = 0;
        uint32_t                         m_slot_scroll      = 0;
        uint32_t                         m_slot_rmb         = 0;
        uint32_t                         m_slot_mmb         = 0;
        uint32_t                         m_slot_lmb         = 0;
        uint32_t                         m_slot_alt         = 0;
        uint32_t                         m_slot_shift       = 0;
        uint32_t                         m_slot_ctrl        = 0;
        uint32_t                         m_slot_focus       = 0;
        uint32_t                         m_slot_bookmark[9] = {};
    };
    ZDEFINE_PTR(FlyCameraController);
} // namespace ZEngine::Controllers
