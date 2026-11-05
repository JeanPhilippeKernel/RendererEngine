#include <ZEngine/Core/Hash/SourceContentHash.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <iostream>
#include "CookResourceTestSupport.h"

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;

    namespace
    {
        using ResourceSink                    = Tests::VerifyingResourceSink;

        constexpr cstring MeshMetadata        = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"MeshImporter","settings":[]})";
        constexpr cstring EnvironmentMetadata = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"EnvironmentMapImporter","source_hash":0,"artifact_path":"/_cache/envmaps/sky.zenvmap","settings":[{"key":"artifact_version","value":"2"},{"key":"pixel_format","value":"rgba32f"},{"key":"color_space","value":"linear_scene"},{"key":"orientation","value":"renderer_canonical_v1"},{"key":"mip_policy","value":"generate_on_gpu"},{"key":"exposure","value":"1.0"}]})";
        constexpr cstring SkySource           = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n\x80\x80\x80\x81\x80\x80\x80\x81";
    } // namespace

    class SceneCookMaterialResourceProcessTest : public Tests::ResourceProcessTest
    {
    protected:
        Tests::GeneratedResourceCapture Capture  = {};
        Tests::GeneratedResourceCapture Expected = {};

        void                            SetUp() override
        {
            Limits.MaxWorkingBytes = ZMega(4);
            ResourceProcessTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            constexpr cstring metadata = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"MaterialImporter","settings":[]})";
            Adapter                    = CookMaterialAdapter();
            InitializeInput(CookAssetKind::Material, metadata, Capture, "assets/generated.zematerial");
        }

        void Configure(uint32_t name_bytes = 4096, uint64_t encoded_bytes = ZMega(1))
        {
            static_assert(Codec::MATERIAL_FILE_VERSION == 1);
            Capture.Reset();
            Expected.Reset();
            // Independent canonical wire oracle, not the production emitter.
            // Maximal decoded name uses six-byte control escapes. Legal trailing
            // JSON whitespace fills the actual 1 MiB encoded material ceiling.
            constexpr cstring prefix = R"({"albedo_color":[0,0,0,0],"alpha_cutoff":0,"alpha_mode":"LEGACY","ambient_color":[0,0,0,0],"emissive_color":[0,0,0,0],"factors":[0,0,0,0],"material_version":1,"name":")";
            constexpr cstring suffix = R"(","roughness_color":[0,0,0,0],"specular_color":[0,0,0,0],"textures":{"albedo":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"},"emissive":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"},"normal":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"},"opacity":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"},"specular":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"}},"uuid":"00000000-0000-0000-0000-000000000001"})"
                                       "\n";
            const auto        append = [&](cstring text, uint64_t bytes) {
                const Core::Containers::ArrayView<const uint8_t> pattern(reinterpret_cast<const uint8_t*>(text), Helpers::secure_strlen(text));
                Capture.Append(pattern, bytes);
                Expected.Append(pattern, bytes);
            };
            append(prefix, Helpers::secure_strlen(prefix));
            append("\\u000a", uint64_t(name_bytes) * 6);
            append(suffix, Helpers::secure_strlen(suffix));
            ASSERT_GE(encoded_bytes, Capture.Size());
            constexpr uint8_t space = ' ';
            Capture.Append({&space, 1}, encoded_bytes - Capture.Size());
            Capture.HashContents();
            Expected.HashContents();
            Capture.Reads = Capture.MaxReadEnd = 0;
        }

        VFS::VFSResult<void> Encode(Tests::VerifyingResourceSink& sink)
        {
            sink.Capture = &Expected;
            return Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, sink, Budget, nullptr);
        }
    };

    TEST_F(SceneCookMaterialResourceProcessTest, ActualEncodedAndNameCapsUseOneWorkspaceAndHonorExactWorkingPeaks)
    {
        ASSERT_EQ(Limits.MaxMaterialBytes, ZMega(1));
        ASSERT_EQ(sizeof(Codec::MaterialAssetDocument{}.Name) - 1, 4096u);
        Configure();
        ASSERT_FALSE(HasFatalFailure());
        ASSERT_EQ(Capture.Size(), Limits.MaxMaterialBytes);
        ASSERT_EQ(Expected.Size(), 25200u);
        Limits.MaxDecodedAssetBytes = sizeof(Codec::MaterialAssetDocument);
        for (const bool encoding : {false, true})
        {
            SCOPED_TRACE(encoding);
            ResetBudget(ZMega(4));
            CookAssetInspection          inspection  = {};
            Tests::VerifyingResourceSink first       = {};
            const auto                   allocations = Native.AllocationCount();
            ASSERT_TRUE((encoding ? Encode(first) : Inspect(inspection)).Succeeded());
            EXPECT_EQ(Native.AllocationCount(), allocations + 1);
            if (encoding)
            {
                EXPECT_EQ(first.Bytes, Expected.Size());
            }
            else
            {
                EXPECT_STREQ(inspection.Format(), "zematerial-v1-json");
                EXPECT_EQ(inspection.LocalContentKey(), Expected.Digest());
                EXPECT_TRUE(inspection.Dependencies().empty());
            }
            ExpectDrained();
            const auto peak = Budget.Statistics().PeakBytes;
            EXPECT_GT(peak, Limits.MaxMaterialBytes);
            EXPECT_LT(peak, Limits.MaxMaterialBytes + ZKilo(64));
            const auto previous = inspection.LocalContentKey();
            ResetBudget(peak - 1);
            Tests::VerifyingResourceSink refused = {};
            EXPECT_EQ((encoding ? Encode(refused) : Inspect(inspection)).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(refused.Calls, 0u);
            EXPECT_EQ(inspection.LocalContentKey(), previous);
            ExpectDrained();
            ResetBudget(peak);
            Tests::VerifyingResourceSink exact = {};
            ASSERT_TRUE((encoding ? Encode(exact) : Inspect(inspection)).Succeeded());
            EXPECT_EQ(exact.Bytes, first.Bytes);
            EXPECT_EQ(exact.Calls, first.Calls);
            EXPECT_EQ(Budget.Statistics().PeakBytes, peak);
            ExpectDrained();
            std::cout << "Encoded-cap material " << (encoding ? "encode" : "inspect") << ": " << Capture.Size() << " input bytes, " << Expected.Size() << " canonical bytes, " << peak << " modeled working bytes\n";
        }
    }

    TEST_F(SceneCookMaterialResourceProcessTest, SmallerCapsAndLateReadOrSyntaxFailuresPreserveInspectionAndDrainWorkspace)
    {
        Configure(1, ZKilo(2));
        ASSERT_FALSE(HasFatalFailure());
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Inspect(inspection).Succeeded());
        const auto previous = inspection.LocalContentKey();
        Configure();
        ASSERT_FALSE(HasFatalFailure());
        const auto reject = [&](VFS::VFSError error) {
            EXPECT_EQ(Inspect(inspection).Error(), error);
            EXPECT_EQ(inspection.LocalContentKey(), previous);
            EXPECT_STREQ(inspection.Format(), "zematerial-v1-json");
            Tests::VerifyingResourceSink sink = {};
            EXPECT_EQ(Encode(sink).Error(), error);
            EXPECT_EQ(sink.Calls, 0u);
            ExpectDrained();
        };
        const auto allocations = Native.AllocationCount();
        --Limits.MaxMaterialBytes;
        reject(VFS::VFSError::SizeLimitExceeded);
        ++Limits.MaxMaterialBytes;
        Limits.MaxEncodedAssetBytes = Capture.Size() - 1;
        reject(VFS::VFSError::SizeLimitExceeded);
        ++Limits.MaxEncodedAssetBytes;
        Limits.MaxDecodedAssetBytes = sizeof(Codec::MaterialAssetDocument) - 1;
        reject(VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Capture.Reads, 0u);
        EXPECT_EQ(Native.AllocationCount(), allocations);
        ++Limits.MaxDecodedAssetBytes;
        Capture.Chunk       = 997;
        Capture.PatchOffset = Capture.Size() - 4;
        Capture.Patch       = {'?', '?', '?', '?'};
        reject(VFS::VFSError::Corrupted);
        EXPECT_EQ(Capture.MaxReadEnd, Capture.Size());
        Capture.PatchOffset = UINT64_MAX;
        Capture.FailOffset  = Capture.Size() - 1;
        reject(VFS::VFSError::PermissionDenied);
        Configure(4097);
        ASSERT_FALSE(HasFatalFailure());
        reject(VFS::VFSError::SizeLimitExceeded);
    }

    class SceneCookEnvironmentResourceProcessTest : public Tests::ResourceProcessTest
    {
    protected:
        Tests::GeneratedResourceCapture Capture = {};
        Tests::GeneratedResourceCapture Source  = {};
        Codec::EnvironmentMapFileHeader Header  = {};

        void                            SetUp() override
        {
            Limits.MaxWorkingBytes = ZKilo(128);
            ResourceProcessTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            Adapter = CookEnvironmentAdapter();
            const Core::Containers::ArrayView<const uint8_t> source(reinterpret_cast<const uint8_t*>(SkySource), Helpers::secure_strlen(SkySource));
            Source.Append(source, source.size());
            Source.HashContents();
            InitializeInput(CookAssetKind::EnvironmentMap, EnvironmentMetadata, Capture, "textures/sky.hdr", &Source);
            Core::Hash::SourceContentHash receipt = {};
            ASSERT_TRUE(receipt.Update(source).Succeeded());
            ASSERT_TRUE(receipt.Finalize(&Metadata.Data.SourceHash).Succeeded());
        }

        void Configure(uint32_t face)
        {
            Capture.Reset();
            Header = {.MagicNumber = ZENVMAP_MAGIC, .Version = 2, .HeaderByteSize = sizeof(Header), .ImporterVersion = 1, .SourceHash = Metadata.Data.SourceHash, .FaceWidth = face, .FaceHeight = face, .Channel = 4, .LayerCount = 6, .MipCount = Codec::GetEnvironmentMapFullMipCount(face), .ColorSpace = 1, .Orientation = 1, .MipPolicy = 1, .Exposure = 1, .BufferByteSize = uint64_t(face) * face * 96};
            Capture.Append({reinterpret_cast<const uint8_t*>(&Header), sizeof(Header)}, sizeof(Header));
            constexpr float pixel[4] = {0.25f, 0.5f, 0.75f, 1.0f};
            Capture.Append({reinterpret_cast<const uint8_t*>(pixel), sizeof(pixel)}, Header.BufferByteSize);
        }
    };

    TEST_F(SceneCookEnvironmentResourceProcessTest, ActualMaximumCubeUsesOneStreamAndPassesAtExactWorkingPeak)
    {
        ASSERT_EQ(Codec::ENVIRONMENT_MAP_MAX_FACE_SIZE, 1024u);
        Configure(Codec::ENVIRONMENT_MAP_MAX_FACE_SIZE);
        ASSERT_EQ(Header.BufferByteSize, ZMega(96));
        Capture.HashContents();
        const auto          allocations = Native.AllocationCount();
        CookAssetInspection inspection  = {};
        ASSERT_TRUE(Inspect(inspection).Succeeded());
        EXPECT_STREQ(inspection.Format(), "zenvmap-v2-le64");
        EXPECT_EQ(Native.AllocationCount(), allocations + 1);
        ExpectDrained();
        ResourceSink first = {};
        ASSERT_TRUE(Encode(first).Succeeded());
        EXPECT_EQ(first.Bytes, Capture.Size());
        EXPECT_EQ(Native.AllocationCount(), allocations + 2);
        ExpectDrained();
        const auto peak = Budget.Statistics().PeakBytes;
        EXPECT_LT(peak, ZKilo(128));
        EXPECT_GT(peak, ZKilo(64));
        const auto previous = inspection.LocalContentKey();
        ResetBudget(peak - 1);
        EXPECT_EQ(Inspect(inspection).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(inspection.LocalContentKey(), previous);
        ResourceSink refused = {};
        EXPECT_EQ(Encode(refused).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(refused.Calls, 0u);
        ExpectDrained();
        ResetBudget(peak);
        ResourceSink exact = {};
        ASSERT_TRUE(Encode(exact).Succeeded());
        EXPECT_EQ(exact.Bytes, first.Bytes);
        EXPECT_EQ(exact.Calls, first.Calls);
        EXPECT_EQ(Budget.Statistics().PeakBytes, peak);
        ExpectDrained();
        std::cout << "Maximum environment: " << Header.BufferByteSize << " decoded bytes, " << peak << " modeled working bytes\n";
    }

    TEST_F(SceneCookEnvironmentResourceProcessTest, SmallerCapsStopAtTheHeaderAndLateFloatAndReadFailuresDrainTheStream)
    {
        Configure(1);
        Capture.HashContents();
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Inspect(inspection).Succeeded());
        const auto previous = inspection.LocalContentKey();
        Configure(1024);
        const auto reject = [&](VFS::VFSError error) {
            EXPECT_EQ(Inspect(inspection).Error(), error);
            EXPECT_EQ(inspection.LocalContentKey(), previous);
            EXPECT_STREQ(inspection.Format(), "zenvmap-v2-le64");
            ResourceSink sink = {};
            EXPECT_EQ(Encode(sink).Error(), error);
            EXPECT_EQ(sink.Calls, 0u);
            ExpectDrained();
        };
        Limits.MaxEncodedAssetBytes = Capture.Size() - 1;
        const auto allocations      = Native.AllocationCount();
        reject(VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Capture.Reads, 0u);
        EXPECT_EQ(Native.AllocationCount(), allocations);
        Limits.MaxEncodedAssetBytes = Capture.Size();
        Limits.MaxDecodedAssetBytes = Header.BufferByteSize - 1;
        reject(VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Capture.MaxReadEnd, sizeof(Header));
        ++Limits.MaxDecodedAssetBytes;
        Limits.MaxImageDimension = 1023;
        reject(VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Capture.MaxReadEnd, sizeof(Header));
        Limits.MaxImageDimension = 16384;
        Capture.PatchOffset      = Capture.Size() - sizeof(float);
        Capture.Patch            = {0, 0, 0xc0, 0x7f}; // Last payload scalar is NaN.
        reject(VFS::VFSError::Corrupted);
        EXPECT_EQ(Capture.MaxReadEnd, Capture.Size());
        Capture.PatchOffset = UINT64_MAX;
        Capture.FailOffset  = Capture.Size() - 1;
        reject(VFS::VFSError::PermissionDenied);
        Limits.MaxEncodedAssetBytes = ZMega(512);
        Limits.MaxDecodedAssetBytes = ZMega(512);
        Configure(1025);
        reject(VFS::VFSError::Corrupted); // Beyond the format's own face limit.
        EXPECT_EQ(Capture.MaxReadEnd, sizeof(Header));
    }

    class SceneCookMeshResourceProcessTest : public Tests::ResourceProcessTest
    {
    protected:
        Tests::GeneratedResourceCapture Capture     = {};
        uint64_t                        VertexBytes = 0;
        uint64_t                        IndexBytes  = 0;
        uint64_t                        IndexOffset = 0;
        size_t                          Workspace   = 0;
        uint32_t                        VertexCount = 0;
        uint32_t                        IndexCount  = 0;

        void                            SetUp() override
        {
            ResourceProcessTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            Adapter = CookMeshAdapter();
            InitializeInput(CookAssetKind::Mesh, MeshMetadata, Capture, "assets/generated.zemesh");
        }

        void Configure(uint32_t vertices, uint32_t indices)
        {
            VertexCount = vertices;
            IndexCount  = indices;
            VertexBytes = uint64_t(vertices) * 8 * sizeof(float);
            IndexBytes  = uint64_t(indices) * sizeof(uint32_t);
            Tests::ConfigureGeneratedMesh(Capture, vertices, indices, Metadata.Data.AssetUUID, uuids::uuid::from_string("00000000-0000-0000-0000-000000000003").value(), {}, &IndexOffset);
        }
        VFS::VFSResult<void> Measure(size_t* bytes)
        {
            Detail::CapturedByteSource source(Capture);
            Codec::MeshAssetReadLimits limits = {};
            limits.MaxFileBytes               = Limits.MaxEncodedAssetBytes;
            limits.MaxDecodedBytes            = static_cast<size_t>(Limits.MaxDecodedAssetBytes);
            return Codec::MeasureMeshAssetReadWorkspace(source.View(), bytes, Metadata.Data.AssetUUID, limits);
        }
        void ConfigureNearLimit()
        {
            Configure(3, 3);
            size_t baseline = 0;
            ASSERT_TRUE(Measure(&baseline).Succeeded());
            ASSERT_GE(baseline, VertexBytes + IndexBytes);
            const auto                       overhead      = baseline - VertexBytes - IndexBytes;
            const Codec::MeshAssetReadLimits limits        = {};
            const uint64_t                   vertex_bytes  = uint64_t(limits.MaxVertices) * 32;
            uint32_t                         indices       = static_cast<uint32_t>(std::min<uint64_t>(limits.MaxIndices, (Limits.MaxDecodedAssetBytes - vertex_bytes - overhead) / 4));
            indices                                       -= indices % 3;
            // Different index counts can change the next array's alignment.
            // At most three triangle decrements cover that bounded padding.
            for (uint32_t attempt = 0; attempt < 4; ++attempt, indices -= 3)
            {
                Configure(limits.MaxVertices, indices);
                if (Measure(&Workspace).Succeeded())
                {
                    break;
                }
            }
            ASSERT_GT(Workspace, Limits.MaxDecodedAssetBytes - 64);
            ASSERT_LE(Workspace, Limits.MaxDecodedAssetBytes);
            ASSERT_LE(Capture.Size(), Limits.MaxEncodedAssetBytes);
            ASSERT_EQ(VertexCount, limits.MaxVertices);
        }
    };

    TEST_F(SceneCookMeshResourceProcessTest, NearDecodedCapReusesOneWorkspaceAndPassesExactDecodedAndWorkingLimits)
    {
        ConfigureNearLimit();
        Capture.HashContents();
        Limits.MaxDecodedAssetBytes     = Workspace;
        CookAssetInspection inspection  = {};
        const auto          allocations = Native.AllocationCount();
        ASSERT_TRUE(Inspect(inspection).Succeeded());
        EXPECT_EQ(inspection.LocalContentKey(), Capture.Digest());
        EXPECT_TRUE(inspection.Dependencies().empty());
        EXPECT_EQ(Native.AllocationCount(), allocations + 1);
        ExpectDrained();
        ResourceSink first = {};
        ASSERT_TRUE(Encode(first).Succeeded());
        EXPECT_EQ(first.Bytes, Capture.Size());
        EXPECT_EQ(Native.AllocationCount(), allocations + 2);
        ExpectDrained();
        const auto peak = Budget.Statistics().PeakBytes;
        EXPECT_LT(peak, ZMega(513));
        --Limits.MaxDecodedAssetBytes;
        ResourceSink refused = {};
        EXPECT_EQ(Inspect(inspection).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(inspection.LocalContentKey(), Capture.Digest());
        EXPECT_EQ(Encode(refused).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(refused.Calls, 0u);
        EXPECT_EQ(Native.AllocationCount(), allocations + 2);
        ++Limits.MaxDecodedAssetBytes;
        const auto footprint = Budget.AllocationFootprint(Workspace, DEFAULT_ALIGNMENT, CookMemoryCategory::Codecs);
        ASSERT_TRUE(footprint.Succeeded());
        const uint64_t mapping_peak = Idle + footprint.Value();
        EXPECT_GE(peak, mapping_peak + Codec::MeshAssetReadFixedWorkingBytes());
        ResetBudget(mapping_peak - 1);
        EXPECT_EQ(Inspect(inspection).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(inspection.LocalContentKey(), Capture.Digest());
        EXPECT_EQ(Encode(refused).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(refused.Calls, 0u);
        EXPECT_EQ(Native.AllocationCount(), allocations + 2);
        ExpectDrained();
        // The fixed reader reservation remains live across the mapping. A
        // one-byte-short complete peak now rejects before mapping as well.
        ResetBudget(peak - 1);
        EXPECT_EQ(Encode(refused).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(refused.Calls, 0u);
        EXPECT_EQ(Native.AllocationCount(), allocations + 2);
        ExpectDrained();
        ResetBudget(peak);
        ResourceSink exact = {};
        ASSERT_TRUE(Encode(exact).Succeeded());
        EXPECT_EQ(exact.Bytes, first.Bytes);
        EXPECT_EQ(exact.Calls, first.Calls);
        EXPECT_EQ(Budget.Statistics().PeakBytes, peak);
        ExpectDrained();
        std::cout << "Near-limit mesh: " << Capture.Size() << " encoded bytes, " << Workspace << " decoded workspace bytes, " << peak << " modeled working bytes\n";
    }

    TEST_F(SceneCookMeshResourceProcessTest, OverCountAndEncodedLimitsRejectBeforeMappingAndLateIndexAndReadFailuresDrain)
    {
        Configure(3, 3);
        Capture.HashContents();
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Inspect(inspection).Succeeded());
        const auto previous = inspection.LocalContentKey();
        const auto reject   = [&](VFS::VFSError error) {
            EXPECT_EQ(Inspect(inspection).Error(), error);
            EXPECT_EQ(inspection.LocalContentKey(), previous);
            EXPECT_STREQ(inspection.Format(), "zemesh-v1-le64");
            ResourceSink sink = {};
            EXPECT_EQ(Encode(sink).Error(), error);
            EXPECT_EQ(sink.Calls, 0u);
            ExpectDrained();
        };
        const Codec::MeshAssetReadLimits limits      = {};
        const auto                       allocations = Native.AllocationCount();
        Configure(limits.MaxVertices + 1, 3);
        reject(VFS::VFSError::SizeLimitExceeded);
        Configure(3, limits.MaxIndices + 2);
        reject(VFS::VFSError::SizeLimitExceeded);
        ConfigureNearLimit();
        Limits.MaxEncodedAssetBytes = Capture.Size() - 1;
        Capture.Reads               = 0;
        reject(VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Capture.Reads, 0u);
        EXPECT_EQ(Native.AllocationCount(), allocations);
        Limits.MaxEncodedAssetBytes = Capture.Size();
        Capture.PatchOffset         = IndexOffset + IndexBytes - sizeof(uint32_t);
        Capture.Patch               = {0xff, 0xff, 0xff, 0xff};
        reject(VFS::VFSError::Corrupted);
        EXPECT_EQ(Native.AllocationCount(), allocations + 2);
        Capture.PatchOffset = UINT64_MAX;
        Capture.FailOffset  = IndexOffset + IndexBytes - 1;
        reject(VFS::VFSError::PermissionDenied);
    }

    TEST(GeneratedResourceCaptureTest, RangeBoundariesShortReadsAndPatchesMatchIndependentBytes)
    {
        Tests::GeneratedResourceCapture source     = {};
        constexpr uint8_t               first[]    = {1, 2, 3};
        constexpr uint8_t               second[]   = {4, 5};
        constexpr uint8_t               expected[] = {1, 2, 3, 1, 2, 4, 5, 4, 5, 4, 5, 4};
        source.Append(first, 5);
        source.Append(second, 7);
        for (size_t chunk : {size_t{1}, size_t{3}, SIZE_MAX})
        {
            source.Chunk = chunk;
            for (size_t offset = 0; offset <= sizeof(expected); ++offset)
            {
                for (size_t count = 0; count <= sizeof(expected) - offset; ++count)
                {
                    Core::Containers::FixedArray<uint8_t, sizeof(expected)> bytes = {};
                    ASSERT_TRUE(ReadCookCapturedRange(source, offset, {bytes.data(), count}).Succeeded());
                    EXPECT_TRUE(std::equal(bytes.data(), bytes.data() + count, expected + offset));
                }
            }
        }
        source.PatchOffset                                            = 4;
        source.Patch                                                  = {20, 21, 22, 23};
        Core::Containers::FixedArray<uint8_t, sizeof(expected)> bytes = {};
        ASSERT_TRUE(ReadCookCapturedRange(source, 0, bytes).Succeeded());
        EXPECT_EQ(bytes[3], expected[3]);
        EXPECT_EQ(bytes[4], 20u);
        EXPECT_EQ(bytes[7], 23u);
        EXPECT_EQ(bytes[8], expected[8]);
        source.FailOffset = 11;
        EXPECT_EQ(ReadCookCapturedRange(source, 0, bytes).Error(), VFS::VFSError::PermissionDenied);
    }
} // namespace ZEngine::Cook
