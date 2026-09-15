#pragma once
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Rendering/Cameras/CameraEnum.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Rendering::Cameras
{
    enum class CameraMode
    {
        Orbit,
        Free
    };

    struct CameraSetting
    {
        float MinMoveSpeed        = 1.0f;
        float MaxMoveSpeed        = 500.0f;
        float PanSpeed            = 1.0f;
        float RotationSpeed       = 0.25f;
        float OrbitSpeed          = 0.25f;
        float FastSpeedMultiplier = 4.0f;
        float ScrollSpeed         = 0.5f;
        float FocusDuration       = 0.25f; // seconds
        float MinOrbitDistance    = 0.5f;
        float MaxOrbitDistance    = 10000.0f;
        float OrthographicHeight  = 10.0f;
        float FOV                 = 60.0f;
        float NearPlane           = 0.1f;
        float FarPlane            = 10000.0f;
        float SmoothingFactor     = 12.0f; // higher = snappier
    };

    /// @brief Immutable camera state consumed by one render payload.
    ///
    /// The editor camera is updated on the main thread while rendering happens
    /// asynchronously. Render code must consume this value, never a mutable
    /// Camera instance owned by the main thread.
    struct CameraFrameData
    {
        ZEngine::Core::Maths::Mat4f View         = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Mat4f Projection   = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Vec3f Position     = {};
        /// @brief Selects the depth clear convention consumed by depth-aware post effects.
        /// @details The current editor camera uses standard Vulkan depth. This
        /// explicit frame value prevents future reverse-Z views from relying on
        /// projection-matrix heuristics.
        bool                        UsesReverseZ = false;
    };

    struct Camera
    {
        Camera()                                                    = default;
        virtual ~Camera()                                           = default;

        CameraType                                      Type        = CameraType::UNDEFINED;
        CameraMode                                      Mode        = CameraMode::Free;

        float                                           AspectRatio = 16.0f / 9.0f;
        float                                           Pitch       = 0.0f;
        float                                           Yaw         = 0.0f;
        /*
         * Coordinate Vectors
         */
        inline static const ZEngine::Core::Maths::Vec3f WorldUp     = {0.0f, 1.0f, 0.0f};

        ZEngine::Core::Maths::Vec3f                     Position    = {0.0f, 5.f, 10.0f};
        ZEngine::Core::Maths::Vec3f                     Target      = {0.0f, 0.0f, -1.0f};

        CameraSetting                                   Settings    = {};

        virtual const ZEngine::Core::Maths::Mat4f&      GetView() const
        {
            return View;
        }
        virtual const ZEngine::Core::Maths::Mat4f& GetProjection() const
        {
            return Projection;
        }
        virtual ZEngine::Core::Maths::Mat4f GetViewProjection() const
        {
            return Projection * View;
        }

        /// @brief Capture the state that will be consumed by a render payload.
        virtual CameraFrameData CaptureFrameData()
        {
            return {.View = View, .Projection = Projection, .Position = GetPosition()};
        }

        virtual ZEngine::Core::Maths::Vec3f GetPosition() const = 0;
        virtual ZEngine::Core::Maths::Vec3f GetForward() const  = 0;
        virtual ZEngine::Core::Maths::Vec3f GetUp() const       = 0;
        virtual ZEngine::Core::Maths::Vec3f GetRight() const    = 0;

    protected:
        ZEngine::Core::Maths::Mat4f View       = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Mat4f Projection = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
    };
    ZDEFINE_PTR(Camera);
} // namespace ZEngine::Rendering::Cameras
