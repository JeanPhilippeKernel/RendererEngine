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
        /// @brief Update the viewport screen rect used for self-contained hover detection.
        void                          SetViewportRect(float x0, float y0, float x1, float y1) override;
        /// @brief Reset to Idle and unlock cursor — call when the app loses focus.
        void                          ResumeEventProcessing() override;
        void                          PauseEventProcessing() override;

    protected:
        /// @brief Camera interaction state.
        enum class CamState : uint8_t
        {
            Idle  = 0, ///< Cursor outside viewport — no input fed.
            Hover = 1, ///< Cursor inside viewport — scroll, pan, orbit active.
            Fly   = 2, ///< RMB held — cursor locked, full WASD + mouselook.
        };

        void                             EnterFly();
        void                             ExitFly();

        Input::InputManager*             m_input            = nullptr;
        CamState                         m_state            = CamState::Idle;
        float                            m_vp[4]            = {}; // viewport rect: x0, y0, x1, y1
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
