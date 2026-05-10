#include <Core/Maths/MathUtils.h>
#include <Rendering/Cameras/FlyCamera.h>
#include <Windows/Inputs/KeyCode.h>
#include <cmath>

using namespace ZEngine::Core::Maths;

namespace ZEngine::Rendering::Cameras
{
    static constexpr float kPitchLimit = radians(89.0f);

    FlyCamera::FlyCamera(float aspectRatio, CameraSetting settings)
    {
        AspectRatio       = aspectRatio;
        Settings          = settings;
        Position          = {0.0f, 20.0f, 10.0f};
        Pitch             = radians(30.0f);

        m_targetPosition  = Position;

        m_projectionDirty = true;
        m_viewDirty       = true;

        UpdateMatrices();
    }

    Quaternion<float> FlyCamera::GetOrientation() const
    {
        return fromEulerAngles(-Pitch, -Yaw, 0.0f);
    }

    Vec3f FlyCamera::GetPosition() const
    {
        return Position;
    }

    Vec3f FlyCamera::GetForward()
    {
        return rotate(GetOrientation(), Vec3f(0.0f, 0.0f, -1.0f));
    }

    Vec3f FlyCamera::GetRight()
    {
        return rotate(GetOrientation(), Vec3f(1.0f, 0.0f, 0.0f));
    }

    Vec3f FlyCamera::GetUp()
    {
        return rotate(GetOrientation(), WorldUp);
    }

    void FlyCamera::UpdateMatrices()
    {
        if (m_projectionDirty)
            RecalculateProjection();
        if (m_viewDirty)
            RecalculateView();
    }

    void FlyCamera::RecalculateView()
    {
        Vec3f f     = GetForward();
        Vec3f r     = GetRight();
        Vec3f u     = GetUp();

        View        = Mat4f(r.x, r.y, r.z, -dot(r, Position), u.x, u.y, u.z, -dot(u, Position), -f.x, -f.y, -f.z, dot(f, Position), 0.0f, 0.0f, 0.0f, 1.0f);
        m_viewDirty = false;
    }

    void FlyCamera::RecalculateProjection()
    {
        float fovRad      = radians(Settings.FOV);
        float tanHalf     = tanf(fovRad * 0.5f);
        float n           = Settings.NearPlane;
        float f           = Settings.FarPlane;
        float a           = AspectRatio;

        // Vulkan: Y flipped, Depth [0, 1]
        Projection        = Mat4f(1.0f / (a * tanHalf), 0.0f, 0.0f, 0.0f, 0.0f, -1.0f / tanHalf, 0.0f, 0.0f, 0.0f, 0.0f, f / (n - f), (n * f) / (n - f), 0.0f, 0.0f, -1.0f, 0.0f);
        m_projectionDirty = false;
    }

    void FlyCamera::SetViewportSize(float width, float height)
    {
        m_viewportWidth  = width;
        m_viewportHeight = height;
        AspectRatio      = width / height;
        RecalculateProjection();
    }

    void FlyCamera::SetPosition(Vec3f position)
    {
        Position = m_targetPosition = position;
        RecalculateView();
    }

    void FlyCamera::SetOrientation(float pitchDeg, float yawDeg)
    {
        Pitch = m_targetPitch = clamp(radians(pitchDeg), -kPitchLimit, kPitchLimit);
        Yaw = m_targetYaw = radians(yawDeg);
        m_viewDirty       = true;
        RecalculateView();
    }

    void FlyCamera::FocusOn(Vec3f center, float radius)
    {
        float fovRad      = radians(Settings.FOV);
        float distance    = (radius / tanf(fovRad * 0.5f)) * 1.5f;
        distance          = max(distance, Settings.MinOrbitDistance);

        // Keep the current look direction — move straight back along it
        Vec3f dir         = (Position - center).magnitude() > 0.001f ? (Position - center).normalize() : -GetForward();
        Vec3f endPos      = center + dir * distance;

        // Compute the yaw/pitch that looks FROM endPos TOWARD center
        Vec3f lookDir     = -dir;
        float endPitch    = asinf(clamp(lookDir.y, -1.0f, 1.0f));
        float endYaw      = -atan2f(lookDir.x, -lookDir.z);

        // Enter orbit around the focused point
        m_orbitPivot      = center;
        m_orbitDistance   = distance;
        m_targetOrbitDist = distance;
        m_mode            = CameraMode::Orbit;

        // Animate smoothly
        m_animStartPos    = Position;
        m_animEndPos      = endPos;
        m_animStartPitch  = Pitch;
        m_animStartYaw    = Yaw;
        m_animEndPitch    = endPitch;
        m_animEndYaw      = endYaw;
        m_animTimer       = 0.0f;
        m_animDuration    = Settings.FocusDuration;
        m_animating       = true;
    }

