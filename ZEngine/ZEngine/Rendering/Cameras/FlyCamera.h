#pragma once
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Quaternion.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Cameras/FlyCameraInput.h>
#include <ZEngine/Rendering/Scenes/SceneRayQuery.h>

namespace ZEngine::Rendering::Cameras
{
    struct FlyCameraHooks
    {
        /// @brief Opaque owner passed to every hook; hooks never allocate.
        void* Context                                                                                                                  = nullptr;

        /// @brief Finds the closest scene hit along a world-space ray.
        /// @details Misses are explicit through SceneRaycastHit::Hit.
        Scenes::SceneRaycastHit (*Raycast)(void* context, Core::Maths::Vec3f origin, Core::Maths::Vec3f direction, float max_distance) = nullptr;

        /// @brief Retrieves world-space bounds for the current F-to-frame target.
        /// @returns True only when out_center and out_radius were written.
        bool (*GetSelectionBounds)(void* context, Core::Maths::Vec3f& out_center, float& out_radius)                                   = nullptr;

        /// @brief Retrieves world-space bounds covering every frameable scene object.
        /// @returns True only when out_center and out_radius were written.
        bool (*GetSceneBounds)(void* context, Core::Maths::Vec3f& out_center, float& out_radius)                                       = nullptr;

        /// @brief Retrieves an optional spherical editor-navigation boundary.
        /// @details The camera stays on or outside out_radius around out_center
        /// when this returns true. This keeps editor policy out of FlyCamera:
        /// clients may use it for an analytic planet surface or leave it unset.
        bool (*GetGroundConstraint)(void* context, Core::Maths::Vec3f& out_center, float& out_radius)                                  = nullptr;
    };

    struct FlyCamera : public Camera
    {
        FlyCameraInput Input = {};
        FlyCameraHooks Hooks = {};
        FlyCameraState State = FlyCameraState::Free;

        FlyCamera()          = default;
        explicit FlyCamera(float aspectRatio, const CameraSetting& settings);
        virtual ~FlyCamera() = default;

        void                     OnUpdate(float dt);
        void                     SetViewportSize(float logicalW, float logicalH);
        void                     SetPosition(Core::Maths::Vec3f position);
        void                     SetOrientation(float pitchDeg, float yawDeg);
        /// @brief Switch between perspective and orthographic projection without moving the camera.
        void                     SetProjectionType(CameraType type);
        /// @brief Return the active projection type.
        [[nodiscard]] CameraType GetProjectionType() const;
        /// @brief Align the view to a world axis while retaining the current position.
        void                     SetAxisView(FlyCameraAxisView view);

        void                     FocusOn(Core::Maths::Vec3f center, float radius);
        void                     FocusOn(Core::Maths::Vec3f point);
        void                     FocusOn(Core::Maths::Vec3f aabbMin, Core::Maths::Vec3f aabbMax);

        void                     SaveBookmark(int slot);
        void                     RecallBookmark(int slot);

        struct Ray
        {
            Core::Maths::Vec3f Origin;
            Core::Maths::Vec3f Direction;
        };
        Ray                            GetRayFromViewport(float viewportX, float viewportY) const;

        CameraFrameData                CaptureFrameData() override;
        virtual Core::Maths::Vec3f     GetPosition() const override;
        virtual Core::Maths::Vec3f     GetForward() const override;
        virtual Core::Maths::Vec3f     GetUp() const override;
        virtual Core::Maths::Vec3f     GetRight() const override;
        Core::Maths::Quaternion<float> GetOrientation() const;

    private:
        void               UpdateFree(float dt);
        void               UpdateOrbit(float dt);
        void               UpdatePan(float dt);
        void               UpdateAnimation(float dt);
        void               HandleCommands();
        void               ApplyLookDelta(float speed);
        void               RecalculateView();
        void               RecalculateProjection();
        Core::Maths::Vec3f KeyboardMoveDir() const;
        float              AdaptiveSpeed() const;
        float              OrbitCollide(float desired) const;
        Core::Maths::Vec3f ConstrainPositionToGround(Core::Maths::Vec3f position) const;
        void               ApplyGroundConstraint();

        Core::Maths::Vec3f m_targetPos          = {0.0f, 5.0f, 10.0f};
        float              m_targetPitch        = 0.0f;
        float              m_targetYaw          = 0.0f;
        float              m_logicalW           = 1280.0f;
        float              m_logicalH           = 720.0f;

        Core::Maths::Vec3f m_orbitPivot         = {};
        float              m_orbitDist          = 10.0f;
        float              m_targetOrbitDist    = 10.0f;
        float              m_orthographicHeight = 10.0f;

        FlyCameraState     m_stateBeforePan     = FlyCameraState::Free;
        FlyCameraState     m_stateBeforeAnim    = FlyCameraState::Free;

        Core::Maths::Vec3f m_animStartPos       = {};
        Core::Maths::Vec3f m_animEndPos         = {};
        float              m_animStartPitch     = 0.0f;
        float              m_animStartYaw       = 0.0f;
        float              m_animEndPitch       = 0.0f;
        float              m_animEndYaw         = 0.0f;
        float              m_animTimer          = 0.0f;
        float              m_animDuration       = 0.25f;

        bool               m_projDirty          = true;
        bool               m_viewDirty          = true;

        void               UpdateMatrices();

        struct BookmarkSlot
        {
            bool               Valid = false;
            Core::Maths::Vec3f Pos   = {};
            float              Pitch = 0.0f;
            float              Yaw   = 0.0f;
        };
        BookmarkSlot m_bookmarks[9] = {};
    };
    ZDEFINE_PTR(FlyCamera);
} // namespace ZEngine::Rendering::Cameras
