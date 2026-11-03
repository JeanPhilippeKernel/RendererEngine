#include <ZEngine/ECS/SceneSettingsYAML.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <string_view>

namespace
{
    namespace ECS                                              = ZEngine::ECS;
    namespace Scenes                                           = ZEngine::Rendering::Scenes;

    constexpr std::array<std::string_view, 6> PresentationKeys = {"sky_luminance_factor", "sky_diffuse_intensity", "sky_specular_intensity", "sky_lower_hemisphere_color", "aerial_perspective_start_depth_kilometers", "aerial_perspective_distance_scale"};

    Scenes::SkyConfig                         PresentationSky()
    {
        Scenes::SkyConfig value                     = {};
        value.EnvironmentIntensity                  = 3.5f;
        value.SkyLuminanceFactor                    = 1.5f;
        value.SkyDiffuseIntensity                   = .5f;
        value.SkySpecularIntensity                  = 2.5f;
        value.SkyLowerHemisphereColor[0]            = .25f;
        value.SkyLowerHemisphereColor[1]            = .5f;
        value.SkyLowerHemisphereColor[2]            = .75f;
        value.AerialPerspectiveStartDepthKilometers = .125f;
        value.AerialPerspectiveDistanceScale        = 4.f;
        return value;
    }

    class SceneSkyYAMLTest : public testing::Test
    {
    protected:
        ECS::SceneYAML::Storage Storage     = {};
        ECS::SceneDiagnostics   Diagnostics = {};

        void                    SetUp() override
        {
            ASSERT_TRUE(Storage.Initialize());
        }

        ECS::SceneYAML::Node Encode(const Scenes::SkyConfig& value)
        {
            auto node = Storage.Destination();
            EXPECT_TRUE(ECS::EncodeSceneSkyYAML(value, &node, &Diagnostics));
            return node;
        }
    };

    TEST_F(SceneSkyYAMLTest, PresentationControlsRoundTripExactlyInEverySkyMode)
    {
        for (const auto mode : {Scenes::SkyMode::Atmosphere, Scenes::SkyMode::HDRI, Scenes::SkyMode::SkySphere})
        {
            auto source     = PresentationSky();
            source.Mode     = mode;
            const auto node = Encode(source);
            ASSERT_EQ(node.size(), 14u);
            Scenes::SkyConfig decoded = {};
            ASSERT_TRUE(ECS::DecodeSceneSkyYAML(node, &decoded, &Diagnostics));
            EXPECT_EQ(decoded, source);
        }
    }

    TEST_F(SceneSkyYAMLTest, OlderScenesAndEveryPartialExtensionRetainDefaultsForAbsentControls)
    {
        const auto source  = PresentationSky();
        const auto encoded = Encode(source);
        for (uint32_t mask = 0; mask < 64; ++mask)
        {
            SCOPED_TRACE(mask);
            const auto node = Storage.Make(ECS::SceneYAML::NodeType::Map);
            for (const auto& field : encoded.Mapping())
            {
                const auto found = std::find(PresentationKeys.begin(), PresentationKeys.end(), field.Key);
                if (found == PresentationKeys.end() || (mask & (1u << (found - PresentationKeys.begin()))))
                    ASSERT_TRUE(Storage.Set(node, field.Key, field.Value));
            }
            Scenes::SkyConfig expected    = {};
            expected.EnvironmentIntensity = source.EnvironmentIntensity;
            if (mask & 1u)
                expected.SkyLuminanceFactor = source.SkyLuminanceFactor;
            if (mask & 2u)
                expected.SkyDiffuseIntensity = source.SkyDiffuseIntensity;
            if (mask & 4u)
                expected.SkySpecularIntensity = source.SkySpecularIntensity;
            if (mask & 8u)
                for (size_t i = 0; i < 3; ++i)
                    expected.SkyLowerHemisphereColor[i] = source.SkyLowerHemisphereColor[i];
            if (mask & 16u)
                expected.AerialPerspectiveStartDepthKilometers = source.AerialPerspectiveStartDepthKilometers;
            if (mask & 32u)
                expected.AerialPerspectiveDistanceScale = source.AerialPerspectiveDistanceScale;
            auto decoded = source;
            ASSERT_TRUE(ECS::DecodeSceneSkyYAML(node, &decoded, &Diagnostics));
            EXPECT_EQ(decoded, expected);
            // Saving an older scene writes a complete canonical representation.
            EXPECT_EQ(Encode(decoded).size(), 14u);
        }
    }

    TEST_F(SceneSkyYAMLTest, MalformedPresentControlsRejectWithoutChangingOutput)
    {
        const auto source = PresentationSky();
        for (const auto key : PresentationKeys)
        {
            SCOPED_TRACE(key);
            for (const auto invalid : {Storage.Make(ECS::SceneYAML::NodeType::Null), Storage.Scalar("not a number"), Storage.Float(-1.f), Storage.Scalar(".inf", true)})
            {
                const auto node = Encode(source);
                ASSERT_TRUE(Storage.Set(node, key, invalid));
                auto decoded = source;
                EXPECT_FALSE(ECS::DecodeSceneSkyYAML(node, &decoded, &Diagnostics));
                EXPECT_EQ(decoded, source);
            }
        }
        const auto node = Encode(source);
        ASSERT_TRUE(Storage.Set(node, "aerial_perspective_distance_scale", Storage.Float(0.f)));
        auto decoded = source;
        EXPECT_FALSE(ECS::DecodeSceneSkyYAML(node, &decoded, &Diagnostics));
        EXPECT_EQ(decoded, source);
        for (size_t count : {2u, 3u, 4u})
        {
            const auto color_node = Encode(source);
            const auto color      = Storage.Make(ECS::SceneYAML::NodeType::Sequence);
            for (size_t i = 0; i < count; ++i)
                ASSERT_TRUE(Storage.Append(color, Storage.Float(i == 1 ? -1.f : .5f)));
            ASSERT_TRUE(Storage.Set(color_node, "sky_lower_hemisphere_color", color));
            EXPECT_FALSE(ECS::DecodeSceneSkyYAML(color_node, &decoded, &Diagnostics));
            EXPECT_EQ(decoded, source);
        }
    }

    TEST_F(SceneSkyYAMLTest, UnknownKeysCannotReplaceOptionalControlsEvenAtLegacyFieldCount)
    {
        const auto source  = PresentationSky();
        const auto encoded = Encode(source);
        const auto node    = Storage.Make(ECS::SceneYAML::NodeType::Map);
        for (const auto& field : encoded.Mapping())
            if (std::find(PresentationKeys.begin(), PresentationKeys.end(), field.Key) == PresentationKeys.end() && field.Key != "environment_intensity")
                ASSERT_TRUE(Storage.Set(node, field.Key, field.Value));
        ASSERT_TRUE(Storage.Set(node, "unknown_control", Storage.Float(1.f)));
        ASSERT_EQ(node.size(), 8u);
        auto decoded = source;
        EXPECT_FALSE(ECS::DecodeSceneSkyYAML(node, &decoded, &Diagnostics));
        EXPECT_EQ(decoded, source);
    }
} // namespace
