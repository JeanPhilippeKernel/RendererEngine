#include <ZEngine/Rendering/Renderers/Compute/SkyAtmosphereViewPass.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Scenes/SkyConfigSerialization.h>
#include <gtest/gtest.h>
#include <limits>
#include <sstream>

using namespace ZEngine::Rendering::Scenes;

TEST(SkyAtmosphereViewPassTest, ComputeCallbacksDeclareTheirExactShaderPushConstantBlocks)
{
    ZEngine::Rendering::Renderers::SkyViewLutPass        sky_view = {};
    ZEngine::Rendering::Renderers::AerialPerspectivePass aerial   = {};

    EXPECT_EQ(sky_view.GetComputePushConstantSize(), sizeof(ZEngine::Rendering::Renderers::SkyViewPushConstants));
    EXPECT_EQ(aerial.GetComputePushConstantSize(), sizeof(ZEngine::Rendering::Renderers::AtmosphereViewPushConstants));
}

TEST(SkyConfigTest, DefaultConfigurationIsAtmosphereAndHasNoRuntimeReferences)
{
    const SkyConfig config = {};

    EXPECT_TRUE(config.IsValid());
    EXPECT_TRUE(config.IsAtmosphere());
    EXPECT_FALSE(config.IsHDRI());
    EXPECT_FALSE(config.IsSkySphere());
    EXPECT_TRUE(config.EnvironmentMap.is_nil());
    EXPECT_TRUE(config.PrimaryCelestialLight.is_nil());
    EXPECT_FLOAT_EQ(config.EnvironmentIntensity, 1.0f);
}

TEST(SkyConfigTest, DefaultAtmospherePlacesWorldOriginAtSeaLevel)
{
    const SkyConfig config     = {};
    const auto&     atmosphere = config.Atmosphere;

    EXPECT_FLOAT_EQ(atmosphere.PlanetCenterWorld[0], 0.0f);
    EXPECT_FLOAT_EQ(atmosphere.PlanetCenterWorld[1], -atmosphere.PlanetRadiusKilometers * atmosphere.WorldUnitsPerMeter * 1000.0f);
    EXPECT_FLOAT_EQ(atmosphere.PlanetCenterWorld[2], 0.0f);
    EXPECT_GT(atmosphere.GroundAlbedo[0], 0.0f);
    EXPECT_GT(atmosphere.GroundAlbedo[1], 0.0f);
    EXPECT_GT(atmosphere.GroundAlbedo[2], 0.0f);
    EXPECT_GT(atmosphere.GroundAmbientIrradiance, 0.0f);
}

TEST(SkyConfigTest, AtmosphereIlluminanceUsesTheDocumentedSceneRadianceScale)
{
    EXPECT_FLOAT_EQ(ConvertSunIlluminanceToSceneRadiance(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ConvertSunIlluminanceToSceneRadiance(ReferenceSunIlluminanceLux), 1.0f);
    EXPECT_FLOAT_EQ(ConvertSunIlluminanceToSceneRadiance(ReferenceSunIlluminanceLux * 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(ConvertSunIlluminanceToSceneRadiance(StandardNoonSunIlluminanceLux), 20.0f);
}

TEST(SkyConfigTest, MissingOptionalReferencesRemainAValidFallbackConfiguration)
{
    SkyConfig config = {};
    config.Mode      = SkyMode::HDRI;

    // A nil asset/light remains valid scene data. The runtime selects the
    // fallback snapshot rather than requiring a null-resource special case.
    EXPECT_TRUE(config.IsValid());
    EXPECT_TRUE(config.EnvironmentMap.is_nil());
    EXPECT_TRUE(config.PrimaryCelestialLight.is_nil());
}

TEST(SkyConfigTest, SanitizeReplacesMalformedDataWithSafeDefaults)
{
    SkyConfig config                             = {};
    config.Mode                                  = static_cast<SkyMode>(255);
    config.EnvironmentIntensity                  = std::numeric_limits<float>::infinity();
    config.Atmosphere.AtmosphereRadiusKilometers = config.Atmosphere.PlanetRadiusKilometers;

    EXPECT_FALSE(config.IsValid());
    config.Sanitize();

    EXPECT_TRUE(config.IsValid());
    EXPECT_TRUE(config.IsAtmosphere());
    EXPECT_FLOAT_EQ(config.EnvironmentIntensity, 1.0f);
    EXPECT_GT(config.Atmosphere.AtmosphereRadiusKilometers, config.Atmosphere.PlanetRadiusKilometers);
}

TEST(SkyConfigTest, BinaryCodecRoundTripsStableAuthoringData)
{
    SkyConfig input                          = {};
    input.Mode                               = SkyMode::SkySphere;
    input.EnvironmentIntensity               = 2.5f;
    input.EnvironmentTint[0]                 = 0.75f;
    input.EnvironmentYawRadians              = 1.25f;
    input.Atmosphere.MieAnisotropy           = 0.6f;
    input.Atmosphere.GroundAlbedo[1]         = 0.35f;
    input.Atmosphere.GroundAmbientIrradiance = 0.75f;
    input.Sphere.HorizonColor[2]             = 0.5f;
    input.Sphere.ShowSunDisc                 = false;
    input.EnvironmentMap                     = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440000").value();
    input.PrimaryCelestialLight              = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440001").value();

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    Serialization::WriteSkyConfig(stream, input);
    stream.seekg(0);

    SkyConfig output = {};
    ASSERT_TRUE(Serialization::ReadSkyConfig(stream, output));
    EXPECT_EQ(output.Mode, input.Mode);
    EXPECT_EQ(output.EnvironmentMap, input.EnvironmentMap);
    EXPECT_EQ(output.PrimaryCelestialLight, input.PrimaryCelestialLight);
    EXPECT_FLOAT_EQ(output.EnvironmentIntensity, input.EnvironmentIntensity);
    EXPECT_FLOAT_EQ(output.EnvironmentTint[0], input.EnvironmentTint[0]);
    EXPECT_FLOAT_EQ(output.EnvironmentYawRadians, input.EnvironmentYawRadians);
    EXPECT_FLOAT_EQ(output.Atmosphere.MieAnisotropy, input.Atmosphere.MieAnisotropy);
    EXPECT_FLOAT_EQ(output.Atmosphere.GroundAlbedo[1], input.Atmosphere.GroundAlbedo[1]);
    EXPECT_FLOAT_EQ(output.Atmosphere.GroundAmbientIrradiance, input.Atmosphere.GroundAmbientIrradiance);
    EXPECT_FLOAT_EQ(output.Sphere.HorizonColor[2], input.Sphere.HorizonColor[2]);
    EXPECT_FALSE(output.Sphere.ShowSunDisc);
}
