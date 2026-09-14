#pragma once
#include <ZEngine/Rendering/Textures/Texture.h>
#include <cstdint>

namespace ZEngine::Rendering
{
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
        Textures::TextureHandle DiffuseIrradiance   = {};
        Textures::TextureHandle SpecularEnvironment = {};
        Textures::TextureHandle BrdfIntegrationLut  = {};
        BrdfIntegrationLutKey   BrdfIntegrationKey  = {};
        uint32_t                SpecularMipCount    = 1;

        [[nodiscard]] bool      Valid() const
        {
            return DiffuseIrradiance.Valid() && SpecularEnvironment.Valid() && BrdfIntegrationLut.Valid() && SpecularMipCount > 0;
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