    void FlyCamera::FocusOn(Vec3f point)
    {
        float dist   = (Position - point).magnitude();
        float radius = clamp(dist * 0.3f, 0.5f, 500.0f);
        FocusOn(point, radius);
    }

    void FlyCamera::OnMouseButtonDown(int button)
    {
        using KC = Windows::Inputs::GlfwKeyCode;
        if (button == (int) KC::MOUSE_BUTTON_RIGHT)
            m_rightMouseDown = true;
        if (button == (int) KC::MOUSE_BUTTON_MIDDLE)
            m_middleMouseDown = true;
    }

    void FlyCamera::OnMouseButtonUp(int button)
    {
        using KC = Windows::Inputs::GlfwKeyCode;
        if (button == (int) KC::MOUSE_BUTTON_RIGHT)
        {
            m_rightMouseDown = false;
            if (!m_altDown && m_mode == CameraMode::Orbit)
                ExitOrbitMode();
        }
        if (button == (int) KC::MOUSE_BUTTON_MIDDLE)
            m_middleMouseDown = false;
    }

    void FlyCamera::OnKeyDown(int key)
    {
        using KC = Windows::Inputs::GlfwKeyCode;
        if (key >= 0 && key < 512)
            m_keys[key] = true;
        if (key == (int) KC::KEY_LEFT_SHIFT)
            m_shiftDown = true;
        if (key == (int) KC::KEY_LEFT_ALT)
            m_altDown = true;
    }

    void FlyCamera::OnKeyUp(int key)
    {
        using KC = Windows::Inputs::GlfwKeyCode;
        if (key >= 0 && key < 512)
            m_keys[key] = false;

        if (key == (int) KC::KEY_LEFT_SHIFT)
            m_shiftDown = false;

        if (key == (int) KC::KEY_LEFT_ALT)
        {
            m_altDown = false;
            if (m_mode == CameraMode::Orbit && !m_rightMouseDown)
                ExitOrbitMode();
        }

        // F — frame to world origin (override with selected-object bounds externally)
        if (key == (int) KC::KEY_F)
            FocusOn(Vec3f(0.0f, 0.0f, 0.0f), 5.0f);
    }

    void FlyCamera::OnMouseMove(float deltaX, float deltaY)
    {
        if (m_animating)
            return;

        float dx = deltaX / m_viewportWidth;
        float dy = deltaY / m_viewportHeight;

        // 1. ALT + RMB = Orbit/Tumble
        if (m_altDown && m_mode == CameraMode::Orbit)
        {
            float yawSign  = GetUp().y < 0.0f ? -1.0f : 1.0f;
            m_targetYaw   -= yawSign * dx * PI<float> * Settings.OrbitSpeed;
            m_targetPitch  = clamp(m_targetPitch - dy * PI<float> * Settings.OrbitSpeed, -kPitchLimit, kPitchLimit);
            return;
        }

        // 2. MMB = Screen-Plane Pan (Refined)
        if (m_middleMouseDown)
        {
            float focalDist   = (m_mode == CameraMode::Orbit) ? m_orbitDistance : 10.0f;
            float fovRad      = radians(Settings.FOV);

            // Calculate plane dimensions at focal distance
            float planeH      = 2.0f * tanf(fovRad * 0.5f) * focalDist;
            float planeW      = planeH * AspectRatio;

            // Use the Screen-space basis for panning to avoid "drift" at poles
            Vec3f right       = GetRight();
            Vec3f screenUp    = cross3d(right, GetForward()).normalize();

            Vec3f pan         = (right * (-dx * planeW * Settings.PanSpeed)) + (screenUp * (dy * planeH * Settings.PanSpeed));

            m_targetPosition += pan;
            if (m_mode == CameraMode::Orbit)
                m_orbitPivot += pan;
            m_viewDirty = true;
            return;
        }

        // 3. RMB = Free Look
        if (m_rightMouseDown)
        {
            float yawSign  = GetUp().y < 0.0f ? -1.0f : 1.0f;
            m_targetYaw   -= yawSign * dx * PI<float> * Settings.RotationSpeed;
            m_targetPitch  = clamp(m_targetPitch - dy * PI<float> * Settings.RotationSpeed, -kPitchLimit, kPitchLimit);

            if (m_mode == CameraMode::Orbit)
                ExitOrbitMode();
        }
    }

