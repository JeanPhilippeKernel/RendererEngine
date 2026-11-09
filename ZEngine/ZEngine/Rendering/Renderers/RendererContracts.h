#pragma once
#include <ZEngine/Core/Maths/Matrix.h>

namespace ZEngine::Rendering::Renderers
{
    /// Shared virtual resource identities, independent of graph implementation.
    struct RendererResourceName
    {
        inline static constexpr const char* FrameDepthRenderTargetName         = "g_frame_depth_render_target";
        inline static constexpr const char* FrameSharedRenderTargetName        = "g_frame_shared_render_target";
        inline static constexpr const char* FrameHdrColorRenderTargetName      = "g_frame_hdr_color_render_target";
        /// Distinct HDR destination: composition never samples its attachment.
        inline static constexpr const char* FrameHdrCompositedRenderTargetName = "g_frame_hdr_composited_render_target";
        inline static constexpr const char* FrameColorRenderTargetName         = "g_frame_color_render_target";
        /// Per-view transients, not revision-owned environment bake LUTs.
        inline static constexpr const char* FrameSkyViewLutName                = "g_frame_sky_view_lut";
        inline static constexpr const char* FrameAerialInscatterName           = "g_frame_aerial_inscatter";
        inline static constexpr const char* FrameAerialTransmittanceName       = "g_frame_aerial_transmittance";
        inline static constexpr const char* GBufferAlbedoAOName                = "g_gbuffer_albedo_ao";
        inline static constexpr const char* GBufferNormalRoughnessName         = "g_gbuffer_normal_roughness";
        inline static constexpr const char* GBufferMetallicEmissiveName        = "g_gbuffer_metallic_emissive";
        /// Discrete object pick token; never sampled by lighting.
        inline static constexpr const char* GBufferObjectIdName                = "g_gbuffer_object_id";
        inline static constexpr const char* SceneCameraBufferName              = "SceneCamera";
    };

    namespace RendererBufferName
    {
        inline constexpr const char* Transform           = "TransformStorageBuffer";
        inline constexpr const char* RenderData          = "RenderDataStorageBuffer";
        inline constexpr const char* Material            = "MaterialStorageBuffer";
        inline constexpr const char* Light               = "LightStorageBuffer";
        inline constexpr const char* CullingInput        = "FrustumCullingInputBuffer";
        inline constexpr const char* CulledIndirect      = "FrustumCulledIndirectBuffer";
        inline constexpr const char* SelectionTokenState = "SelectionTokenStateBuffer";
        inline constexpr const char* GlobalVertex        = "GlobalVertexStorageBuffer";
        inline constexpr const char* GlobalIndex         = "GlobalIndexStorageBuffer";
    } // namespace RendererBufferName

    struct UBOCameraLayout
    {
        ZEngine::Core::Maths::Mat4f View        = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Mat4f Projection  = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        ZEngine::Core::Maths::Vec4f Position    = ZEngine::Core::Maths::Vec4f(0.0f, 0.0f, 0.0f, 1.0f);
        ZEngine::Core::Maths::Mat4f InvViewProj = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
    };

} // namespace ZEngine::Rendering::Renderers
