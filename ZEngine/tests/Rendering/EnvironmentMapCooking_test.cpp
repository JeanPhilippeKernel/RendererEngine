#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/EnvironmentMapImporter.h>
#include <ZEngine/Rendering/Buffers/Bitmap.h>
#include <ZEngine/ZEngineDef.h>
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

using namespace ZEngine::Importers;
using namespace ZEngine::Importers::AssetCodec;
using namespace ZEngine::Rendering::Buffers;

namespace
{
    EnvironmentMapFileHeader MakeValidHeader(uint32_t face_size = 2, uint64_t source_hash = 0xAABBCCDDULL)
    {
        return {
            .MagicNumber     = ZENVMAP_MAGIC,
            .Version         = ENVIRONMENT_MAP_FILE_VERSION,
            .HeaderByteSize  = sizeof(EnvironmentMapFileHeader),
            .ImporterVersion = ENVIRONMENT_MAP_IMPORTER_VERSION,
            .SourceHash      = source_hash,
            .FaceWidth       = face_size,
            .FaceHeight      = face_size,
            .Channel         = 4,
            .LayerCount      = 6,
            .MipCount        = GetEnvironmentMapFullMipCount(face_size),
            .ColorSpace      = static_cast<uint32_t>(EnvironmentMapColorSpace::LinearScene),
            .Orientation     = static_cast<uint32_t>(EnvironmentMapOrientation::RendererCanonical),
            .MipPolicy       = static_cast<uint32_t>(EnvironmentMapMipPolicy::GenerateOnGpu),
            .Exposure        = 1.0f,
            .BufferByteSize  = static_cast<uint64_t>(face_size) * face_size * 6 * 4 * sizeof(float),
        };
    }
} // namespace

TEST(EnvironmentMapCookingTest, ImporterClaimsOnlySupportedHdrSources)
{
    EnvironmentMapImporter importer = {};

    EXPECT_TRUE(importer.CanImport("hdr"));
    EXPECT_FALSE(importer.CanImport("exr"));
    EXPECT_FALSE(importer.CanImport("png"));
    EXPECT_FALSE(importer.CanImport(nullptr));
}

TEST(EnvironmentMapCookingTest, SourceValidationRejectsMalformedAndOversizedInputs)
{
    std::array<float, 4 * 2 * 4> pixels = {};
    pixels.fill(1.0f);

    EXPECT_TRUE(EnvironmentMapImporter::IsSupportedEquirectangularSource(4, 2, pixels.data()));
    EXPECT_FALSE(EnvironmentMapImporter::IsSupportedEquirectangularSource(6, 2, pixels.data()));
    EXPECT_FALSE(EnvironmentMapImporter::IsSupportedEquirectangularSource(4, 2, nullptr));

    pixels[0] = -0.5f;
    EXPECT_FALSE(EnvironmentMapImporter::IsSupportedEquirectangularSource(4, 2, pixels.data()));
    pixels[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(EnvironmentMapImporter::IsSupportedEquirectangularSource(4, 2, pixels.data()));

    float placeholder = 1.0f;
    EXPECT_FALSE(EnvironmentMapImporter::IsSupportedEquirectangularSource(static_cast<int>(AssetCodec::ENVIRONMENT_MAP_MAX_FACE_SIZE * 4 + 4), static_cast<int>(AssetCodec::ENVIRONMENT_MAP_MAX_FACE_SIZE * 2 + 2), &placeholder));
}

TEST(EnvironmentMapCookingTest, ArtifactPathIsStableAndCacheOnly)
{
    const uuids::uuid asset_uuid         = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440000").value();
    char              artifact_path[256] = {};

    ASSERT_TRUE(EnvironmentMapImporter::BuildArtifactPath(asset_uuid, artifact_path, sizeof(artifact_path)));
    EXPECT_STREQ(artifact_path, "/_cache/envmaps/550e8400-e29b-41d4-a716-446655440000.zenvmap");
    EXPECT_FALSE(EnvironmentMapImporter::BuildArtifactPath({}, artifact_path, sizeof(artifact_path)));
}

TEST(EnvironmentMapCookingTest, HeaderValidationRejectsWrongContractsAndStaleSources)
{
    EnvironmentMapFileHeader header = MakeValidHeader();
    ASSERT_TRUE(IsEnvironmentMapFileHeaderValid(header));
    EXPECT_TRUE(DoesEnvironmentMapHeaderMatchSource(header, 0xAABBCCDDULL));
    EXPECT_FALSE(DoesEnvironmentMapHeaderMatchSource(header, 0xDDCCBBAAULL));

    header.Orientation = 0;
    EXPECT_FALSE(IsEnvironmentMapFileHeaderValid(header));
    header = MakeValidHeader();
    ++header.BufferByteSize;
    EXPECT_FALSE(IsEnvironmentMapFileHeaderValid(header));
}

TEST(EnvironmentMapCookingTest, DeserializerRejectsTruncatedCookedArtifact)
{
    const std::filesystem::path    artifact_path = std::filesystem::temp_directory_path() / "zengine_environment_map_cooking_test.zenvmap";
    const EnvironmentMapFileHeader header        = MakeValidHeader();
    const std::vector<float>       payload(static_cast<size_t>(header.BufferByteSize) / sizeof(float), 0.25f);

    {
        std::ofstream output(artifact_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(header.BufferByteSize));
    }

    Bitmap decoded = {};
    ASSERT_TRUE(DeserializeEnvironmentMapFile(artifact_path.c_str(), decoded));
    EXPECT_EQ(decoded.Type, BitmapType::CubeMap);
    EXPECT_EQ(decoded.Width, 2);
    EXPECT_EQ(decoded.Layers, 6);

    {
        std::ofstream output(artifact_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }

    Bitmap corrupt = {};
    EXPECT_FALSE(DeserializeEnvironmentMapFile(artifact_path.c_str(), corrupt));
    std::error_code error;
    std::filesystem::remove(artifact_path, error);
}

TEST(EnvironmentMapCookingTest, DirectConversionPreservesTheCanonicalFaceOrientation)
{
    constexpr int                         width  = 8;
    constexpr int                         height = 4;
    std::array<float, width * height * 4> pixels = {};
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const size_t index = static_cast<size_t>(y * width + x) * 4;
            pixels[index + 0]  = static_cast<float>(x + y * width);
            pixels[index + 1]  = static_cast<float>(x);
            pixels[index + 2]  = static_cast<float>(y);
            pixels[index + 3]  = 1.0f;
        }
    }

    Bitmap equirectangular = Bitmap::FromData(width, height, 1, 4, BitmapFormat::Float, BitmapType::Texture2D, pixels.data());
    Bitmap cross           = BitmapConvert::EquirectToCross(equirectangular);
    Bitmap expected        = BitmapConvert::CrossToCubemap(cross);
    Bitmap actual          = BitmapConvert::EquirectToCubemap(equirectangular);

    ASSERT_EQ(actual.Type, BitmapType::CubeMap);
    ASSERT_EQ(actual.BufferSize, expected.BufferSize);
    const auto* const expected_pixels = reinterpret_cast<const float*>(expected.Buffer);
    const auto* const actual_pixels   = reinterpret_cast<const float*>(actual.Buffer);
    for (size_t index = 0; index < actual.BufferSize / sizeof(float); ++index)
        EXPECT_FLOAT_EQ(actual_pixels[index], expected_pixels[index]) << "component " << index;
}
