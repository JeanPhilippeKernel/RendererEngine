#pragma once
#include <ZEngine/Rendering/Textures/Texture.h>
#include <cstdint>

namespace ZEngine::Rendering
{
    /// @brief Selects the fixed resource and sampling budget for environment IBL.
    /// @details This is renderer/project policy, not serialized artistic scene data.
    enum class EnvironmentLightingQualityTier : uint8_t
    {
        Low = 0,
        Standard,
        High,
    };

    /// @brief Fully resolved, immutable resource budget for one environment bake.
    struct EnvironmentLightingBakeSettings
    {
        EnvironmentLightingQualityTier Tier                     = EnvironmentLightingQualityTier::Standard;
        uint32_t                       SourceRadianceResolution = 512;
        uint32_t                       DiffuseResolution        = 32;
        uint32_t                       DiffuseSampleCount       = 32;
        uint32_t                       SpecularResolution       = 128;
        uint32_t                       SpecularSampleCount      = 128;

        [[nodiscard]] constexpr bool   Matches(const EnvironmentLightingBakeSettings& other) const
        {
            return Tier == other.Tier && SourceRadianceResolution == other.SourceRadianceResolution && DiffuseResolution == other.DiffuseResolution && DiffuseSampleCount == other.DiffuseSampleCount && SpecularResolution == other.SpecularResolution && SpecularSampleCount == other.SpecularSampleCount;
        }

        [[nodiscard]] constexpr bool IsValid() const
        {
            return SourceRadianceResolution > 0 && DiffuseResolution > 0 && DiffuseSampleCount > 0 && SpecularResolution > 0 && SpecularSampleCount > 0;
        }
    };

    /// @brief Resolves the project-selected quality tier into immutable bake settings.
    [[nodiscard]] constexpr EnvironmentLightingBakeSettings ResolveEnvironmentLightingQuality(EnvironmentLightingQualityTier tier)
    {
        switch (tier)
        {
            case EnvironmentLightingQualityTier::Low:
                return {EnvironmentLightingQualityTier::Low, 128, 16, 16, 64, 64};
            case EnvironmentLightingQualityTier::High:
                return {EnvironmentLightingQualityTier::High, 1024, 64, 64, 256, 256};
            case EnvironmentLightingQualityTier::Standard:
            default:
                return {EnvironmentLightingQualityTier::Standard, 512, 32, 32, 128, 128};
        }
    }

    /// @brief Versioned recipe used to create the engine-global BRDF integration LUT.
    struct BrdfIntegrationLutKey
    {
        uint32_t           ShaderVersion = 1;
        uint32_t           Resolution    = 512;
        uint32_t           SampleCount   = 64;

        [[nodiscard]] bool Matches(const BrdfIntegrationLutKey& other) const
        {
            return ShaderVersion == other.ShaderVersion && Resolution == other.Resolution && SampleCount == other.SampleCount;
        }
    };

    /// @brief Engine-global textures consumed by deferred environment lighting.
    /// @details The fallback textures and BRDF LUT are owned by RenderResourceManager;
    ///          scene snapshots only borrow these handles until per-scene IBL baking lands.
    struct EnvironmentLightingResources
    {
        Textures::TextureHandle         DiffuseIrradiance   = {};
        Textures::TextureHandle         SpecularEnvironment = {};
        Textures::TextureHandle         BrdfIntegrationLut  = {};
        BrdfIntegrationLutKey           BrdfIntegrationKey  = {};
        EnvironmentLightingBakeSettings BakeSettings        = {};
        uint32_t                        SpecularMipCount    = 1;

        [[nodiscard]] bool              Valid() const
        {
            return DiffuseIrradiance.Valid() && SpecularEnvironment.Valid() && BrdfIntegrationLut.Valid() && BakeSettings.IsValid() && SpecularMipCount > 0;
        }
    };

    /// @brief Per-frame presentation values shared by sky and deferred IBL sampling.
    struct EnvironmentLightingPushConstants
    {
        float TintIntensity[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float YawRadians       = 0.0f;
        float SpecularMaxLod   = 0.0f;
        float Padding[2]       = {};
    };

    static_assert(sizeof(EnvironmentLightingPushConstants) == 32, "Environment lighting push constants must match GLSL");
} // namespace ZEngine::Rendering
