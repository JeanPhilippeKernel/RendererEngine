#include <GLFW/glfw3.h>
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <cmath>

using namespace ZEngine::Core::Maths;

namespace ZEngine::Rendering::Cameras
{
    static constexpr float kPitchLimit = 1.5533430f; // radians(89)
    static constexpr float kMaxDt      = 0.1f;

    static inline float    SmoothT(float factor, float dt)
    {
        return 1.0f - expf(-factor * dt);
    }

    static inline float WrapAngle(float a)
    {
        while (a > PI<float>)
            a -= 2.0f * PI<float>;
        while (a < -PI<float>)
            a += 2.0f * PI<float>;
        return a;
    }

    static inline float LerpAngleRad(float a, float b, float t)
    {
        return a + WrapAngle(b - a) * t;
    }

    FlyCamera::FlyCamera(float aspectRatio, const CameraSetting& settings)
    {
        AspectRatio    = aspectRatio;
        Settings       = settings;
        Position       = {0.0f, 5.0f, 8.0f};
        Pitch          = radians(30.0f);
        m_targetPitch  = Pitch;
        m_targetPos    = Position;
        m_animDuration = Settings.FocusDuration;
        Type           = CameraType::PERSPECTIVE;
        m_projDirty    = true;
        m_viewDirty    = true;
        UpdateMatrices();
    }

    // ---------------------------------------------------------------------------
    // Public accessors
    // ---------------------------------------------------------------------------

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

    // ---------------------------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------------------------

    void FlyCamera::SetViewportSize(float logicalW, float logicalH)
    {
        m_logicalW  = logicalW;
        m_logicalH  = logicalH;
        AspectRatio = logicalW / logicalH;
        m_projDirty = true;
    }

    void FlyCamera::SetPosition(Vec3f position)
    {
        Position = m_targetPos = position;
        m_viewDirty            = true;
    }

    void FlyCamera::SetOrientation(float pitchDeg, float yawDeg)
    {
        Pitch = m_targetPitch = clamp(radians(pitchDeg), -kPitchLimit, kPitchLimit);
        Yaw = m_targetYaw = WrapAngle(radians(yawDeg));
        m_viewDirty       = true;
    }

    // ---------------------------------------------------------------------------
    // OnUpdate — main entry point called once per frame by the controller
    // ---------------------------------------------------------------------------

    void FlyCamera::OnUpdate(float dt)
    {
        if (dt <= 0.0f)
            return;
        dt = std::min(dt, kMaxDt);

        // Pan transitions — checked every frame regardless of current state.
        if (Input.MiddleDown && State != FlyCameraState::Pan && State != FlyCameraState::Animating)
        {
            m_stateBeforePan = State;
            State            = FlyCameraState::Pan;
        }
        if (!Input.MiddleDown && State == FlyCameraState::Pan)
        {
            State = m_stateBeforePan;
        }

        // Orbit entry (Alt+LMB, only from Free).
        if (Input.AltDown && Input.LeftDown && State == FlyCameraState::Free)
        {
            float pivotDist = m_orbitDist;
            if (Hooks.Raycast)
            {
                float hit = Hooks.Raycast(Position, GetForward(), m_orbitDist * 2.0f);
                if (hit < m_orbitDist * 2.0f)
                    pivotDist = hit;
            }
            m_orbitPivot      = Position + GetForward() * pivotDist;
            m_orbitDist       = clamp((Position - m_orbitPivot).magnitude(), Settings.MinOrbitDistance, Settings.MaxOrbitDistance);
            m_targetOrbitDist = m_orbitDist;
            State             = FlyCameraState::Orbit;
        }

        // Orbit exit (Alt released, or no buttons held).
        if (!Input.AltDown && State == FlyCameraState::Orbit && !Input.LeftDown)
        {
            m_targetPos   = Position;
            m_targetPitch = Pitch;
            m_targetYaw   = Yaw;
            State         = FlyCameraState::Free;
        }

        switch (State)
        {
            case FlyCameraState::Animating:
                UpdateAnimation(dt);
                break;
            case FlyCameraState::Pan:
                UpdatePan(dt);
                break;
            case FlyCameraState::Orbit:
                UpdateOrbit(dt);
                break;
            case FlyCameraState::Free:
                UpdateFree(dt);
                break;
        }

        if (Input.Keys[GLFW_KEY_F])
        {
            Vec3f center = {0.0f, 0.0f, 0.0f};
            float radius = 5.0f;
            if (Hooks.GetSelectionBounds)
                std::tie(center, radius) = Hooks.GetSelectionBounds();
            FocusOn(center, radius);
            Input.Keys[GLFW_KEY_F] = false;
        }

        for (int i = 0; i < 9; ++i)
        {
            int key = GLFW_KEY_1 + i;
            if (Input.Keys[key])
            {
                if (Input.CtrlDown)
                    SaveBookmark(i);
                else
                    RecallBookmark(i);
                Input.Keys[key] = false;
            }
        }

        if (m_projDirty)
            RecalculateProjection();
        if (m_viewDirty)
            RecalculateView();

        Input.FlushDeltas();
    }

