#pragma once
#include <Core/Maths/MathUtils.h>
#include <Core/Maths/Matrix.h>
#include <Core/Maths/Quaternion.h>
#include <Rendering/Cameras/Camera.h>
#include <functional>
#include <utility>

namespace ZEngine::Rendering::Cameras
{
    struct FlyCamera : public Camera
    {
        // Inject scene picking: return hit distance along (origin, dir), or desiredDist if no hit.
        std::function<float(Core::Maths::Vec3f, Core::Maths::Vec3f, float)> SceneRaycast;

        // Inject selection bounds for KEY_F framing: returns {center, radius}.
        // Falls back to world origin / radius 5 when unset.
        std::function<std::pair<Core::Maths::Vec3f, float>()>               OnGetSelectionBounds;

        FlyCamera() = default;
        FlyCamera(float aspectRatio, const CameraSetting& settings);
        virtual ~FlyCamera() = default;

        void                       OnUpdate(float dt);

        void                       OnMouseMove(float deltaX, float deltaY);
        void                       OnMouseScroll(float delta, float mouseX, float mouseY);
        void                       OnMouseButtonDown(int button);
        void                       OnMouseButtonUp(int button);
        void                       OnKeyDown(int key);
        void                       OnKeyUp(int key);
        // Call when the viewport loses OS focus to clear all held input state.
        void                       OnFocusLost();

        void                       FocusOn(Core::Maths::Vec3f center, float radius);
        void                       FocusOn(Core::Maths::Vec3f point);
        void                       FocusOn(Core::Maths::Vec3f aabbMin, Core::Maths::Vec3f aabbMax);

        void                       SetViewportSize(float width, float height);
        void                       SetPosition(Core::Maths::Vec3f position);
        void                       SetOrientation(float pitchDeg, float yawDeg);

        void                       EnterOrbitMode(Core::Maths::Vec3f pivot);
        void                       ExitOrbitMode();

        // View bookmarks — slots 0-8 map to keys 1-9
        void                       SaveBookmark(int slot);
        void                       RecallBookmark(int slot);

        virtual Core::Maths::Vec3f GetPosition() const override;
        virtual Core::Maths::Vec3f GetForward() const override;
        virtual Core::Maths::Vec3f GetUp() const override;
        virtual Core::Maths::Vec3f GetRight() const override;

        // World-space ray through the given screen pixel — use for picking and gizmo hit-testing.
        struct Ray
        {
            Core::Maths::Vec3f Origin;
            Core::Maths::Vec3f Direction;
        };
        Ray                            GetRayFromScreen(float mouseX, float mouseY) const;

        // Orientation quaternion rebuilt fresh from Pitch/Yaw every call — no accumulation.
        Core::Maths::Quaternion<float> GetOrientation() const;

    private:
        void               UpdateFreeCamera(float dt);
        void               UpdateOrbitCamera(float dt);
        void               UpdateAnimation(float dt);
        void               RecalculateView();
        void               RecalculateProjection();
        void               UpdateMatrices();

        Core::Maths::Vec3f GetKeyboardMoveDirection() const;
        float              ComputeAdaptiveSpeed() const;
        float              CollideCameraRay(Core::Maths::Vec3f origin, Core::Maths::Vec3f dir, float desiredDist) const;

        Core::Maths::Vec3f m_targetPosition  = {0.0f, 5.0f, 10.0f};

        // Euler angles in radians. GetOrientation() rebuilds the quaternion each frame.
        float              m_targetPitch     = 0.0f;
        float              m_targetYaw       = 0.0f;

        float              m_viewportWidth   = 1280.0f;
        float              m_viewportHeight  = 720.0f;

        // Orbit state
        Core::Maths::Vec3f m_orbitPivot      = {0.0f, 0.0f, 0.0f};
        float              m_orbitDistance   = 10.0f;
        float              m_targetOrbitDist = 10.0f;

        // Focus animation
        bool               m_animating       = false;
        Core::Maths::Vec3f m_animStartPos    = {};
        Core::Maths::Vec3f m_animEndPos      = {};
        float              m_animStartPitch  = 0.0f;
        float              m_animStartYaw    = 0.0f;
        float              m_animEndPitch    = 0.0f;
        float              m_animEndYaw      = 0.0f;
        float              m_animTimer       = 0.0f;
        float              m_animDuration    = 0.25f; // kept in sync with Settings.FocusDuration

        // Input state
        bool               m_rightMouseDown  = false;
        bool               m_middleMouseDown = false;
        bool               m_leftMouseDown   = false;
        bool               m_altDown         = false;
        bool               m_shiftDown       = false;
        bool               m_ctrlDown        = false;
        bool               m_keys[512]       = {};

        bool               m_projectionDirty = true;
        bool               m_viewDirty       = true;

        struct BookmarkSlot
        {
            bool               Valid    = false;
            Core::Maths::Vec3f Position = {};
            float              Pitch    = 0.0f;
            float              Yaw      = 0.0f;
        };
        BookmarkSlot m_bookmarks[9] = {};
    };
    ZDEFINE_PTR(FlyCamera);
} // namespace ZEngine::Rendering::Cameras
