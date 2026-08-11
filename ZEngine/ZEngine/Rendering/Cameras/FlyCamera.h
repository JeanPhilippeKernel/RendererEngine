#pragma once
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Quaternion.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Cameras/FlyCameraInput.h>
#include <functional>
#include <utility>

namespace ZEngine::Rendering::Cameras
{
    struct FlyCameraHooks
    {
        // Returns hit distance along (origin, dir), or maxDist if no hit.
        std::function<float(Core::Maths::Vec3f, Core::Maths::Vec3f, float)> Raycast;
        // Returns {center, radius} of the current selection for F-to-frame.
        std::function<std::pair<Core::Maths::Vec3f, float>()>               GetSelectionBounds;
    };

    struct FlyCamera : public Camera
    {
        FlyCameraInput Input = {};
        FlyCameraHooks Hooks = {};
        FlyCameraState State = FlyCameraState::Free;

        FlyCamera()          = default;
        explicit FlyCamera(float aspectRatio, const CameraSetting& settings);
        virtual ~FlyCamera() = default;

        void OnUpdate(float dt);
        void SetViewportSize(float logicalW, float logicalH);
        void SetPosition(Core::Maths::Vec3f position);
        void SetOrientation(float pitchDeg, float yawDeg);

        void FocusOn(Core::Maths::Vec3f center, float radius);
        void FocusOn(Core::Maths::Vec3f point);
        void FocusOn(Core::Maths::Vec3f aabbMin, Core::Maths::Vec3f aabbMax);

        void SaveBookmark(int slot);
        void RecallBookmark(int slot);

        struct Ray
        {
            Core::Maths::Vec3f Origin;
            Core::Maths::Vec3f Direction;
        };
        Ray                            GetRayFromViewport(float viewportX, float viewportY) const;

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
        void               RecalculateView();
        void               RecalculateProjection();
        Core::Maths::Vec3f KeyboardMoveDir() const;
        float              AdaptiveSpeed() const;
        float              OrbitCollide(float desired) const;

        Core::Maths::Vec3f m_targetPos       = {0.0f, 5.0f, 10.0f};
        float              m_targetPitch     = 0.0f;
        float              m_targetYaw       = 0.0f;
        float              m_logicalW        = 1280.0f;
        float              m_logicalH        = 720.0f;

        Core::Maths::Vec3f m_orbitPivot      = {};
        float              m_orbitDist       = 10.0f;
        float              m_targetOrbitDist = 10.0f;

        FlyCameraState     m_stateBeforePan  = FlyCameraState::Free;
        FlyCameraState     m_stateBeforeAnim = FlyCameraState::Free;

        Core::Maths::Vec3f m_animStartPos    = {};
        Core::Maths::Vec3f m_animEndPos      = {};
        float              m_animStartPitch  = 0.0f;
        float              m_animStartYaw    = 0.0f;
        float              m_animEndPitch    = 0.0f;
        float              m_animEndYaw      = 0.0f;
        float              m_animTimer       = 0.0f;
        float              m_animDuration    = 0.25f;

        bool               m_projDirty       = true;
        bool               m_viewDirty       = true;

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
