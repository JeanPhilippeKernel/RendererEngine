#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/Windows/Inputs/KeyCode.h>
#include <cmath>

using namespace ZEngine::Core::Maths;

namespace ZEngine::Rendering::Cameras
{
    static constexpr float kPitchLimit = 1.5533430f; // radians(89.0f)
    static constexpr float kMaxDt      = 0.1f;       // cap: prevents teleport after pause/unminimize

    // Frame-rate-independent exponential smoothing: identical feel at any fps.
    static inline float    SmoothT(float factor, float dt)
    {
        return 1.0f - expf(-factor * dt);
    }

    // Wrap angle to [-PI, PI] so lerps always take the shortest arc.
    static inline float WrapAngle(float a)
    {
        while (a > PI<float>)
            a -= 2.0f * PI<float>;
        while (a < -PI<float>)
            a += 2.0f * PI<float>;
        return a;
    }

    // Shortest-arc lerp for angles stored in radians.
    static inline float LerpAngleRad(float a, float b, float t)
    {
        return a + WrapAngle(b - a) * t;
    }

    FlyCamera::FlyCamera(float aspectRatio, const CameraSetting& settings)
    {
        AspectRatio       = aspectRatio;
        Settings          = settings;
        Position          = {0.0f, 20.0f, 10.0f};
        Pitch             = radians(30.0f);
        m_targetPitch     = Pitch;
        m_targetPosition  = Position;
        m_animDuration    = Settings.FocusDuration;
        Type              = CameraType::PERSPECTIVE;

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

    Vec3f FlyCamera::GetForward() const
    {
        return rotate(GetOrientation(), Vec3f(0.0f, 0.0f, -1.0f));
    }

    Vec3f FlyCamera::GetRight() const
    {
        return rotate(GetOrientation(), Vec3f(1.0f, 0.0f, 0.0f));
    }

    Vec3f FlyCamera::GetUp() const
    {
        return rotate(GetOrientation(), Vec3f(0.0f, 1.0f, 0.0f));
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

        // Vulkan: Y flipped, depth range [0, 1].
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
        Yaw = m_targetYaw = WrapAngle(radians(yawDeg));
        m_viewDirty       = true;
        RecalculateView();
    }

    void FlyCamera::FocusOn(Vec3f center, float radius)
    {
        float fovRad      = radians(Settings.FOV);
        float distance    = (radius / tanf(fovRad * 0.5f)) * 1.5f;
        distance          = max(distance, Settings.MinOrbitDistance);

        Vec3f dir         = (Position - center).magnitude() > 0.001f ? (Position - center).normalize() : -GetForward();
        Vec3f endPos      = center + dir * distance;

        Vec3f lookDir     = -dir;
        float endPitch    = asinf(clamp(lookDir.y, -1.0f, 1.0f));
        // Derive yaw from the orientation convention: fromEulerAngles(-pitch, -yaw, 0).
        // Solving for yaw yields: yaw = atan2(-lookDir.x, lookDir.z).  No implicit sign hack.
        float endYaw      = WrapAngle(atan2f(-lookDir.x, lookDir.z));

        m_orbitPivot      = center;
        m_orbitDistance   = distance;
        m_targetOrbitDist = distance;
        // FocusOn does not change Mode — the caller remains in whatever mode they were in.

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

    void FlyCamera::FocusOn(Vec3f aabbMin, Vec3f aabbMax)
    {
        Vec3f center = (aabbMin + aabbMax) * 0.5f;
        Vec3f extent = (aabbMax - aabbMin) * 0.5f;
        float radius = extent.magnitude();
        FocusOn(center, max(radius, 0.01f));
    }

    void FlyCamera::OnMouseButtonDown(int button)
    {
        using KC = Windows::Inputs::GlfwKeyCode;

        if (button == (int) KC::MOUSE_BUTTON_LEFT)
        {
            m_leftMouseDown = true;
            // Alt + LMB = enter orbit pivoting at the scene point under the cursor.
            if (m_altDown && Mode == CameraMode::Free)
            {
                float pivotDist = m_orbitDistance;
                if (SceneRaycast)
                {
                    float hit = SceneRaycast(Position, GetForward(), m_orbitDistance * 2.0f);
                    if (hit < m_orbitDistance * 2.0f)
                        pivotDist = hit;
                }
                EnterOrbitMode(Position + GetForward() * pivotDist);
            }
        }
        if (button == (int) KC::MOUSE_BUTTON_RIGHT)
            m_rightMouseDown = true;
        if (button == (int) KC::MOUSE_BUTTON_MIDDLE)
            m_middleMouseDown = true;
    }

    void FlyCamera::OnMouseButtonUp(int button)
    {
        using KC = Windows::Inputs::GlfwKeyCode;
        if (button == (int) KC::MOUSE_BUTTON_LEFT)
            m_leftMouseDown = false;
        if (button == (int) KC::MOUSE_BUTTON_RIGHT)
        {
            m_rightMouseDown = false;
            if (!m_altDown && !m_leftMouseDown && Mode == CameraMode::Orbit)
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
        if (key == (int) KC::KEY_LEFT_ALT || key == (int) KC::KEY_RIGHT_ALT)
            m_altDown = true;
        if (key == (int) KC::KEY_LEFT_CONTROL)
            m_ctrlDown = true;
    }

    void FlyCamera::OnKeyUp(int key)
    {
        using KC = Windows::Inputs::GlfwKeyCode;
        if (key >= 0 && key < 512)
            m_keys[key] = false;

        if (key == (int) KC::KEY_LEFT_SHIFT)
            m_shiftDown = false;

        if (key == (int) KC::KEY_LEFT_ALT || key == (int) KC::KEY_RIGHT_ALT)
        {
            m_altDown = false;
            if (Mode == CameraMode::Orbit && !m_rightMouseDown && !m_leftMouseDown)
                ExitOrbitMode();
        }

        if (key == (int) KC::KEY_LEFT_CONTROL)
            m_ctrlDown = false;

        // F — frame selected object. Editor injects bounds via OnGetSelectionBounds.
        if (key == (int) KC::KEY_F)
        {
            if (OnGetSelectionBounds)
            {
                auto [center, radius] = OnGetSelectionBounds();
                FocusOn(center, radius);
            }
            else
            {
                FocusOn(Vec3f(0.0f, 0.0f, 0.0f), 5.0f);
            }
        }

        // Ctrl+1…9 = save bookmark; 1…9 alone = recall bookmark.
        if (key >= (int) KC::KEY_1 && key <= (int) KC::KEY_9)
        {
            int slot = key - (int) KC::KEY_1;
            if (m_ctrlDown)
                SaveBookmark(slot);
            else
                RecallBookmark(slot);
        }
    }

    void FlyCamera::OnFocusLost()
    {
        for (bool& k : m_keys)
            k = false;
        m_rightMouseDown  = false;
        m_middleMouseDown = false;
        m_leftMouseDown   = false;
        m_altDown         = false;
        m_shiftDown       = false;
        m_ctrlDown        = false;
    }

    void FlyCamera::OnMouseMove(float deltaX, float deltaY)
    {
        if (m_animating)
            return;

        float dx = deltaX / m_viewportWidth;
        float dy = deltaY / m_viewportHeight;

        // 1. Alt + LMB or Alt + RMB = Orbit tumble
        if (m_altDown && Mode == CameraMode::Orbit && (m_leftMouseDown || m_rightMouseDown))
        {
            float yawSign = GetUp().y < 0.0f ? -1.0f : 1.0f;
            m_targetYaw   = WrapAngle(m_targetYaw - yawSign * dx * PI<float> * Settings.OrbitSpeed);
            m_targetPitch = clamp(m_targetPitch - dy * PI<float> * Settings.OrbitSpeed, -kPitchLimit, kPitchLimit);
            return;
        }

        // 2. MMB = Screen-plane pan
        if (m_middleMouseDown)
        {
            float focalDist   = (Mode == CameraMode::Orbit) ? m_orbitDistance : 10.0f;
            float fovRad      = radians(Settings.FOV);
            float planeH      = 2.0f * tanf(fovRad * 0.5f) * focalDist;
            float planeW      = planeH * AspectRatio;

            // Read orthonormal basis directly from the view matrix rows — pole-safe.
            Vec3f right       = {View(0, 0), View(0, 1), View(0, 2)};
            Vec3f screenUp    = {-View(1, 0), -View(1, 1), -View(1, 2)};

            Vec3f pan         = (right * (-dx * planeW * Settings.PanSpeed)) + (screenUp * (dy * planeH * Settings.PanSpeed));

            m_targetPosition += pan;
            if (Mode == CameraMode::Orbit)
                m_orbitPivot += pan;
            m_viewDirty = true;
            return;
        }

        // 3. RMB = Free look
        if (m_rightMouseDown)
        {
            float yawSign = GetUp().y < 0.0f ? -1.0f : 1.0f;
            m_targetYaw   = WrapAngle(m_targetYaw - yawSign * dx * PI<float> * Settings.RotationSpeed);
            m_targetPitch = clamp(m_targetPitch - dy * PI<float> * Settings.RotationSpeed, -kPitchLimit, kPitchLimit);

            if (Mode == CameraMode::Orbit)
                ExitOrbitMode();
        }
    }

    void FlyCamera::OnMouseScroll(float delta, float mouseX, float mouseY)
    {
        if (Mode == CameraMode::Orbit)
        {
            float distance     = m_targetOrbitDist * 0.2f;
            distance           = std::max(distance, 0.001f);
            float speed        = std::min(distance * distance, 100.0f);

            m_targetOrbitDist -= delta * speed * Settings.ScrollSpeed;
            m_targetOrbitDist  = clamp(m_targetOrbitDist, Settings.MinOrbitDistance, Settings.MaxOrbitDistance);
        }
        else
        {
            // Zoom toward the 3D point under the cursor, not just along the view axis.
            float speed       = ComputeAdaptiveSpeed();
            Ray   ray         = GetRayFromScreen(mouseX, mouseY);
            m_targetPosition += ray.Direction * delta * Settings.ScrollSpeed * speed;
        }
    }

    void FlyCamera::OnUpdate(float dt)
    {
        if (dt <= 0.0f)
            return;

        dt = std::min(dt, kMaxDt);

        if (m_animating)
            UpdateAnimation(dt);
        else if (Mode == CameraMode::Orbit)
            UpdateOrbitCamera(dt);
        else
            UpdateFreeCamera(dt);

        UpdateMatrices();
    }

    void FlyCamera::UpdateFreeCamera(float dt)
    {
        float t = SmoothT(Settings.SmoothingFactor, dt);

        if (m_rightMouseDown)
        {
            float speed       = ComputeAdaptiveSpeed() * (m_shiftDown ? Settings.FastSpeedMultiplier : 1.0f);
            m_targetPosition += GetKeyboardMoveDirection() * speed * dt;
        }

        if ((m_targetPosition - Position).magnitude() > 0.00001f)
        {
            Position    = lerp(Position, m_targetPosition, t);
            m_viewDirty = true;
        }

        // Pitch is clamped so it never wraps — plain lerp is correct and cheaper here.
        float newPitch = clamp(lerp(Pitch, m_targetPitch, t), -kPitchLimit, kPitchLimit);
        float newYaw   = LerpAngleRad(Yaw, m_targetYaw, t);

        if (newPitch != Pitch || newYaw != Yaw)
        {
            Pitch       = newPitch;
            Yaw         = newYaw;
            m_viewDirty = true;
        }
    }

    void FlyCamera::UpdateOrbitCamera(float dt)
    {
        float t          = SmoothT(Settings.SmoothingFactor, dt);

        Pitch            = clamp(lerp(Pitch, m_targetPitch, t), -kPitchLimit, kPitchLimit);
        Yaw              = LerpAngleRad(Yaw, m_targetYaw, t);
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
        float t      = smoothstep(clamp(m_animTimer / m_animDuration, 0.0f, 1.0f));

        Position     = lerp(m_animStartPos, m_animEndPos, t);
        Pitch        = clamp(lerp(m_animStartPitch, m_animEndPitch, t), -kPitchLimit, kPitchLimit);
        Yaw          = LerpAngleRad(m_animStartYaw, m_animEndYaw, t);

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
        Mode              = CameraMode::Orbit;
    }

    void FlyCamera::ExitOrbitMode()
    {
        m_targetPosition = Position;
        m_targetPitch    = Pitch;
        m_targetYaw      = Yaw;
        Mode             = CameraMode::Free;
    }

    void FlyCamera::SaveBookmark(int slot)
    {
        if (slot < 0 || slot >= 9)
            return;
        m_bookmarks[slot] = {true, Position, Pitch, Yaw};
    }

    void FlyCamera::RecallBookmark(int slot)
    {
        if (slot < 0 || slot >= 9 || !m_bookmarks[slot].Valid)
            return;
        const auto& bm   = m_bookmarks[slot];

        m_animStartPos   = Position;
        m_animEndPos     = bm.Position;
        m_animStartPitch = Pitch;
        m_animStartYaw   = Yaw;
        m_animEndPitch   = bm.Pitch;
        m_animEndYaw     = bm.Yaw;
        m_animTimer      = 0.0f;
        m_animDuration   = Settings.FocusDuration;
        m_animating      = true;
    }

    Vec3f FlyCamera::GetKeyboardMoveDirection() const
    {
        using KC      = Windows::Inputs::GlfwKeyCode;

        Vec3f forward = GetForward();
        Vec3f right   = GetRight();
        Vec3f up      = Vec3f(Camera::WorldUp.x, Camera::WorldUp.y, Camera::WorldUp.z);

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
        return mag > 0.0001f ? dir / mag : dir;
    }

    float FlyCamera::ComputeAdaptiveSpeed() const
    {
        if (SceneRaycast)
        {
            float hitFwd  = SceneRaycast(Position, GetForward(), Settings.MaxMoveSpeed * 10.0f);
            float hitDown = SceneRaycast(Position, {0.0f, -1.0f, 0.0f}, Settings.MaxMoveSpeed * 10.0f);
            return clamp(std::min(hitFwd, hitDown) * 0.5f, Settings.MinMoveSpeed, Settings.MaxMoveSpeed);
        }
        float h = fabsf(Position.y);
        return clamp(h * 0.5f, Settings.MinMoveSpeed, Settings.MaxMoveSpeed);
    }

    float FlyCamera::CollideCameraRay(Vec3f origin, Vec3f dir, float desiredDist) const
    {
        if (SceneRaycast)
        {
            float hit = SceneRaycast(origin, dir, desiredDist);
            if (hit < desiredDist)
                return hit * 0.9f;
        }
        return desiredDist;
    }

    FlyCamera::Ray FlyCamera::GetRayFromScreen(float mouseX, float mouseY) const
    {
        // NDC in [-1,1], Vulkan Y-up in NDC (screen Y=0 is top).
        float ndcX     = (mouseX / m_viewportWidth) * 2.0f - 1.0f;
        float ndcY     = 1.0f - (mouseY / m_viewportHeight) * 2.0f;

        // Unproject to view space: inv(P) * NDC.
        float fovRad   = radians(Settings.FOV);
        float tanHalf  = tanf(fovRad * 0.5f);
        float viewDirX = ndcX * AspectRatio * tanHalf;
        float viewDirY = ndcY * tanHalf;

        // Rotate view-space direction to world space via the camera basis.
        Vec3f r        = GetRight();
        Vec3f u        = GetUp();
        Vec3f f        = GetForward();
        Vec3f dir      = r * viewDirX + u * viewDirY + f; // f corresponds to view-space (0,0,-1) → forward

        float mag      = dir.magnitude();
        return {Position, mag > 0.0001f ? dir / mag : f};
    }

} // namespace ZEngine::Rendering::Cameras
