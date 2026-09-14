#pragma once
#include <ZEngine/Helpers/SerializerCommonHelper.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <cstdint>
#include <istream>
#include <ostream>

namespace ZEngine::Rendering::Scenes::Serialization
{
    inline void WriteFloatArray(std::ostream& out, const float* values, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            Helpers::WriteBinary(out, values[i]);
    }

    inline bool ReadFloatArray(std::istream& in, float* values, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            if (!Helpers::ReadBinary(in, values[i]))
                return false;
        return true;
    }

    /// @brief Writes the versioned scene representation of stable sky authoring data.
    ///
    /// Native texture handles, generated-cache paths, revision state, and other
    /// per-frame values are intentionally not part of this codec.
    inline void WriteSkyConfig(std::ostream& out, const SkyConfig& source)
    {
        SkyConfig sky = source;
        sky.Sanitize();
        const uint8_t mode          = static_cast<uint8_t>(sky.Mode);
        const uint8_t show_sun_disc = sky.Sphere.ShowSunDisc ? 1u : 0u;

        Helpers::WriteBinary(out, mode);
        Helpers::WriteBinary(out, sky.EnvironmentMap);
        Helpers::WriteBinary(out, sky.EnvironmentIntensity);
        WriteFloatArray(out, sky.EnvironmentTint, 4);
        Helpers::WriteBinary(out, sky.EnvironmentYawRadians);
        Helpers::WriteBinary(out, sky.PrimaryCelestialLight);

        const auto& atmosphere = sky.Atmosphere;
        WriteFloatArray(out, atmosphere.PlanetCenterWorld, 3);
        Helpers::WriteBinary(out, atmosphere.WorldUnitsPerMeter);
        Helpers::WriteBinary(out, atmosphere.PlanetRadiusKilometers);
        Helpers::WriteBinary(out, atmosphere.AtmosphereRadiusKilometers);
        WriteFloatArray(out, atmosphere.RayleighScatteringPerKilometer, 3);
        Helpers::WriteBinary(out, atmosphere.RayleighScaleHeightKilometers);
        Helpers::WriteBinary(out, atmosphere.MieScatteringPerKilometer);
        Helpers::WriteBinary(out, atmosphere.MieAbsorptionPerKilometer);
        Helpers::WriteBinary(out, atmosphere.MieScaleHeightKilometers);
        Helpers::WriteBinary(out, atmosphere.MieAnisotropy);
        WriteFloatArray(out, atmosphere.OzoneAbsorptionPerKilometer, 3);
        Helpers::WriteBinary(out, atmosphere.OzoneCenterKilometers);
        Helpers::WriteBinary(out, atmosphere.OzoneThicknessKilometers);
        Helpers::WriteBinary(out, atmosphere.SunAngularRadiusRadians);
        Helpers::WriteBinary(out, atmosphere.SunIlluminanceLux);

        const auto& sphere = sky.Sphere;
        WriteFloatArray(out, sphere.HorizonColor, 4);
        WriteFloatArray(out, sphere.ZenithColor, 4);
        WriteFloatArray(out, sphere.GroundColor, 4);
        Helpers::WriteBinary(out, sphere.SunDiscAngularRadiusRadians);
        Helpers::WriteBinary(out, sphere.SunDiscIntensity);
        Helpers::WriteBinary(out, sphere.HorizonSharpness);
        Helpers::WriteBinary(out, show_sun_disc);
    }

    /// @brief Reads stable sky authoring data and sanitizes corrupt numeric values.
    /// @return False only for a truncated or structurally malformed payload.
    inline bool ReadSkyConfig(std::istream& in, SkyConfig& sky)
    {
        uint8_t mode          = 0;
        uint8_t show_sun_disc = 0;
        if (!Helpers::ReadBinary(in, mode) || !Helpers::ReadBinary(in, sky.EnvironmentMap) || !Helpers::ReadBinary(in, sky.EnvironmentIntensity) || !ReadFloatArray(in, sky.EnvironmentTint, 4) || !Helpers::ReadBinary(in, sky.EnvironmentYawRadians) || !Helpers::ReadBinary(in, sky.PrimaryCelestialLight))
            return false;

        sky.Mode         = static_cast<SkyMode>(mode);
        auto& atmosphere = sky.Atmosphere;
        if (!ReadFloatArray(in, atmosphere.PlanetCenterWorld, 3) || !Helpers::ReadBinary(in, atmosphere.WorldUnitsPerMeter) || !Helpers::ReadBinary(in, atmosphere.PlanetRadiusKilometers) || !Helpers::ReadBinary(in, atmosphere.AtmosphereRadiusKilometers) || !ReadFloatArray(in, atmosphere.RayleighScatteringPerKilometer, 3) || !Helpers::ReadBinary(in, atmosphere.RayleighScaleHeightKilometers) || !Helpers::ReadBinary(in, atmosphere.MieScatteringPerKilometer) || !Helpers::ReadBinary(in, atmosphere.MieAbsorptionPerKilometer) ||
            !Helpers::ReadBinary(in, atmosphere.MieScaleHeightKilometers) || !Helpers::ReadBinary(in, atmosphere.MieAnisotropy) || !ReadFloatArray(in, atmosphere.OzoneAbsorptionPerKilometer, 3) || !Helpers::ReadBinary(in, atmosphere.OzoneCenterKilometers) || !Helpers::ReadBinary(in, atmosphere.OzoneThicknessKilometers) || !Helpers::ReadBinary(in, atmosphere.SunAngularRadiusRadians) || !Helpers::ReadBinary(in, atmosphere.SunIlluminanceLux))
            return false;

        auto& sphere = sky.Sphere;
        if (!ReadFloatArray(in, sphere.HorizonColor, 4) || !ReadFloatArray(in, sphere.ZenithColor, 4) || !ReadFloatArray(in, sphere.GroundColor, 4) || !Helpers::ReadBinary(in, sphere.SunDiscAngularRadiusRadians) || !Helpers::ReadBinary(in, sphere.SunDiscIntensity) || !Helpers::ReadBinary(in, sphere.HorizonSharpness) || !Helpers::ReadBinary(in, show_sun_disc) || show_sun_disc > 1u)
            return false;

        sphere.ShowSunDisc = show_sun_disc != 0;
        sky.Sanitize();
        return true;
    }
} // namespace ZEngine::Rendering::Scenes::Serialization
