#pragma once
#include <Core/Maths/Matrix.h>
#include <Rendering/Cameras/CameraEnum.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Cameras
{
    struct Camera
    {
        float                               Fov                          = 0.0f;
        float                               AspectRatio                  = 0.0f;
        float                               ClipNear                     = 0.1f;
        float                               ClipFar                      = 1000.0f;
        /*
         * Coordinate Vectors
         */
        ZEngine::Core::Maths::Vec3f         Position                     = {0.0f, 0.0f, 0.0f};
        ZEngine::Core::Maths::Vec3f         Up                           = {0.0f, 1.0f, 0.0f};
        const ZEngine::Core::Maths::Vec3f   WorldUp                      = {0.0f, 1.0f, 0.0f};
        ZEngine::Core::Maths::Vec3f         Right                        = {0.0f, 0.0f, 0.0f};
        ZEngine::Core::Maths::Vec3f         Forward                      = {0.0f, 0.0f, 0.0f};
        ZEngine::Core::Maths::Vec3f         Target                       = {0.0f, 0.0f, -1.0f};
        /*
         * Matrices
         */
        ZEngine::Core::Maths::Mat4f         View                         = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Mat4f         Projection                   = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Mat4f         ViewProjection               = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        /*
         * Extras data
         */
        CameraType                          Type                         = CameraType::UNDEFINED;

        virtual ZEngine::Core::Maths::Mat4f GetViewMatrix()              = 0;
        virtual ZEngine::Core::Maths::Mat4f GetPerspectiveMatrix() const = 0;
        virtual ZEngine::Core::Maths::Vec3f GetPosition() const          = 0;
    };
    ZDEFINE_PTR(Camera);
} // namespace ZEngine::Rendering::Cameras
