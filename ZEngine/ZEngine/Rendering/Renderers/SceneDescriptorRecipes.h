#pragma once
#include <ZEngine/Rendering/Renderers/ScenePipelinePreparation.h>
#include <ZEngine/Rendering/Renderers/SceneRenderTargets.h>
#include <cstring>

namespace ZEngine::Rendering::Renderers
{
    enum class SceneDescriptorSampler : uint8_t
    {
        None,
        Wrap,
        Clamp,
        SkyView,
        NearestClamp
    };

    inline SceneDescriptorSampler ResolveSceneDescriptorSampler(RGSceneDescriptorProfile profile, cstring name)
    {
        if (!name)
        {
            return SceneDescriptorSampler::None;
        }
        if (std::strcmp(name, "LinearWrapSampler") == 0)
        {
            return SceneDescriptorSampler::Wrap;
        }
        struct Recipe
        {
            RGSceneDescriptorProfile Profile = RGSceneDescriptorProfile::None;
            cstring                  Name    = nullptr;
            SceneDescriptorSampler   Sampler = SceneDescriptorSampler::None;
        };
        static constexpr Recipe recipes[] = {
            {             RGSceneDescriptorProfile::Lighting,           "GBufferSampler",         SceneDescriptorSampler::Wrap},
            {             RGSceneDescriptorProfile::Lighting,       "EnvironmentSampler",        SceneDescriptorSampler::Clamp},
            {          RGSceneDescriptorProfile::Transparent,       "EnvironmentSampler",        SceneDescriptorSampler::Clamp},
            {RGSceneDescriptorProfile::EnvironmentBackground, "LinearClampToEdgeSampler",        SceneDescriptorSampler::Clamp},
            {          RGSceneDescriptorProfile::ToneMapping, "LinearClampToEdgeSampler",        SceneDescriptorSampler::Clamp},
            {           RGSceneDescriptorProfile::SkyViewLut, "LinearClampToEdgeSampler",        SceneDescriptorSampler::Clamp},
            {    RGSceneDescriptorProfile::AerialPerspective, "LinearClampToEdgeSampler",        SceneDescriptorSampler::Clamp},
            {         RGSceneDescriptorProfile::SkyComposite, "LinearClampToEdgeSampler",        SceneDescriptorSampler::Clamp},
            {         RGSceneDescriptorProfile::SkyComposite,           "SkyViewSampler",      SceneDescriptorSampler::SkyView},
            {                 RGSceneDescriptorProfile::Grid,             "DepthSampler",        SceneDescriptorSampler::Clamp},
            {           RGSceneDescriptorProfile::GridLabels,             "AtlasSampler",        SceneDescriptorSampler::Clamp},
            {     RGSceneDescriptorProfile::SelectionOutline,          "ObjectIdSampler", SceneDescriptorSampler::NearestClamp},
        };
        for (const auto& recipe : recipes)
        {
            if (recipe.Profile == profile && std::strcmp(recipe.Name, name) == 0)
            {
                return recipe.Sampler;
            }
        }
        return SceneDescriptorSampler::None;
    }