    // ---------------------------------------------------------------------------
    // Private update methods
    // ---------------------------------------------------------------------------

    void FlyCamera::UpdateFree(float dt)
    {
        float t = SmoothT(Settings.SmoothingFactor, dt);

        if (Input.RightDown)
        {
            float speed    = AdaptiveSpeed() * (Input.ShiftDown ? Settings.FastSpeedMultiplier : 1.0f);
            m_targetPos   += KeyboardMoveDir() * speed * dt;

            float yawSign  = GetUp().y < 0.0f ? -1.0f : 1.0f;
            m_targetYaw    = WrapAngle(m_targetYaw - yawSign * (Input.MouseDeltaX / m_logicalW) * PI<float> * Settings.RotationSpeed);
            m_targetPitch  = clamp(m_targetPitch - (Input.MouseDeltaY / m_logicalH) * PI<float> * Settings.RotationSpeed, -kPitchLimit, kPitchLimit);
        }

        if (Input.ScrollDelta != 0.0f)
        {
            Ray   ray    = GetRayFromViewport(Input.MouseViewportX, Input.MouseViewportY);
            float speed  = std::min(AdaptiveSpeed(), 3.0f);
            m_targetPos += ray.Direction * Input.ScrollDelta * Settings.ScrollSpeed * speed;
        }

        if ((m_targetPos - Position).magnitude() > 0.00001f)
        {
            Position    = lerp(Position, m_targetPos, t);
            m_viewDirty = true;
        }

        float newPitch = clamp(lerp(Pitch, m_targetPitch, t), -kPitchLimit, kPitchLimit);
        float newYaw   = LerpAngleRad(Yaw, m_targetYaw, t);
        if (newPitch != Pitch || newYaw != Yaw)
        {
            Pitch       = newPitch;
            Yaw         = newYaw;
            m_viewDirty = true;
        }
    }

    void FlyCamera::UpdateOrbit(float dt)
    {
        float t = SmoothT(Settings.SmoothingFactor, dt);

        if (Input.AltDown && (Input.LeftDown || Input.RightDown))
        {
            float yawSign = GetUp().y < 0.0f ? -1.0f : 1.0f;
            m_targetYaw   = WrapAngle(m_targetYaw - yawSign * (Input.MouseDeltaX / m_logicalW) * PI<float> * Settings.OrbitSpeed);
            m_targetPitch = clamp(m_targetPitch - (Input.MouseDeltaY / m_logicalH) * PI<float> * Settings.OrbitSpeed, -kPitchLimit, kPitchLimit);
        }

        if (Input.ScrollDelta != 0.0f)
        {
            float dist         = std::max(m_targetOrbitDist * 0.2f, 0.001f);
            float speed        = std::min(dist * dist, 100.0f);
            m_targetOrbitDist -= Input.ScrollDelta * speed * Settings.ScrollSpeed;
            m_targetOrbitDist  = clamp(m_targetOrbitDist, Settings.MinOrbitDistance, Settings.MaxOrbitDistance);
        }

        Pitch          = clamp(lerp(Pitch, m_targetPitch, t), -kPitchLimit, kPitchLimit);
        Yaw            = LerpAngleRad(Yaw, m_targetYaw, t);
        m_orbitDist    = lerp(m_orbitDist, m_targetOrbitDist, t);

        Vec3f fwd      = GetForward();
        float safeDist = OrbitCollide(m_orbitDist);
        Position       = m_orbitPivot - fwd * safeDist;
        m_targetPos    = Position;
        m_viewDirty    = true;
    }

