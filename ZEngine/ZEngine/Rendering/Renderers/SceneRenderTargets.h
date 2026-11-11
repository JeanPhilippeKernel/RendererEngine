#pragma once
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Specifications/TextureSpecification.h>
#include <cstdint>

namespace ZEngine::Rendering::Renderers
{
    enum class SceneRenderTarget : uint8_t
    {
        Depth,
        AlbedoAO,
        NormalRoughness,
        MetallicEmissive,
        ObjectDepth,
        ObjectId,
        HdrColor,
        Color,
        SkyViewLut,
        AerialInscatter,
        AerialTransmittance,
        HdrComposited,
        Count
    };

    inline constexpr uint32_t SceneSkyViewWidth     = 192;
    inline constexpr uint32_t SceneSkyViewHeight    = 108;
    inline constexpr uint32_t SceneAerialResolution = 32;

    constexpr uint32_t        SceneTargetMask(SceneRenderTarget target)
    {
        return 1u << static_cast<uint32_t>(target);
    }

    struct SceneTargetDeclaration
    {
        cstring                              Name = nullptr;
        Specifications::TextureSpecification Spec = {};
    };

    /// Shared by candidate preparation and ordinary graph declarations. Usage is
    /// a conservative superset for core attachments, including readback. Fixed
    /// atmosphere storage keeps its sampled/storage-only usage and 2D/3D shape.
    /// Ordinary recording and preparation must request the same image recipes.
    inline SceneTargetDeclaration DescribeSceneTarget(SceneRenderTarget target, uint32_t width, uint32_t height)
    {
        SceneTargetDeclaration declaration = {};
        auto&                  spec        = declaration.Spec;
        spec.IsRenderTargetSized           = true;
        spec.IsUsageTransferSource         = true;
        spec.IsAliasable                   = true;
        spec.Width                         = width;
        spec.Height                        = height;
        spec.Format                        = Specifications::ImageFormat::R8G8B8A8_UNORM;
        switch (target)
        {
            case SceneRenderTarget::Depth:
                declaration.Name = RendererResourceName::FrameDepthRenderTargetName;
                spec.Format      = Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE;
                break;
            case SceneRenderTarget::AlbedoAO:
                declaration.Name = RendererResourceName::GBufferAlbedoAOName;
                break;
            case SceneRenderTarget::NormalRoughness:
                declaration.Name  = RendererResourceName::GBufferNormalRoughnessName;
                spec.Format       = Specifications::ImageFormat::R16G16B16A16_SFLOAT;
                spec.BytePerPixel = 8;
                break;
            case SceneRenderTarget::MetallicEmissive:
                declaration.Name = RendererResourceName::GBufferMetallicEmissiveName;
                break;
            case SceneRenderTarget::ObjectDepth:
                declaration.Name = "ObjectIdDepth";
                spec.Format      = Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE;
                break;
            case SceneRenderTarget::ObjectId:
                declaration.Name = RendererResourceName::GBufferObjectIdName;
                spec.Format      = Specifications::ImageFormat::R32_UINT;
                break;
            case SceneRenderTarget::HdrColor:
                declaration.Name   = RendererResourceName::FrameHdrColorRenderTargetName;
                spec.Format        = Specifications::ImageFormat::R16G16B16A16_SFLOAT;
                spec.BytePerPixel  = 8;
                spec.ClearColor[0] = spec.ClearColor[1] = spec.ClearColor[2] = 0.11f;
                spec.ClearColor[3]                                           = 1.f;
                break;
            case SceneRenderTarget::Color:
                declaration.Name   = RendererResourceName::FrameColorRenderTargetName;
                spec.ClearColor[3] = 1.f;
                break;
            case SceneRenderTarget::SkyViewLut:
            case SceneRenderTarget::AerialInscatter:
            case SceneRenderTarget::AerialTransmittance:
                declaration.Name           = target == SceneRenderTarget::SkyViewLut ? RendererResourceName::FrameSkyViewLutName : (target == SceneRenderTarget::AerialInscatter ? RendererResourceName::FrameAerialInscatterName : RendererResourceName::FrameAerialTransmittanceName);
                spec.IsRenderTargetSized   = false;
                spec.IsUsageStorage        = true;
                spec.IsUsageTransfert      = false;
                spec.IsUsageTransferSource = false;
                spec.Is3D                  = target != SceneRenderTarget::SkyViewLut;
                spec.Width                 = spec.Is3D ? SceneAerialResolution : SceneSkyViewWidth;
                spec.Height                = spec.Is3D ? SceneAerialResolution : SceneSkyViewHeight;
                spec.Depth                 = spec.Is3D ? SceneAerialResolution : 1;
                spec.Format                = Specifications::ImageFormat::R16G16B16A16_SFLOAT;
                spec.BytePerPixel          = 8;
                break;
            case SceneRenderTarget::HdrComposited:
                declaration.Name           = RendererResourceName::FrameHdrCompositedRenderTargetName;
                spec.IsUsageTransfert      = false;
                spec.IsUsageTransferSource = false;
                spec.Format                = Specifications::ImageFormat::R16G16B16A16_SFLOAT;
                spec.BytePerPixel          = 8;
                spec.ClearColor[3]         = 1.f;
                break;
            case SceneRenderTarget::Count:
                break;
        }
        return declaration;
    }
} // namespace ZEngine::Rendering::Renderers