    void FlyCamera::OnMouseScroll(float delta, float mouseX, float mouseY)
    {
        if (m_mode == CameraMode::Orbit)
        {
            // Zoom toward pivot — same quadratic speed as PerspectiveCamera::Zoom
            float distance     = m_targetOrbitDist * 0.2f;
            distance           = std::max(distance, 0.001f);
            float speed        = std::min(distance * distance, 100.0f);

            m_targetOrbitDist -= delta * speed * Settings.ScrollSpeed;
            m_targetOrbitDist  = clamp(m_targetOrbitDist, Settings.MinOrbitDistance, Settings.MaxOrbitDistance);
        }
        else
        {
            float speed       = ComputeAdaptiveSpeed();
            m_targetPosition += GetForward() * delta * Settings.ScrollSpeed * speed;
        }
    }

    void FlyCamera::OnUpdate(float dt)
    {
        if (dt <= 0.0f)
            return;

        if (m_animating)
            UpdateAnimation(dt);
        else if (m_mode == CameraMode::Orbit)
            UpdateOrbitCamera(dt);
        else
            UpdateFreeCamera(dt);

        UpdateMatrices();
    }

    void FlyCamera::UpdateFreeCamera(float dt)
    {
        float t = clamp(Settings.SmoothingFactor * dt, 0.0f, 1.0f);

        if (m_rightMouseDown)
        {
            float speed       = ComputeAdaptiveSpeed() * (m_shiftDown ? Settings.FastSpeedMultiplier : 1.0f);
            m_targetPosition += GetKeyboardMoveDirection() * speed * dt;
        }

        // Position Smoothing
        if ((m_targetPosition - Position).magnitude() > 0.00001f)
        {
            Position    = lerp(Position, m_targetPosition, t);
            m_viewDirty = true;
        }

        // Rotation Smoothing
        m_targetPitch  = clamp(m_targetPitch, -kPitchLimit, kPitchLimit);
        float newPitch = clamp(lerpAngle(Pitch, m_targetPitch, t), -kPitchLimit, kPitchLimit);
        float newYaw   = lerpAngle(Yaw, m_targetYaw, t);

        if (newPitch != Pitch || newYaw != Yaw)
        {
            Pitch       = newPitch;
            Yaw         = newYaw;
            m_viewDirty = true;
        }
    }

    void FlyCamera::UpdateOrbitCamera(float dt)
    {
        float t          = clamp(Settings.SmoothingFactor * dt, 0.0f, 1.0f);

        m_targetPitch    = clamp(m_targetPitch, -kPitchLimit, kPitchLimit);
        Pitch            = clamp(lerp(Pitch, m_targetPitch, t), -kPitchLimit, kPitchLimit);
        Yaw              = lerpAngle(Yaw, m_targetYaw, t); // was lerp — now shortest path
        m_orbitDistance  = lerp(m_orbitDistance, m_targetOrbitDist, t);

        Vec3f fwd        = GetForward();
        float safeDist   = CollideCameraRay(m_orbitPivot, -fwd, m_orbitDistance);
        Position         = m_orbitPivot - fwd * safeDist;
        m_targetPosition = Position;
        m_viewDirty      = true;
    }