    void FlyCamera::UpdatePan(float dt)
    {
        float focalDist  = (m_stateBeforePan == FlyCameraState::Orbit) ? m_orbitDist : 10.0f;
        float fovRad     = radians(Settings.FOV);
        float planeH     = 2.0f * tanf(fovRad * 0.5f) * focalDist;
        float planeW     = planeH * AspectRatio;

        Vec3f right      = {View(0, 0), View(0, 1), View(0, 2)};
        Vec3f screenUp   = {-View(1, 0), -View(1, 1), -View(1, 2)};

        Vec3f pan        = (right * (-(Input.MouseDeltaX / m_logicalW) * planeW * Settings.PanSpeed)) + (screenUp * ((Input.MouseDeltaY / m_logicalH) * planeH * Settings.PanSpeed));

        m_targetPos     += pan;
        m_orbitPivot    += pan;
        m_viewDirty      = true;

        // Apply position immediately — pan should feel direct.
        Position         = m_targetPos;
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
            m_targetPos       = Position;
            m_targetPitch     = Pitch;
            m_targetYaw       = Yaw;
            m_targetOrbitDist = m_orbitDist;
            State             = m_stateBeforeAnim;
        }
    }

    // ---------------------------------------------------------------------------
    // Focus / bookmarks
    // ---------------------------------------------------------------------------

    void FlyCamera::FocusOn(Vec3f center, float radius)
    {
        float fovRad      = radians(Settings.FOV);
        float distance    = (radius / tanf(fovRad * 0.5f)) * 1.5f;
        distance          = max(distance, Settings.MinOrbitDistance);

        Vec3f dir         = (Position - center).magnitude() > 0.001f ? (Position - center).normalize() : -GetForward();
        Vec3f endPos      = center + dir * distance;

        Vec3f lookDir     = -dir;
        float endPitch    = -asinf(clamp(lookDir.y, -1.0f, 1.0f));
        float endYaw      = WrapAngle(atan2f(lookDir.x, -lookDir.z));

        m_orbitPivot      = center;
        m_orbitDist       = distance;
        m_targetOrbitDist = distance;

        m_animStartPos    = Position;
        m_animEndPos      = endPos;
        m_animStartPitch  = Pitch;
        m_animStartYaw    = Yaw;
        m_animEndPitch    = endPitch;
        m_animEndYaw      = endYaw;
        m_animTimer       = 0.0f;
        m_animDuration    = Settings.FocusDuration;
        m_stateBeforeAnim = FlyCameraState::Free;
        State             = FlyCameraState::Animating;
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
        FocusOn(center, max(extent.magnitude(), 0.01f));
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
        const auto& bm    = m_bookmarks[slot];
        m_animStartPos    = Position;
        m_animEndPos      = bm.Pos;
        m_animStartPitch  = Pitch;
        m_animStartYaw    = Yaw;
        m_animEndPitch    = bm.Pitch;
        m_animEndYaw      = bm.Yaw;
        m_animTimer       = 0.0f;
        m_animDuration    = Settings.FocusDuration;
        m_stateBeforeAnim = State;
        State             = FlyCameraState::Animating;
    }

    // ---------------------------------------------------------------------------
    // Ray unprojection
    // ---------------------------------------------------------------------------

    FlyCamera::Ray FlyCamera::GetRayFromViewport(float viewportX, float viewportY) const
    {
        // NDC in [-1,1]; viewport coords are logical-pixel-relative, Y=0 at top.
        float ndcX    = (viewportX / m_logicalW) * 2.0f - 1.0f;
        float ndcY    = 1.0f - (viewportY / m_logicalH) * 2.0f;

        float fovRad  = radians(Settings.FOV);
        float tanHalf = tanf(fovRad * 0.5f);
        float vx      = ndcX * AspectRatio * tanHalf;
        float vy      = ndcY * tanHalf;

        Vec3f r       = GetRight();
        Vec3f u       = GetUp();
        Vec3f f       = GetForward();
        Vec3f dir     = r * vx + u * vy + f;

        float mag     = dir.magnitude();
        return {Position, mag > 0.0001f ? dir / mag : f};
    }

    // ---------------------------------------------------------------------------
    // Private helpers
    // ---------------------------------------------------------------------------

    Vec3f FlyCamera::KeyboardMoveDir() const
    {
        Vec3f fwd = GetForward();
        Vec3f rgt = GetRight();
        Vec3f up  = Vec3f(Camera::WorldUp.x, Camera::WorldUp.y, Camera::WorldUp.z);
        Vec3f dir = {};

        if (Input.Keys[GLFW_KEY_W])
            dir += fwd;
        if (Input.Keys[GLFW_KEY_S])
            dir -= fwd;
        if (Input.Keys[GLFW_KEY_D])
            dir += rgt;
        if (Input.Keys[GLFW_KEY_A])
            dir -= rgt;
        if (Input.Keys[GLFW_KEY_E])
            dir += up;
        if (Input.Keys[GLFW_KEY_Q])
            dir -= up;

        float mag = dir.magnitude();
        return mag > 0.0001f ? dir / mag : dir;
    }

    float FlyCamera::AdaptiveSpeed() const
    {
        if (Hooks.Raycast)
        {
            float hitFwd  = Hooks.Raycast(Position, GetForward(), Settings.MaxMoveSpeed * 10.0f);
            float hitDown = Hooks.Raycast(Position, {0.0f, -1.0f, 0.0f}, Settings.MaxMoveSpeed * 10.0f);
            return clamp(std::min(hitFwd, hitDown) * 0.5f, Settings.MinMoveSpeed, Settings.MaxMoveSpeed);
        }
        return clamp(fabsf(Position.y) * 0.5f, Settings.MinMoveSpeed, Settings.MaxMoveSpeed);
    }

    float FlyCamera::OrbitCollide(float desired) const
    {
        if (Hooks.Raycast)
        {
            Vec3f fwd = GetForward();
            float hit = Hooks.Raycast(m_orbitPivot, -fwd, desired);
            if (hit < desired)
                return hit * 0.9f;
        }
        return desired;
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
        float fovRad  = radians(Settings.FOV);
        float tanHalf = tanf(fovRad * 0.5f);
        float n       = Settings.NearPlane;
        float f       = Settings.FarPlane;
        float a       = AspectRatio;

        // Vulkan: Y flipped, depth range [0, 1].
        Projection    = Mat4f(1.0f / (a * tanHalf), 0.0f, 0.0f, 0.0f, 0.0f, -1.0f / tanHalf, 0.0f, 0.0f, 0.0f, 0.0f, f / (n - f), (n * f) / (n - f), 0.0f, 0.0f, -1.0f, 0.0f);
        m_projDirty   = false;
    }

    // Thin compatibility shim used by RecalculateView/Projection path.
    void FlyCamera::UpdateMatrices()
    {
        if (m_projDirty)
            RecalculateProjection();
        if (m_viewDirty)
            RecalculateView();
    }

} // namespace ZEngine::Rendering::Cameras
