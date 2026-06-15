#pragma once
#include <Core/Maths/MathUtils.h>
#include <Core/Maths/Matrix.h>
#include <Core/Maths/Quaternion.h>
#include <Rendering/Cameras/Camera.h>

namespace ZEngine::Rendering::Cameras
{

    class FlyCamera : public Camera
    {
    public:
        FlyCamera() = default;
        FlyCamera(float aspectRatio, CameraSetting settings = {});

        CameraSetting                       Settings = {};

        void                                OnUpdate(float dt);

        void                                OnMouseMove(float deltaX, float deltaY);
        void                                OnMouseScroll(float delta, float mouseX, float mouseY);
        void                                OnMouseButtonDown(int button);
        void                                OnMouseButtonUp(int button);
        void                                OnKeyDown(int key);
        void                                OnKeyUp(int key);

        void                                FocusOn(Core::Maths::Vec3f center, float radius);
        void                                FocusOn(Core::Maths::Vec3f point);

        void                                SetViewportSize(float width, float height);

        void                                SetPosition(Core::Maths::Vec3f position);
        void                                SetOrientation(float pitchDeg, float yawDeg);

        virtual Core::Maths::Vec3f          GetPosition() const override;
        virtual ZEngine::Core::Maths::Vec3f GetForward() override;
        virtual ZEngine::Core::Maths::Vec3f GetUp() override;
        virtual ZEngine::Core::Maths::Vec3f GetRight() override;
        CameraMode                          GetMode() const
        {
            return m_mode;
        }

        void                           EnterOrbitMode(Core::Maths::Vec3f pivot);
        void                           ExitOrbitMode();

        // Recomputed fresh every frame from m_pitch / m_yaw —
        // identical pattern to PerspectiveCamera::GetOrientation().
        // No quaternion accumulation => zero roll, zero drift, zero flip.
        Core::Maths::Quaternion<float> GetOrientation() const;

        void                           UpdateFreeCamera(float dt);
        void                           UpdateOrbitCamera(float dt);
        void                           UpdateAnimation(float dt);
        void                           RecalculateView();
        void                           RecalculateProjection();
        void                           UpdateMatrices();

        Core::Maths::Vec3f             GetKeyboardMoveDirection();
        float                          ComputeAdaptiveSpeed() const;
        Core::Maths::Vec3f             Unproject(float mouseX, float mouseY, float depth);
        float                          CollideCameraRay(Core::Maths::Vec3f origin, Core::Maths::Vec3f dir, float desiredDist) const;

        // World-space position
        Core::Maths::Vec3f             m_targetPosition  = {0.0f, 5.0f, 10.0f};

        // Orientation stored as plain Euler angles (radians).
        // GetOrientation() rebuilds the quaternion each frame — no accumulation.
        float                          m_targetPitch     = 0.0f;
        float                          m_targetYaw       = 0.0f;

        float                          m_viewportWidth   = 1280.0f;
        float                          m_viewportHeight  = 720.0f;

        // Orbit
        CameraMode                     m_mode            = CameraMode::Free;
        Core::Maths::Vec3f             m_orbitPivot      = {0.0f, 0.0f, 0.0f};
        float                          m_orbitDistance   = 10.0f;
        float                          m_targetOrbitDist = 10.0f;

        // Smooth focus animation
        bool                           m_animating       = false;
        Core::Maths::Vec3f             m_animStartPos    = {};
        Core::Maths::Vec3f             m_animEndPos      = {};
        float                          m_animStartPitch  = 0.0f;
        float                          m_animStartYaw    = 0.0f;
        float                          m_animEndPitch    = 0.0f;
        float                          m_animEndYaw      = 0.0f;
        float                          m_animTimer       = 0.0f;
        float                          m_animDuration    = 0.20f;

        // Input state
        bool                           m_rightMouseDown  = false;
        bool                           m_middleMouseDown = false;
        bool                           m_altDown         = false;
        bool                           m_shiftDown       = false;
        bool                           m_keys[512]       = {};

        bool                           m_projectionDirty = true;
        bool                           m_viewDirty       = true;
    };
    ZDEFINE_PTR(FlyCamera);
} // namespace ZEngine::Rendering::Cameras