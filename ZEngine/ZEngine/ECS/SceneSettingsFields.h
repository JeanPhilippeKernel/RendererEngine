#pragma once
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

// One authored scalar inventory for YAML and portable binary sky settings.
// Field order/counts are the v1 binary contract; changes require versioning.
namespace ZEngine::ECS::SceneSettingsDetail
{
    struct FloatField
    {
        cstring Key   = nullptr;
        float*  Data  = nullptr;
        size_t  Count = 0;
    };

    inline auto SkyEnvironment(Rendering::Scenes::SkyConfig& sky)
    {
        return Core::Containers::FixedArray{
            FloatField{  "environment_intensity",  &sky.EnvironmentIntensity, 1},
            FloatField{       "environment_tint",        sky.EnvironmentTint, 4},
            FloatField{"environment_yaw_radians", &sky.EnvironmentYawRadians, 1},
        };
    }

    inline auto SkyPresentation(Rendering::Scenes::SkyConfig& sky)
    {
        return Core::Containers::FixedArray{
            FloatField{                     "sky_luminance_factor",                    &sky.SkyLuminanceFactor, 1},
            FloatField{                    "sky_diffuse_intensity",                   &sky.SkyDiffuseIntensity, 1},
            FloatField{                   "sky_specular_intensity",                  &sky.SkySpecularIntensity, 1},
            FloatField{               "sky_lower_hemisphere_color",                sky.SkyLowerHemisphereColor, 3},
            FloatField{"aerial_perspective_start_depth_kilometers", &sky.AerialPerspectiveStartDepthKilometers, 1},
            FloatField{        "aerial_perspective_distance_scale",        &sky.AerialPerspectiveDistanceScale, 1},
        };
    }

    inline auto SkyAtmosphere(Rendering::Scenes::AtmosphereSettings& sky)
    {
        return Core::Containers::FixedArray{
            FloatField{              "planet_center_world",              sky.PlanetCenterWorld, 3},
            FloatField{            "world_units_per_meter",            &sky.WorldUnitsPerMeter, 1},
            FloatField{         "planet_radius_kilometers",        &sky.PlanetRadiusKilometers, 1},
            FloatField{     "atmosphere_radius_kilometers",    &sky.AtmosphereRadiusKilometers, 1},
            FloatField{"rayleigh_scattering_per_kilometer", sky.RayleighScatteringPerKilometer, 3},
            FloatField{ "rayleigh_scale_height_kilometers", &sky.RayleighScaleHeightKilometers, 1},
            FloatField{     "mie_scattering_per_kilometer",     &sky.MieScatteringPerKilometer, 1},
            FloatField{     "mie_absorption_per_kilometer",     &sky.MieAbsorptionPerKilometer, 1},
            FloatField{      "mie_scale_height_kilometers",      &sky.MieScaleHeightKilometers, 1},
            FloatField{                   "mie_anisotropy",                 &sky.MieAnisotropy, 1},
            FloatField{   "ozone_absorption_per_kilometer",    sky.OzoneAbsorptionPerKilometer, 3},
            FloatField{          "ozone_center_kilometers",         &sky.OzoneCenterKilometers, 1},
            FloatField{       "ozone_thickness_kilometers",      &sky.OzoneThicknessKilometers, 1},
            FloatField{       "sun_angular_radius_radians",       &sky.SunAngularRadiusRadians, 1},
            FloatField{              "sun_illuminance_lux",             &sky.SunIlluminanceLux, 1},
            FloatField{                    "ground_albedo",                   sky.GroundAlbedo, 3},
            FloatField{        "ground_ambient_irradiance",       &sky.GroundAmbientIrradiance, 1},
        };
    }

    inline auto SkySphere(Rendering::Scenes::SkySphereSettings& sky)
    {
        return Core::Containers::FixedArray{
            FloatField{                  "horizon_color",                 sky.HorizonColor, 4},
            FloatField{                   "zenith_color",                  sky.ZenithColor, 4},
            FloatField{                   "ground_color",                  sky.GroundColor, 4},
            FloatField{"sun_disc_angular_radius_radians", &sky.SunDiscAngularRadiusRadians, 1},
            FloatField{             "sun_disc_intensity",            &sky.SunDiscIntensity, 1},
            FloatField{              "horizon_sharpness",            &sky.HorizonSharpness, 1},
        };
    }
} // namespace ZEngine::ECS::SceneSettingsDetail