    /// Closed recipes: a reflected name from another pass is not an implicit
    /// permission to bind an outgoing graph image or an unrelated environment.
    inline SceneRenderTarget ResolveSceneDescriptorTarget(RGSceneDescriptorProfile profile, cstring name, Specifications::DescriptorType type, bool composited)
    {
        if (!name)
        {
            return SceneRenderTarget::Count;
        }
        if (profile == RGSceneDescriptorProfile::ToneMapping && type == Specifications::DescriptorType::SAMPLED_IMAGE && std::strcmp(name, "SceneColor") == 0)
        {
            return composited ? SceneRenderTarget::HdrComposited : SceneRenderTarget::HdrColor;
        }
        struct Recipe
        {
            RGSceneDescriptorProfile       Profile = RGSceneDescriptorProfile::None;
            cstring                        Name    = nullptr;
            SceneRenderTarget              Target  = SceneRenderTarget::Count;
            Specifications::DescriptorType Type    = Specifications::DescriptorType::SAMPLED_IMAGE;
        };
        static constexpr Recipe recipes[] = {
            {RGSceneDescriptorProfile::Lighting, "GBufferAlbedoAO", SceneRenderTarget::AlbedoAO},
            {RGSceneDescriptorProfile::Lighting, "GBufferNormalRoughness", SceneRenderTarget::NormalRoughness},
            {RGSceneDescriptorProfile::Lighting, "GBufferMetallicEmissive", SceneRenderTarget::MetallicEmissive},
            {RGSceneDescriptorProfile::Lighting, "GBufferDepth", SceneRenderTarget::Depth},
            {RGSceneDescriptorProfile::Grid, "SceneDepth", SceneRenderTarget::Depth},
            {RGSceneDescriptorProfile::GridLabels, "SceneDepth", SceneRenderTarget::Depth},
            {RGSceneDescriptorProfile::SelectionOutline, "ObjectId", SceneRenderTarget::ObjectId},
            {RGSceneDescriptorProfile::SkyComposite, "OpaqueSceneColor", SceneRenderTarget::HdrColor},
            {RGSceneDescriptorProfile::SkyComposite, "SceneDepth", SceneRenderTarget::Depth},
            {RGSceneDescriptorProfile::SkyComposite, "SkyViewLut", SceneRenderTarget::SkyViewLut},
            {RGSceneDescriptorProfile::SkyComposite, "AerialInscatter", SceneRenderTarget::AerialInscatter},
            {RGSceneDescriptorProfile::SkyComposite, "AerialTransmittance", SceneRenderTarget::AerialTransmittance},
            {RGSceneDescriptorProfile::SkyViewLut, "Destination", SceneRenderTarget::SkyViewLut, Specifications::DescriptorType::STORAGE_IMAGE},
            {RGSceneDescriptorProfile::AerialPerspective, "InscatterDestination", SceneRenderTarget::AerialInscatter, Specifications::DescriptorType::STORAGE_IMAGE},
            {RGSceneDescriptorProfile::AerialPerspective, "TransmittanceDestination", SceneRenderTarget::AerialTransmittance, Specifications::DescriptorType::STORAGE_IMAGE},
        };
        for (const auto& recipe : recipes)
        {
            if (recipe.Profile == profile && recipe.Type == type && std::strcmp(recipe.Name, name) == 0)
            {
                return recipe.Target;
            }
        }
        return SceneRenderTarget::Count;
    }

    inline const RenderResourceManager::TextureVersionPin* ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile profile, cstring name, const SceneDescriptorEnvironment& environment)
    {
        if (!name)
        {
            return nullptr;
        }
        if (profile == RGSceneDescriptorProfile::Transparent || profile == RGSceneDescriptorProfile::Lighting)
        {
            if (std::strcmp(name, "DiffuseIrradiance") == 0)
            {
                return environment.DiffuseIrradiance;
            }
            if (std::strcmp(name, "SpecularEnvironment") == 0)
            {
                return environment.SpecularEnvironment;
            }
            if (std::strcmp(name, "BrdfIntegrationLut") == 0)
            {
                return environment.BrdfIntegrationLut;
            }
        }
        if (profile == RGSceneDescriptorProfile::EnvironmentBackground && std::strcmp(name, "EnvMap") == 0)
        {
            return environment.SourceRadiance;
        }
        if (profile == RGSceneDescriptorProfile::SkyViewLut || profile == RGSceneDescriptorProfile::AerialPerspective)
        {
            if (std::strcmp(name, "Transmittance") == 0)
            {
                return environment.AtmosphereTransmittance;
            }
            if (std::strcmp(name, "Multiscattering") == 0)
            {
                return environment.AtmosphereMultiscattering;
            }
        }
        if (profile == RGSceneDescriptorProfile::SkyComposite && std::strcmp(name, "AtmosphereTransmittance") == 0)
        {
            return environment.AtmosphereTransmittance;
        }
        if (profile == RGSceneDescriptorProfile::GridLabels && std::strcmp(name, "FontAtlas") == 0)
        {
            return environment.GridLabelFont;
        }
        return nullptr;
    }
} // namespace ZEngine::Rendering::Renderers
