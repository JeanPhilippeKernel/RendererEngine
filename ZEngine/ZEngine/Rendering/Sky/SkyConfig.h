#pragma once
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Rendering::Sky
{
    enum class SkyMode : uint8_t
    {
        Atmosphere = 0,
        HDRI       = 1,
        SkySphere  = 2,
    };

    /**
     * @brief Renderer-level sky configuration. Populated at scene load from the
     *        per-scene data deserialized by EditorSceneSerializer, then passed to
     *        SkySystem::SetConfig.
     *
     * Atmosphere parameter defaults are physical values for Earth's standard atmosphere.
     *
     * References:
     *   [Bruneton08]  E. Bruneton & F. Neyret, "Precomputed Atmospheric Scattering",
     *                 Eurographics Symposium on Rendering 2008.
     *   [Hillaire20]  S. Hillaire, "A Scalable and Production Ready Sky and Atmosphere
     *                 Rendering Technique", EG Symposium on Rendering 2020, Table 1.
     *   [AFGL86]      AFGL Atmospheric Constituent Profiles (0–120 km), Tech. Report
     *                 AFGL-TR-86-0110, 1986.
     */
    struct SkyConfig
    {
        SkyMode            Mode                = SkyMode::Atmosphere;

        /// Rayleigh scattering coefficients (RGB, m⁻¹) at sea level. [Bruneton08]
        Core::Maths::Vec4f RayleighScattering  = {5.802e-6f, 13.558e-6f, 33.100e-6f, 0.0f};
        /// Rayleigh exponential scale height (m). [Bruneton08]
        float              RayleighScaleHeight = 8000.0f;
        /// Mie scattering coefficient (m⁻¹), wavelength-independent. [Hillaire20 Table 1]
        float              MieScattering       = 3.996e-6f;
        /// Mie absorption coefficient (m⁻¹). [Hillaire20 Table 1]
        float              MieAbsorption       = 4.400e-6f;
        /// Mie exponential scale height (m). [Hillaire20 Table 1]
        float              MieScaleHeight      = 1200.0f;
        /// Henyey-Greenstein asymmetry factor for Mie phase (forward-scattering aerosols). [Hillaire20]
        float              MieAnisotropy       = 0.8f;
        /// Centre altitude of the ozone layer (m). [AFGL86]
        float              OzoneLayerCentre    = 25000.0f;
        /// Width of the ozone layer (m). [AFGL86]
        float              OzoneLayerWidth     = 15000.0f;
        /// Ozone absorption cross-sections (RGB, m⁻¹). [Bruneton08]
        Core::Maths::Vec4f OzoneAbsorption     = {0.650e-6f, 1.881e-6f, 0.085e-6f, 0.0f};
        /// Earth mean radius (km).
        float              PlanetRadiusKm      = 6360.0f;
        /// Atmosphere boundary radius (km), 60 km above surface.
        float              AtmosphereRadiusKm  = 6420.0f;
        /// Angular radius of the sun disc (degrees); measured value ≈ 0.5334°.
        float              SunAngularRadiusDeg = 0.5334f;
        /// Artistic scale applied to sun illuminance; not a physical constant.
        float              SunIlluminanceScale = 10.0f;

        cstring            EnvironmentMapPath  = nullptr;
        float              HDRIExposure        = 1.0f;
        Core::Maths::Vec4f HDRITint            = {1.0f, 1.0f, 1.0f, 1.0f};

        /// World-space sun direction (unit vector). Updated each frame from the first active
        /// directional light. Falls back to normalize(1,1,0) when no directional light exists.
        Core::Maths::Vec4f SunDirection        = {0.577f, 0.577f, 0.577f, 0.0f};

        Core::Maths::Vec4f HorizonColor        = {0.53f, 0.81f, 0.98f, 1.0f};
        Core::Maths::Vec4f ZenithColor         = {0.10f, 0.31f, 0.72f, 1.0f};
        Core::Maths::Vec4f GroundColor         = {0.30f, 0.27f, 0.24f, 1.0f};
        float              SunDiscSize         = 0.005f;
        float              SunDiscIntensity    = 5.0f;
        float              HorizonSharpness    = 8.0f;
        bool               ShowSunDisc         = true;
    };

    struct SkySpherePush
    {
        float HorizonColor[4];
        float ZenithColor[4];
        float GroundColor[4];
        float SunDirection[4];
        float SunDiscSize;
        float SunDiscIntensity;
        float HorizonSharpness;
        float ShowSunDisc;
    };
    static_assert(sizeof(SkySpherePush) == 80);
} // namespace ZEngine::Rendering::Sky