    void FlyCamera::UpdateAnimation(float dt)
    {
        m_animTimer += dt;

        // Clamp t to [0,1] — on a slow first frame dt can exceed duration
        float t      = smoothstep(clamp(m_animTimer / m_animDuration, 0.0f, 1.0f));

        Position     = lerp(m_animStartPos, m_animEndPos, t);
        Pitch        = clamp(lerpAngle(m_animStartPitch, m_animEndPitch, t), -kPitchLimit, kPitchLimit);
        Yaw          = lerpAngle(m_animStartYaw, m_animEndYaw, t);

        m_viewDirty  = true;

        if (m_animTimer >= m_animDuration)
        {
            Position          = m_animEndPos;
            Pitch             = clamp(m_animEndPitch, -kPitchLimit, kPitchLimit);
            Yaw               = m_animEndYaw;
            m_animating       = false;
            m_targetPosition  = Position;
            m_targetPitch     = Pitch;
            m_targetYaw       = Yaw;
            m_targetOrbitDist = m_orbitDistance;
        }
    }

    void FlyCamera::EnterOrbitMode(Vec3f pivot)
    {
        m_orbitPivot      = pivot;
        m_orbitDistance   = clamp((Position - pivot).magnitude(), Settings.MinOrbitDistance, Settings.MaxOrbitDistance);
        m_targetOrbitDist = m_orbitDistance;
        m_mode            = CameraMode::Orbit;
    }

    void FlyCamera::ExitOrbitMode()
    {
        m_targetPosition = Position;
        m_targetPitch    = Pitch;
        m_targetYaw      = Yaw;
        m_mode           = CameraMode::Free;
    }

    Vec3f FlyCamera::GetKeyboardMoveDirection()
    {
        using KC      = Windows::Inputs::GlfwKeyCode;

        Vec3f forward = GetForward();
        Vec3f right   = GetRight();
        Vec3f up      = WorldUp; // world-up so Q/E always move vertically

        Vec3f dir     = {};
        if (m_keys[(int) KC::KEY_W])
            dir += forward;
        if (m_keys[(int) KC::KEY_S])
            dir -= forward;
        if (m_keys[(int) KC::KEY_D])
            dir += right;
        if (m_keys[(int) KC::KEY_A])
            dir -= right;
        if (m_keys[(int) KC::KEY_E])
            dir += up;
        if (m_keys[(int) KC::KEY_Q])
            dir -= up;

        float mag = dir.magnitude();
        if (mag > 0.0001f)
            dir = dir / mag;

        return dir;
    }

    float FlyCamera::ComputeAdaptiveSpeed() const
    {
        // Stub — replace with raycast against your scene
        // float hitFwd  = Scene::Raycast(Position,  GetForward(),  FLT_MAX).distance;
        // float hitDown = Scene::Raycast(Position, {0,-1,0},       FLT_MAX).distance;
        // return clamp(min(hitFwd, hitDown) * 0.5f, Settings.MinMoveSpeed, Settings.MaxMoveSpeed);

        // Fallback: scale by height above ground plane
        float h = fabsf(Position.y);
        return clamp(h * 0.5f, Settings.MinMoveSpeed, Settings.MaxMoveSpeed);
    }

    Vec3f FlyCamera::Unproject(float mouseX, float mouseY, float depth)
    {
        float ndcX    = (mouseX / m_viewportWidth) * 2.0f - 1.0f;
        float ndcY    = -((mouseY / m_viewportHeight) * 2.0f - 1.0f); // Y flip for Vulkan

        float fovRad  = radians(Settings.FOV);
        float tanHalf = tanf(fovRad * 0.5f);
        float viewX   = ndcX * AspectRatio * tanHalf * depth;
        float viewY   = ndcY * tanHalf * depth;

        return Position + GetForward() * depth + GetRight() * viewX + GetUp() * viewY;
    }

    float FlyCamera::CollideCameraRay(Vec3f /*origin*/, Vec3f /*dir*/, float desiredDist) const
    {
        // Plug your scene raycast here:
        //   RayHit hit = Scene::Raycast(origin, dir, desiredDist);
        //   if (hit) return hit.distance * 0.9f;
        return desiredDist;
    }

} // namespace ZEngine::Rendering::Cameras