#pragma once
#include <ZEngine/Core/Maths/Matrix.h>

namespace ZEngine::Rendering::Renderers
{
    namespace RendererBufferName
    {
        inline constexpr const char* Transform      = "TransformStorageBuffer";
        inline constexpr const char* RenderData     = "RenderDataStorageBuffer";
        inline constexpr const char* Material       = "MaterialStorageBuffer";
        inline constexpr const char* Light          = "LightStorageBuffer";
        inline constexpr const char* CullingInput   = "FrustumCullingInputBuffer";
        inline constexpr const char* CulledIndirect = "FrustumCulledIndirectBuffer";
        inline constexpr const char* GlobalVertex   = "GlobalVertexStorageBuffer";
        inline constexpr const char* GlobalIndex    = "GlobalIndexStorageBuffer";
    } // namespace RendererBufferName

    struct UBOCameraLayout
    {
        ZEngine::Core::Maths::Mat4f View        = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Mat4f Projection  = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Vec4f Position    = ZEngine::Core::Maths::Vec4f(0.0f, 0.0f, 0.0f, 1.0f);
        ZEngine::Core::Maths::Mat4f InvViewProj = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
    };

} // namespace ZEngine::Rendering::Renderers
