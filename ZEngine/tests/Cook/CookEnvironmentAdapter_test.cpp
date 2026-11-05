#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/Hash/SourceContentHash.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <gtest/gtest.h>
#include <rapidhash.h>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;

    namespace
    {
        struct EnvironmentCapture final : ICookCapturedFile
        {
            std::vector<uint8_t> Bytes       = {};
            size_t               Chunk       = SIZE_MAX;
            mutable uint32_t     Reads       = 0;
            uint32_t             FailAt      = UINT32_MAX;
            bool                 WrongDigest = false;
            bool                 Overreport  = false;
            uint64_t             Size() const noexcept override
            {
                return Bytes.size();
            }
            Digest256 Digest() const noexcept override
            {
                Core::Hash::SHA256 hash   = {};
                Digest256          digest = {};
                (void) hash.Update({Bytes.data(), Bytes.size()});
                (void) hash.Finalize(&digest);
                if (WrongDigest)
                    digest.Bytes[0] ^= 1;
                return digest;
            }
            VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> bytes) const override
            {
                if (++Reads == FailAt)
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::PermissionDenied);
                if (Overreport)
                    return VFS::VFSResult<size_t>::Ok(bytes.size() + 1);
                const size_t count = offset >= Bytes.size() ? 0 : std::min({bytes.size(), Bytes.size() - static_cast<size_t>(offset), Chunk});
                if (count)
                    Helpers::secure_memcpy(bytes.data(), bytes.size(), Bytes.data() + offset, count);
                return VFS::VFSResult<size_t>::Ok(count);
            }
        };
        struct EnvironmentSink final : ICookByteSink
        {
            std::vector<uint8_t> Bytes  = {};
            uint32_t             Calls  = 0;
            uint32_t             FailAt = UINT32_MAX;
            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                if (++Calls == FailAt)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::PermissionDenied);
                Bytes.insert(Bytes.end(), bytes.begin(), bytes.end());
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct EnvironmentFixedAllocator final : ICookAllocator
        {
            ICookAllocator*                           Parent    = nullptr;
            uint32_t                                  Calls     = 0;
            uint32_t                                  FailAt    = 0;
            uint64_t                                  LastBytes = 0;
            Core::Containers::FixedArray<uint64_t, 4> Requests  = {};
            VFS::VFSError                             Failure   = VFS::VFSError::SizeLimitExceeded;

            VFS::VFSResult<void*>                     Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return Parent->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent->Statistics();
            }
            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                LastBytes = bytes;
                if (Calls < Requests.size())
                    Requests[Calls] = bytes;
                return ++Calls == FailAt ? VFS::VFSResult<void>::Fail(Failure) : Parent->TryCharge(bytes, category, output);
            }
        };
    } // namespace

    class CookEnvironmentAdapterTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(128), 1> Parent   = {};
        CookScratchAllocator                 Scratch  = {};
        EnvironmentCapture                   Artifact = {};
        EnvironmentCapture                   Source   = {};
        Codec::EnvironmentMapFileHeader      Header   = {};
        VFS::StrictMetaFileData              Metadata = {};
        CookLimits                           Limits   = {.MaxWorkingBytes = ZKilo(256)};
        CookAssetInput                       Input    = {};
        CookAdapterFns                       Adapter  = CookEnvironmentAdapter();

        void                                 SetUp() override
        {
            // Fixed receipt/digest state is external to the unchanged slab.
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Scratch.Initialize(Parent, ZKilo(128), CookMemoryCategory::Codecs).Succeeded());
            Source.Bytes.resize(8193);
            for (size_t i = 0; i < Source.Bytes.size(); ++i)
                Source.Bytes[i] = static_cast<uint8_t>(i * 37);
            uint64_t hash = 0;
            for (size_t offset = 0; offset < Source.Bytes.size(); offset += 4096)
                hash = rapidhash_withSeed(Source.Bytes.data() + offset, std::min(size_t(4096), Source.Bytes.size() - offset), hash);
            const auto metadata = std::string(R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"EnvironmentMapImporter","source_hash":)") + std::to_string(hash) + R"(,"artifact_path":"/_cache/envmaps/sky.zenvmap","settings":[{"key":"exposure","value":"1.0"},{"key":"artifact_version","value":"2"},{"key":"pixel_format","value":"rgba32f"},{"key":"color_space","value":"linear_scene"},{"key":"orientation","value":"renderer_canonical_v1"},{"key":"mip_policy","value":"generate_on_gpu"}]})";
            ASSERT_TRUE(VFS::MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(metadata.data()), metadata.size()}, &Metadata).Succeeded());
            Header = {.MagicNumber = ZENVMAP_MAGIC, .Version = 2, .HeaderByteSize = sizeof(Header), .ImporterVersion = 1, .SourceHash = hash, .FaceWidth = 1, .FaceHeight = 1, .Channel = 4, .LayerCount = 6, .MipCount = 1, .ColorSpace = 1, .Orientation = 1, .MipPolicy = 1, .Exposure = 1, .BufferByteSize = 96};
            Bake();
            Input = {
                {CookAssetKind::EnvironmentMap, Metadata.Data.AssetUUID},
                &Artifact, &Source, &Metadata, &Limits, VFS::VFSRelativePath::Parse("textures/sky.hdr").Value()
            };
        }
        void TearDown() override
        {
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Scratch.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            EXPECT_EQ(Parent.Frees, 1u);
        }
        void Bake()
        {
            Artifact.Bytes.assign(sizeof(Header) + static_cast<size_t>(Header.BufferByteSize), 0);
            Helpers::secure_memcpy(Artifact.Bytes.data(), Artifact.Bytes.size(), &Header, sizeof(Header));
        }
        VFS::VFSResult<void> Inspect(CookAssetInspection& result)
        {
            return Adapter.Inspect(nullptr, Input, Scratch, &result, nullptr);
        }
        VFS::VFSResult<void> Encode(EnvironmentSink& sink)
        {
            return Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr);
        }
    };

    TEST_F(CookEnvironmentAdapterTest, LeafSemanticKeyAndExactPassthroughUseOnlyOneReusableStream)
    {
        Source.Chunk   = 7;
        Artifact.Chunk = 3;
        for (uint32_t i = 0; i < 128; ++i)
        {
            CookAssetInspection result = {};
            ASSERT_TRUE(Inspect(result).Succeeded());
            EXPECT_STREQ(result.Format(), "zenvmap-v2-le64");
            EXPECT_EQ(result.Dependencies().size(), 0u);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            CookFingerprint key = {};
            ASSERT_TRUE(key.String("ze.cook.environment.v1").Succeeded());
            ASSERT_TRUE(key.Digest(Source.Digest()).Succeeded());
            ASSERT_TRUE(key.Digest(Artifact.Digest()).Succeeded());
            Digest256 expected = {};
            ASSERT_TRUE(key.Finalize(&expected).Succeeded());
            EXPECT_EQ(result.LocalContentKey(), expected);
            EnvironmentSink sink = {};
            ASSERT_TRUE(Encode(sink).Succeeded());
            EXPECT_EQ(sink.Bytes, Artifact.Bytes);
        }
    }

    TEST_F(CookEnvironmentAdapterTest, SourceHashFixedBudgetRejectsOneByteShortAndAcceptsExactWithoutAnotherAllocation)
    {
        EnvironmentFixedAllocator allocator = {};
        allocator.Parent                    = &Scratch;
        CookAssetInspection result          = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, allocator, &result, nullptr).Succeeded());
        ASSERT_EQ(allocator.Calls, 3u); // Metadata, source hash, semantic key.
        const uint64_t bytes = allocator.Requests[1];
        EXPECT_GT(bytes, allocator.Requests[0]);
        EXPECT_GT(bytes, allocator.Requests[2]);
        EXPECT_EQ(bytes, sizeof(Core::Hash::SourceContentHash) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(uint64_t) + sizeof(CookMemoryCharge));
        const auto key      = result.LocalContentKey();
        const auto retained = Parent.Statistics().LiveBytes;
        for (uint64_t shortfall : {1u, 0u})
        {
            SCOPED_TRACE(shortfall);
            ASSERT_GE(Parent.Ledger.RemainingBytes(), bytes);
            CookMemoryCharge occupied = {};
            ASSERT_TRUE(Parent.TryCharge(Parent.Ledger.RemainingBytes() - bytes + shortfall, CookMemoryCategory::Codecs, &occupied).Succeeded());
            Source.Reads = Artifact.Reads = allocator.Calls = 0;
            const auto inspected                            = Adapter.Inspect(nullptr, Input, allocator, &result, nullptr);
            EXPECT_EQ(inspected.Error(), shortfall ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::OK);
            EXPECT_EQ(allocator.Calls, shortfall ? 2u : 3u);
            EXPECT_EQ(result.LocalContentKey(), key);
            if (shortfall)
            {
                EXPECT_EQ(Source.Reads, 0u);
                EXPECT_EQ(Artifact.Reads, 0u);
            }
            EXPECT_EQ(Parent.Statistics().LiveBytes, retained + occupied.Bytes());
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        EXPECT_EQ(Parent.Allocations, 1u);
        EXPECT_EQ(Parent.Statistics().LiveBytes, retained);
    }

    TEST_F(CookEnvironmentAdapterTest, MetadataAndSemanticKeyRefusalsPreserveLeafInspectionAndReportResources)
    {
        CookAssetInspection result = {};
        ASSERT_TRUE(Inspect(result).Succeeded());
        const auto local_key    = result.LocalContentKey();
        const auto metadata_key = result.MetadataKey();
        const auto live         = Parent.Statistics().LiveBytes;
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            for (uint32_t phase : {1u, 3u})
            {
                SCOPED_TRACE(phase);
                EnvironmentFixedAllocator allocator = {};
                allocator.Parent                    = &Scratch;
                allocator.FailAt                    = phase;
                allocator.Failure                   = error;
                CookDiagnostics diagnostics         = {};
                Source.Reads = Artifact.Reads = 0;
                EXPECT_EQ(Adapter.Inspect(nullptr, Input, allocator, &result, &diagnostics).Error(), error);
                EXPECT_EQ(allocator.Calls, phase);
                EXPECT_EQ(result.LocalContentKey(), local_key);
                EXPECT_EQ(result.MetadataKey(), metadata_key);
                EXPECT_EQ(Parent.Statistics().LiveBytes, live);
                EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
                if (phase == 1)
                {
                    EXPECT_EQ(Source.Reads, 0u);
                    EXPECT_EQ(Artifact.Reads, 0u);
                }
                ASSERT_EQ(diagnostics.Count, 1u);
                EXPECT_EQ(diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
            }
        }
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookEnvironmentAdapterTest, FixedHashRefusalsDrainTheStreamAndNeverWritePartialOutput)
    {
        const auto retained = Parent.Statistics().LiveBytes;
        for (auto error : {VFS::VFSError::SizeLimitExceeded, VFS::VFSError::Unsupported})
            for (uint32_t failure : {1u, 2u, 3u}) // Metadata, source hash, then copy.
            {
                SCOPED_TRACE(failure);
                EnvironmentFixedAllocator allocator = {};
                allocator.Parent                    = &Scratch;
                allocator.FailAt                    = failure;
                allocator.Failure                   = error;
                Source.Reads = Artifact.Reads = 0;
                EnvironmentSink sink          = {};
                EXPECT_EQ(Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, sink, allocator, nullptr).Error(), error);
                EXPECT_EQ(allocator.Calls, failure);
                EXPECT_EQ(sink.Calls, 0u);
                EXPECT_TRUE(sink.Bytes.empty());
                if (failure == 1)
                {
                    EXPECT_EQ(Source.Reads, 0u);
                    EXPECT_EQ(Artifact.Reads, 0u);
                }
                EXPECT_EQ(Parent.Statistics().LiveBytes, retained);
                EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            }
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookEnvironmentAdapterTest, UnsupportedHeadersAndEveryTruncatedArtifactFailBeforeOutput)
    {
        const auto          original = Artifact.Bytes;
        CookAssetInspection result   = {};
        ASSERT_TRUE(Inspect(result).Succeeded());
        const auto key = result.LocalContentKey();
        for (size_t size = 0; size < original.size(); ++size)
        {
            SCOPED_TRACE(size);
            Artifact.Bytes.assign(original.begin(), original.begin() + size);
            EXPECT_TRUE(Inspect(result).Failed());
            EXPECT_EQ(result.LocalContentKey(), key);
            EnvironmentSink sink = {};
            EXPECT_TRUE(Encode(sink).Failed());
            EXPECT_EQ(sink.Calls, 0u);
        }
        Artifact.Bytes = original;
        Header.Version = 3;
        Bake();
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::Unsupported);
        Header.Version         = 2;
        Header.ImporterVersion = 2;
        Bake();
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::Unsupported);
    }

    TEST_F(CookEnvironmentAdapterTest, InvalidHeaderFieldsNonfinitePayloadAndTrailingBytesAreRejected)
    {
        const auto                                   original  = Header;
        std::vector<Codec::EnvironmentMapFileHeader> invalid   = {};
        Header.MagicNumber                                    ^= 1;
        invalid.push_back(Header);
        Header                = original;
        Header.HeaderByteSize = 73;
        invalid.push_back(Header);
        Header             = original;
        Header.SourceHash ^= 1;
        invalid.push_back(Header);
        Header           = original;
        Header.FaceWidth = 0;
        invalid.push_back(Header);
        Header            = original;
        Header.FaceHeight = 2;
        invalid.push_back(Header);
        Header         = original;
        Header.Channel = 3;
        invalid.push_back(Header);
        Header            = original;
        Header.LayerCount = 5;
        invalid.push_back(Header);
        Header          = original;
        Header.MipCount = 2;
        invalid.push_back(Header);
        Header            = original;
        Header.ColorSpace = 0;
        invalid.push_back(Header);
        Header             = original;
        Header.Orientation = 0;
        invalid.push_back(Header);
        Header           = original;
        Header.MipPolicy = 0;
        invalid.push_back(Header);
        Header          = original;
        Header.Exposure = 0;
        invalid.push_back(Header);
        Header          = original;
        Header.Exposure = 2;
        invalid.push_back(Header);
        Header          = original;
        Header.Reserved = 1;
        invalid.push_back(Header);
        Header                = original;
        Header.BufferByteSize = 92;
        invalid.push_back(Header);
        Header = original;
        for (const auto& header : invalid)
        {
            Header = header;
            Bake();
            EnvironmentSink sink = {};
            EXPECT_TRUE(Encode(sink).Failed());
            EXPECT_EQ(sink.Calls, 0u);
        }
        Header = original;
        for (float value : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        {
            Bake();
            Helpers::secure_memcpy(Artifact.Bytes.data() + Artifact.Bytes.size() - 4, 4, &value, 4);
            EnvironmentSink sink = {};
            EXPECT_TRUE(Encode(sink).Failed());
            EXPECT_EQ(sink.Calls, 0u);
        }
        Bake();
        Artifact.Bytes.push_back(0);
        CookAssetInspection result = {};
        EXPECT_TRUE(Inspect(result).Failed());
    }

    TEST_F(CookEnvironmentAdapterTest, SourceHashAndCapturedDigestsMustAgreeAndSinkFailuresPropagate)
    {
        Source.Bytes[0]      ^= 1;
        EnvironmentSink sink  = {};
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(sink.Calls, 0u);
        Source.Bytes[0]    ^= 1;
        Source.WrongDigest  = true;
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(sink.Calls, 0u);
        Source.WrongDigest   = false;
        Artifact.WrongDigest = true;
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::Corrupted);
        Artifact.WrongDigest = false;
        sink                 = {};
        sink.FailAt          = 1;
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::PermissionDenied);
    }

    TEST_F(CookEnvironmentAdapterTest, ArtifactLargerThanScratchPoolValidatesAndStreamsWithoutPixelAllocation)
    {
        Header.FaceWidth = Header.FaceHeight = 64;
        Header.MipCount                      = Codec::GetEnvironmentMapFullMipCount(64);
        Header.BufferByteSize                = 64 * 64 * 6 * 4 * sizeof(float);
        Bake();
        ASSERT_GT(Artifact.Bytes.size(), ZKilo(128));
        CookAssetInspection result = {};
        ASSERT_TRUE(Inspect(result).Succeeded());
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        EnvironmentSink sink = {};
        ASSERT_TRUE(Encode(sink).Succeeded());
        EXPECT_EQ(sink.Bytes, Artifact.Bytes);
        EXPECT_GT(sink.Calls, 1u);
        sink        = {};
        sink.FailAt = 2;
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(sink.Bytes.size(), ZKilo(64));
    }

    TEST_F(CookEnvironmentAdapterTest, InvalidRequestsTargetsAndDependenciesDoNotReadCaptures)
    {
        CookAssetInspection result = {};
        auto                bad    = Input;
        bad.EnvironmentSource      = nullptr;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        bad               = Input;
        bad.Identity.Kind = CookAssetKind::Texture;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        bad          = Input;
        bad.Artifact = nullptr;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        bad        = Input;
        bad.Limits = nullptr;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        EXPECT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, nullptr, nullptr).Failed());
        EnvironmentSink sink = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {}, static_cast<CookTarget>(255), sink, Scratch, nullptr).Error(), VFS::VFSError::Unsupported);
        CookResolvedDependency child = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Source.Reads, 0u);
        EXPECT_EQ(Artifact.Reads, 0u);
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST_F(CookEnvironmentAdapterTest, MetadataPresenceSettingsAndOriginalArtifactPathsAreStrict)
    {
        const auto          original = Metadata;
        CookAssetInspection result   = {};
        for (auto field : {VFS::MetaFileField::SourceHash, VFS::MetaFileField::ArtifactPath})
        {
            Metadata         = original;
            Metadata.Fields &= ~static_cast<uint32_t>(field);
            EXPECT_TRUE(Inspect(result).Failed());
        }
        for (cstring exposure : {"0", "-1", "nan", "inf", "1e100", "1.0suffix", "2.0"})
        {
            Metadata = original;
            Helpers::secure_strcpy(Metadata.Data.Settings[0].Value, sizeof(Metadata.Data.Settings[0].Value), exposure);
            EXPECT_TRUE(Inspect(result).Failed());
        }
        for (cstring path : {"/_cache/../sky.zenvmap", "/tmp//sky.zenvmap", "sky.png", "C:/sky.zenvmap"})
        {
            Metadata = original;
            Helpers::secure_strcpy(Metadata.Data.ArtifactPath, sizeof(Metadata.Data.ArtifactPath), path);
            EXPECT_TRUE(Inspect(result).Failed());
        }
        Metadata                    = original;
        Metadata.Data.SettingsCount = 5;
        EXPECT_TRUE(Inspect(result).Failed());
    }

    TEST_F(CookEnvironmentAdapterTest, LimitsAndAllocationRefusalPreservePriorInspection)
    {
        CookAssetInspection result = {};
        ASSERT_TRUE(Inspect(result).Succeeded());
        const auto key              = result.LocalContentKey();
        Limits.MaxDecodedAssetBytes = 95;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxDecodedAssetBytes = 96;
        Limits.MaxImageDimension    = 0;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxImageDimension    = 1;
        Limits.MaxEncodedAssetBytes = Source.Bytes.size() - 1;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(result.LocalContentKey(), key);
        Limits.MaxEncodedAssetBytes                  = ZMega(512);
        Tests::FixedAllocator<ZKilo(64), 1> refusing = {};
        refusing.Failure                             = VFS::VFSError::OutOfMemory;
        EXPECT_EQ(Adapter.Inspect(nullptr, Input, refusing, &result, nullptr).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_EQ(result.LocalContentKey(), key);
    }

    TEST_F(CookEnvironmentAdapterTest, EverySourceAndArtifactReadFailurePropagates)
    {
        EnvironmentSink baseline = {};
        ASSERT_TRUE(Encode(baseline).Succeeded());
        const uint32_t      counts[]   = {Source.Reads, Artifact.Reads};
        EnvironmentCapture* captures[] = {&Source, &Artifact};
        for (size_t which = 0; which < 2; ++which)
            for (uint32_t failure = 1; failure <= counts[which]; ++failure)
            {
                Source.Reads = Artifact.Reads = 0;
                captures[which]->FailAt       = failure;
                EnvironmentSink sink          = {};
                EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::PermissionDenied);
                EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
                captures[which]->FailAt = UINT32_MAX;
            }
        Source.Overreport    = true;
        EnvironmentSink sink = {};
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::IOError);
        Source.Overreport = false;
        Artifact.Chunk    = 0;
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::Corrupted);
    }

    TEST_F(CookEnvironmentAdapterTest, SharedReaderRequiresSafeWorkspaceAndPreservesHeaderOnInvalidRequests)
    {
        Detail::CapturedByteSource      bridge(Artifact);
        auto                            source   = bridge.View();
        uint8_t                         bytes[8] = {};
        Codec::EnvironmentMapFileHeader output   = {};
        output.SourceHash                        = 123;
        EXPECT_EQ(Codec::ReadEnvironmentMapAsset(source, {}, &output, Header.SourceHash).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Codec::ReadEnvironmentMapAsset(source, {bytes, 3}, &output, Header.SourceHash).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Codec::ReadEnvironmentMapAsset(source, bytes, nullptr, Header.SourceHash).Error(), VFS::VFSError::InvalidPath);
        source.Read = nullptr;
        EXPECT_EQ(Codec::ReadEnvironmentMapAsset(source, bytes, &output, Header.SourceHash).Error(), VFS::VFSError::InvalidPath);
        source      = bridge.View();
        source.Size = UINT64_MAX;
        EXPECT_EQ(Codec::ReadEnvironmentMapAsset(source, bytes, &output, Header.SourceHash).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(output.SourceHash, 123u);
        EXPECT_EQ(Artifact.Reads, 0u);
        source = bridge.View();
        EXPECT_TRUE(Codec::ReadAssetByteRange(source, source.Size, {}).Succeeded());
        EXPECT_EQ(Codec::ReadAssetByteRange(source, source.Size + 1, {}).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Codec::ReadAssetByteRange(source, source.Size, {bytes, 1}).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Artifact.Reads, 0u);
    }

    TEST_F(CookEnvironmentAdapterTest, SharedVFSReaderPublishesHeaderOnlyAfterCheckedClose)
    {
        struct ClosingContext : VFS::VFSContext
        {
            bool                 Refuse = false;
            VFS::VFSResult<void> CloseChecked(VFS::IVFSFile* file) override
            {
                const auto closed = VFS::VFSContext::CloseChecked(file);
                return Refuse ? VFS::VFSResult<void>::Fail(VFS::VFSError::PermissionDenied) : closed;
            }
        };
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSMemoryBackend backend = {};
        ASSERT_TRUE(backend.InitializeOwned(ZMega(2), 16).Succeeded());
        ClosingContext context = {};
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        const auto path = VFS::VFSPath::Parse("/sky.zenvmap").Value();
        ASSERT_TRUE(VFS::WriteFileAtomically(context, path, {Artifact.Bytes.data(), Artifact.Bytes.size()}).Succeeded());
        const auto source_path = VFS::VFSPath::Parse("/source.hdr").Value();
        ASSERT_TRUE(VFS::WriteFileAtomically(context, source_path, {Source.Bytes.data(), Source.Bytes.size()}).Succeeded());
        const auto imported = VFS::MetaFileIO::ComputeHash(context, source_path);
        ASSERT_TRUE(imported.Succeeded());
        EXPECT_EQ(imported.Value(), Header.SourceHash);
        uint8_t                         stream[12] = {};
        Codec::EnvironmentMapFileHeader output     = {};
        ASSERT_TRUE(Codec::ReadEnvironmentMapFileVFS(context, path, stream, &output, Header.SourceHash).Succeeded());
        EXPECT_EQ(output.BufferByteSize, 96u);
        output.SourceHash = 123;
        context.Refuse    = true;
        EXPECT_EQ(VFS::MetaFileIO::ComputeHash(context, source_path).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Codec::ReadEnvironmentMapFileVFS(context, path, stream, &output, Header.SourceHash).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(output.SourceHash, 123u);
        context.Shutdown();
    }
} // namespace ZEngine::Cook
