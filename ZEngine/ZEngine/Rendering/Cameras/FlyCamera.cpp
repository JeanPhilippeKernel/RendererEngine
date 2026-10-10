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
        AspectRatio                      = aspectRatio;
        Settings                         = settings;
        Position                         = {0.0f, 5.0f, 8.0f};
        Pitch                            = radians(30.0f);
        m_targetPitch                    = Pitch;
        m_targetPos                      = Position;
        m_animDuration                   = Settings.FocusDuration;
        const float minimum_ortho_height = Settings.MinOrbitDistance * 2.0f;
        m_orthographicHeight             = std::isfinite(Settings.OrthographicHeight) ? std::max(Settings.OrthographicHeight, minimum_ortho_height) : minimum_ortho_height;
        Type                             = CameraType::PERSPECTIVE;
        m_projDirty                      = true;
        m_viewDirty                      = true;
        UpdateMatrices();
    }

    // Public accessors

    Quaternion<float> FlyCamera::GetOrientation() const
    {
        return fromEulerAngles(-Pitch, -Yaw, 0.0f);
    }

    CameraFrameData FlyCamera::CaptureFrameData()
    {
        UpdateMatrices();
        return Camera::CaptureFrameData();
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

    // Configuration

    void FlyCamera::SetViewportSize(float logicalW, float logicalH)
    {
        if (!std::isfinite(logicalW) || !std::isfinite(logicalH) || logicalW <= 0.0f || logicalH <= 0.0f)
            return;

        if (m_logicalW == logicalW && m_logicalH == logicalH)
            return;

        m_logicalW  = logicalW;
        m_logicalH  = logicalH;
        AspectRatio = logicalW / logicalH;
        m_projDirty = true;
        RecalculateProjection();
    }

    void FlyCamera::SetPosition(Vec3f position)
    {
        Position = m_targetPos = ConstrainPositionToGround(position);
        m_viewDirty            = true;
    }

    void FlyCamera::SetOrientation(float pitchDeg, float yawDeg)
    {
        Pitch = m_targetPitch = clamp(radians(pitchDeg), -kPitchLimit, kPitchLimit);
        Yaw = m_targetYaw = WrapAngle(radians(yawDeg));
        m_viewDirty       = true;
    }

    void FlyCamera::SetProjectionType(CameraType type)
    {
        if (type != CameraType::PERSPECTIVE && type != CameraType::ORTHOGRAPHIC)
            return;
        if (Type == type)
            return;

        // Preserve the apparent vertical scale at the current orbit distance
        // when entering an orthographic view.
        if (type == CameraType::ORTHOGRAPHIC)
        {
            const float perspective_height = 2.0f * std::max(m_orbitDist, Settings.MinOrbitDistance) * tanf(radians(Settings.FOV) * 0.5f);
            m_orthographicHeight           = std::max(perspective_height, Settings.MinOrbitDistance * 2.0f);
        }

        Type        = type;
        m_projDirty = true;
    }

    CameraType FlyCamera::GetProjectionType() const
    {
        return Type;
    }

    void FlyCamera::SetAxisView(FlyCameraAxisView view)
    {
        float pitch = Pitch;
        float yaw   = Yaw;

        switch (view)
        {
            case FlyCameraAxisView::Front:
                pitch = 0.0f;
                yaw   = 0.0f;
                break;
            case FlyCameraAxisView::Back:
                pitch = 0.0f;
                yaw   = PI<float>;
                break;
            case FlyCameraAxisView::Right:
                pitch = 0.0f;
                yaw   = HALF_PI<float>;
                break;
            case FlyCameraAxisView::Left:
                pitch = 0.0f;
                yaw   = -HALF_PI<float>;
                break;
            // Stay infinitesimally clear of the Euler singularity.  This
            // keeps a subsequent mouse-look stable without a visible tilt.
            case FlyCameraAxisView::Top:
                pitch = kPitchLimit;
                yaw   = 0.0f;
                break;
            case FlyCameraAxisView::Bottom:
                pitch = -kPitchLimit;
                yaw   = 0.0f;
                break;
            case FlyCameraAxisView::None:
                return;
        }

        Pitch             = pitch;
        Yaw               = WrapAngle(yaw);
        m_targetPitch     = Pitch;
        m_targetYaw       = Yaw;
        m_targetPos       = Position;
        m_stateBeforeAnim = FlyCameraState::Free;
        State             = FlyCameraState::Free;
        m_viewDirty       = true;
    }

    // OnUpdate — main entry point called once per frame by the controller

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
                const Scenes::SceneRaycastHit hit = Hooks.Raycast(Hooks.Context, Position, GetForward(), m_orbitDist * 2.0f);
                if (hit.Hit && std::isfinite(hit.Distance) && hit.Distance > 0.0f)
                    pivotDist = hit.Distance;
            }
            m_orbitPivot      = Position + GetForward() * pivotDist;
            m_orbitDist       = clamp((Position - m_orbitPivot).magnitude(), Settings.MinOrbitDistance, Settings.MaxOrbitDistance);
            m_targetOrbitDist = m_orbitDist;
            State             = FlyCameraState::Orbit;
        }

        // Orbit exit (Alt released, or no buttons held).
        if (State == FlyCameraState::Orbit && (!Input.AltDown || (!Input.LeftDown && !Input.RightDown)))
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

        HandleCommands();
        ApplyGroundConstraint();

        if (m_projDirty)
            RecalculateProjection();
        if (m_viewDirty)
            RecalculateView();

        Input.FlushDeltas();
    }

    // Private update methods

    void FlyCamera::UpdateFree(float dt)
    {
        float t = SmoothT(Settings.SmoothingFactor, dt);

        if (Input.RightDown)
        {
            float speed  = AdaptiveSpeed() * (Input.ShiftDown ? Settings.FastSpeedMultiplier : 1.0f);
            m_targetPos += KeyboardMoveDir() * speed * dt;

            ApplyLookDelta(Settings.RotationSpeed);
        }

        if (Input.ScrollDelta != 0.0f)
        {
            if (Type == CameraType::ORTHOGRAPHIC)
            {
                const float zoom     = expf(-Input.ScrollDelta * Settings.ScrollSpeed * 0.25f);
                m_orthographicHeight = clamp(m_orthographicHeight * zoom, Settings.MinOrbitDistance * 2.0f, Settings.MaxOrbitDistance * 2.0f);
                m_projDirty          = true;
            }
            else
            {
                Ray   ray    = GetRayFromViewport(Input.MouseViewportX, Input.MouseViewportY);
                float speed  = std::min(AdaptiveSpeed(), 3.0f);
                m_targetPos += ray.Direction * Input.ScrollDelta * Settings.ScrollSpeed * speed;
            }
        }

        if ((m_targetPos - Position).magnitude() > 0.00001f)
        {
            Position    = lerp(Position, m_targetPos, t);
            m_viewDirty = true;
        }
    }

    void FlyCamera::UpdateOrbit(float dt)
    {
        float t = SmoothT(Settings.SmoothingFactor, dt);

        if (Input.AltDown && (Input.LeftDown || Input.RightDown))
        {
            ApplyLookDelta(Settings.OrbitSpeed);
        }

        if (Input.ScrollDelta != 0.0f)
        {
            if (Type == CameraType::ORTHOGRAPHIC)
            {
                const float zoom     = expf(-Input.ScrollDelta * Settings.ScrollSpeed * 0.25f);
                m_orthographicHeight = clamp(m_orthographicHeight * zoom, Settings.MinOrbitDistance * 2.0f, Settings.MaxOrbitDistance * 2.0f);
                m_projDirty          = true;
            }
            else
            {
                float dist         = std::max(m_targetOrbitDist * 0.2f, 0.001f);
                float speed        = std::min(dist * dist, 100.0f);
                m_targetOrbitDist -= Input.ScrollDelta * speed * Settings.ScrollSpeed;
                m_targetOrbitDist  = clamp(m_targetOrbitDist, Settings.MinOrbitDistance, Settings.MaxOrbitDistance);
            }
        }

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
        float planeH     = Type == CameraType::ORTHOGRAPHIC ? m_orthographicHeight : 2.0f * tanf(fovRad * 0.5f) * focalDist;
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

    // Focus / bookmarks

    void FlyCamera::FocusOn(Vec3f center, float radius)
    {
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(radius) || radius <= 0.0f)
            return;

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
        if (Type == CameraType::ORTHOGRAPHIC)
        {
            m_orthographicHeight = clamp(radius * 3.0f, Settings.MinOrbitDistance * 2.0f, Settings.MaxOrbitDistance * 2.0f);
            m_projDirty          = true;
        }

        m_animStartPos    = Position;
        m_animEndPos      = ConstrainPositionToGround(endPos);
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
        m_animEndPos      = ConstrainPositionToGround(bm.Pos);
        m_animStartPitch  = Pitch;
        m_animStartYaw    = Yaw;
        m_animEndPitch    = bm.Pitch;
        m_animEndYaw      = bm.Yaw;
        m_animTimer       = 0.0f;
        m_animDuration    = Settings.FocusDuration;
        m_stateBeforeAnim = State;
        State             = FlyCameraState::Animating;
    }

    // Ray unprojection

    FlyCamera::Ray FlyCamera::GetRayFromViewport(float viewportX, float viewportY) const
    {
        // NDC in [-1,1]; viewport coords are logical-pixel-relative, Y=0 at top.
        float ndcX = (viewportX / m_logicalW) * 2.0f - 1.0f;
        float ndcY = 1.0f - (viewportY / m_logicalH) * 2.0f;

        Vec3f r    = GetRight();
        Vec3f u    = GetUp();
        Vec3f f    = GetForward();

        if (Type == CameraType::ORTHOGRAPHIC)
        {
            const float half_height = m_orthographicHeight * 0.5f;
            const float half_width  = half_height * AspectRatio;
            return {Position + r * (ndcX * half_width) + u * (ndcY * half_height), f};
        }

        float fovRad  = radians(Settings.FOV);
        float tanHalf = tanf(fovRad * 0.5f);
        float vx      = ndcX * AspectRatio * tanHalf;
        float vy      = ndcY * tanHalf;
        Vec3f dir     = r * vx + u * vy + f;

        float mag     = dir.magnitude();
        return {Position, mag > 0.0001f ? dir / mag : f};
    }

    // Private helpers

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
        const float fallback_speed = clamp(fabsf(Position.y) * 0.5f, Settings.MinMoveSpeed, Settings.MaxMoveSpeed);
        if (Hooks.Raycast)
        {
            const float                   query_distance = Settings.MaxMoveSpeed * 10.0f;
            const Scenes::SceneRaycastHit forward_hit    = Hooks.Raycast(Hooks.Context, Position, GetForward(), query_distance);
            const Scenes::SceneRaycastHit down_hit       = Hooks.Raycast(Hooks.Context, Position, {0.0f, -1.0f, 0.0f}, query_distance);
            float                         closest        = query_distance;
            bool                          has_hit        = false;

            if (forward_hit.Hit && std::isfinite(forward_hit.Distance) && forward_hit.Distance > 0.0f)
            {
                closest = forward_hit.Distance;
                has_hit = true;
            }
            if (down_hit.Hit && std::isfinite(down_hit.Distance) && down_hit.Distance > 0.0f)
            {
                closest = has_hit ? std::min(closest, down_hit.Distance) : down_hit.Distance;
                has_hit = true;
            }
            if (has_hit)
                return clamp(closest * 0.5f, Settings.MinMoveSpeed, Settings.MaxMoveSpeed);
        }
        return fallback_speed;
    }

    float FlyCamera::OrbitCollide(float desired) const
    {
        if (Hooks.Raycast)
        {
            const Vec3f                   fwd = GetForward();
            const Scenes::SceneRaycastHit hit = Hooks.Raycast(Hooks.Context, m_orbitPivot, -fwd, desired);
            if (hit.Hit && std::isfinite(hit.Distance) && hit.Distance > 0.0f && hit.Distance < desired)
                return std::max(hit.Distance * 0.9f, Settings.MinOrbitDistance);
        }
        return desired;
    }

    Vec3f FlyCamera::ConstrainPositionToGround(Vec3f position) const
    {
        if (!Hooks.GetGroundConstraint)
            return position;

        Vec3f center = {};
        float radius = 0.0f;
        if (!Hooks.GetGroundConstraint(Hooks.Context, center, radius) || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(radius) || radius <= 0.0f)
            return position;

        const Vec3f relative         = position - center;
        const float distance_squared = dot(relative, relative);
        const float radius_squared   = radius * radius;
        if (!std::isfinite(distance_squared) || !std::isfinite(radius_squared) || distance_squared >= radius_squared)
            return position;

        const float distance  = sqrtf(distance_squared);
        const Vec3f direction = distance > 1.0e-5f ? relative / distance : Vec3f(0.0f, 1.0f, 0.0f);
        return center + direction * radius;
    }

    void FlyCamera::ApplyGroundConstraint()
    {
        const Vec3f constrained_position = ConstrainPositionToGround(Position);
        const Vec3f constrained_target   = ConstrainPositionToGround(m_targetPos);
        if (constrained_position.x != Position.x || constrained_position.y != Position.y || constrained_position.z != Position.z)
        {
            Position    = constrained_position;
            m_viewDirty = true;
        }
        m_targetPos = constrained_target;
    }

    void FlyCamera::HandleCommands()
    {
        if (Input.ToggleProjectionRequested)
            SetProjectionType(Type == CameraType::PERSPECTIVE ? CameraType::ORTHOGRAPHIC : CameraType::PERSPECTIVE);

        if (Input.AxisViewRequested != FlyCameraAxisView::None)
            SetAxisView(Input.AxisViewRequested);

        Vec3f center = {};
        float radius = 0.0f;
        if (Input.FrameSelectionRequested && Hooks.GetSelectionBounds && Hooks.GetSelectionBounds(Hooks.Context, center, radius))
            FocusOn(center, radius);
        if (Input.FrameAllRequested && Hooks.GetSceneBounds && Hooks.GetSceneBounds(Hooks.Context, center, radius))
            FocusOn(center, radius);

        if (Input.BookmarkSlotRequested >= 0 && Input.BookmarkSlotRequested < 9)
        {
            if (Input.SaveBookmarkRequested)
                SaveBookmark(Input.BookmarkSlotRequested);
            else
                RecallBookmark(Input.BookmarkSlotRequested);
        }

        Input.ClearCommands();
    }

    void FlyCamera::ApplyLookDelta(float speed)
    {
        if (Input.MouseDeltaX == 0.0f && Input.MouseDeltaY == 0.0f)
            return;

        const float yaw_sign = GetUp().y < 0.0f ? -1.0f : 1.0f;
        Yaw                  = WrapAngle(Yaw - yaw_sign * (Input.MouseDeltaX / m_logicalW) * PI<float> * speed);
        Pitch                = clamp(Pitch - (Input.MouseDeltaY / m_logicalH) * PI<float> * speed, -kPitchLimit, kPitchLimit);
        m_targetYaw          = Yaw;
        m_targetPitch        = Pitch;
        m_viewDirty          = true;
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
        if (Type == CameraType::ORTHOGRAPHIC)
        {
            const float half_height = m_orthographicHeight * 0.5f;
            const float half_width  = half_height * AspectRatio;
            const float n           = Settings.NearPlane;
            const float f           = Settings.FarPlane;

            // Vulkan: Y flipped, depth range [0, 1].
            Projection              = Mat4f(1.0f / half_width, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f / half_height, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f / (n - f), n / (n - f), 0.0f, 0.0f, 0.0f, 1.0f);
            m_projDirty             = false;
            return;
        }

        float fovRad  = radians(Settings.FOV);
        float tanHalf = tanf(fovRad * 0.5f);
        float n       = Settings.NearPlane;
        float f       = Settings.FarPlane;
        float a       = AspectRatio;

        // Vulkan: Y flipped, depth range [0, 1].
        Projection    = Mat4f(1.0f / (a * tanHalf), 0.0f, 0.0f, 0.0f, 0.0f, -1.0f / tanHalf, 0.0f, 0.0f, 0.0f, 0.0f, f / (n - f), (n * f) / (n - f), 0.0f, 0.0f, -1.0f, 0.0f);
        m_projDirty   = false;
    }

    void FlyCamera::UpdateMatrices()
    {
        if (m_projDirty)
            RecalculateProjection();
        if (m_viewDirty)
            RecalculateView();
    }

} // namespace ZEngine::Rendering::Cameras
