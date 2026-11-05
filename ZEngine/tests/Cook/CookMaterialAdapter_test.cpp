#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;

    namespace
    {
        struct MaterialCapture final : ICookCapturedFile
        {
            std::string      Bytes          = {};
            size_t           Chunk          = SIZE_MAX;
            uint64_t         AdvertisedSize = UINT64_MAX;
            mutable uint32_t Reads          = 0;
            VFS::VFSError    Failure        = VFS::VFSError::OK;

            uint64_t         Size() const noexcept override
            {
                return AdvertisedSize == UINT64_MAX ? Bytes.size() : AdvertisedSize;
            }
            Digest256 Digest() const noexcept override
            {
                return {};
            }
            VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const override
            {
                ++Reads;
                if (Failure != VFS::VFSError::OK)
                {
                    return VFS::VFSResult<size_t>::Fail(Failure);
                }
                const size_t size = offset >= Bytes.size() ? 0 : std::min({output.size(), Bytes.size() - static_cast<size_t>(offset), Chunk});
                if (size)
                {
                    Helpers::secure_memcpy(output.data(), output.size(), Bytes.data() + offset, size);
                }
                return VFS::VFSResult<size_t>::Ok(size);
            }
        };

        struct MaterialAdapterSink final : ICookByteSink
        {
            std::string          Bytes   = {};
            uint32_t             Calls   = 0;
            uint32_t             FailAt  = UINT32_MAX;
            VFS::VFSError        Failure = VFS::VFSError::PermissionDenied;

            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                if (++Calls == FailAt)
                {
                    return VFS::VFSResult<void>::Fail(Failure);
                }
                Bytes.append(reinterpret_cast<cstring>(bytes.data()), bytes.size());
                return VFS::VFSResult<void>::Ok();
            }

            static VFS::VFSResult<void> Emit(void* context, Core::Containers::ArrayView<const uint8_t> bytes)
            {
                return static_cast<MaterialAdapterSink*>(context)->Write(bytes);
            }
        };

        uuids::uuid MaterialUUID(cstring text = "00000000-0000-0000-0000-000000000001")
        {
            return uuids::uuid::from_string(text).value();
        }

        Core::Containers::ArrayView<const uint8_t> AdapterBytes(const std::string& text)
        {
            return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
        }

        void ReplaceMaterialText(std::string& text, const std::string& before, const std::string& after)
        {
            const auto offset = text.find(before);
            ASSERT_NE(offset, std::string::npos);
            text.replace(offset, before.size(), after);
        }

        struct FailingAdapterAllocator final : ICookAllocator
        {
            ICookAllocator* Parent       = nullptr;
            uint32_t        Calls        = 0;
            uint32_t        FailAt       = 0;
            uint32_t        FixedCalls   = 0;
            uint32_t        FixedFailAt  = 0;
            VFS::VFSError   FixedFailure = VFS::VFSError::Unsupported;
            struct FixedRequest
            {
                uint64_t Bytes      = 0;
                uint64_t LiveBefore = 0;
            };
            Core::Containers::FixedArray<FixedRequest, 8> FixedRequests   = {};
            Core::Containers::FixedArray<size_t, 4>       AllocationBytes = {};
            VFS::VFSResult<void>                          TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                if (FixedCalls < FixedRequests.size())
                {
                    FixedRequests[FixedCalls] = {bytes, Parent->Statistics().LiveBytes};
                }
                return ++FixedCalls == FixedFailAt ? VFS::VFSResult<void>::Fail(FixedFailure) : Parent->TryCharge(bytes, category, output);
            }
            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                if (Calls < AllocationBytes.size())
                {
                    AllocationBytes[Calls] = bytes;
                }
                return ++Calls == FailAt ? VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory) : Parent->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent->Statistics();
            }
        };
    } // namespace

    class CookMaterialAdapterTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(64), 1> Parent      = {};
        CookScratchAllocator                Scratch     = {};
        MaterialCapture                     Capture     = {};
        VFS::StrictMetaFileData             Metadata    = {};
        CookLimits                          Limits      = {.MaxWorkingBytes = ZKilo(128)};
        CookAssetInput                      Input       = {};
        CookAdapterFns                      Adapter     = CookMaterialAdapter();
        CookDiagnostics                     Diagnostics = {};

        void                                SetUp() override
        {
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Scratch.Initialize(Parent, ZKilo(64), CookMemoryCategory::Codecs).Succeeded());
            constexpr cstring metadata = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"MaterialImporter","settings":[]})";
            ASSERT_TRUE(VFS::MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(metadata), Helpers::secure_strlen(metadata)}, &Metadata).Succeeded());
            Input = {
                {CookAssetKind::Material, MaterialUUID()},
                &Capture, nullptr, &Metadata, &Limits, VFS::VFSRelativePath::Parse("assets/material.zematerial").Value()
            };
            SetTextureCount(1);
        }

        void TearDown() override
        {
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Scratch.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            EXPECT_EQ(Parent.Frees, 1u);
            EXPECT_EQ(Parent.Statistics().LiveBytes, 0u);
        }

        void SetTextureCount(size_t count)
        {
            Codec::MaterialAssetDocument document                       = {};
            document.UUID                                               = MaterialUUID();
            document.Name[0]                                            = 'M';
            document.Factors[2]                                         = .5f;
            Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {};
            for (size_t i = 0; i < count; ++i)
            {
                document.Textures[i].UUID = MaterialUUID("00000000-0000-0000-0000-000000000002");
                Helpers::secure_memcpy(document.Textures[i].Path, sizeof(document.Textures[i].Path), "assets/tex.png", sizeof("assets/tex.png"));
                paths[i] = VFS::VFSRelativePath::Parse("assets/tex.png").Value();
            }
            MaterialAdapterSink sink = {};
            ASSERT_TRUE(Codec::WriteCanonicalMaterialAsset(document, paths, MaterialAdapterSink::Emit, &sink).Succeeded());
            Capture.Bytes = std::move(sink.Bytes);
        }

        CookResolvedDependency Resolved(cstring slot = "albedo")
        {
            return {
                slot, {CookAssetKind::Texture, MaterialUUID("00000000-0000-0000-0000-000000000002")},
                 {},
                 "image-png", VFS::VFSRelativePath::Parse("objects/1111111111111111111111111111111111111111111111111111111111111111.png").Value(), Digest256::ParseHex("1111111111111111111111111111111111111111111111111111111111111111").Value(), 70
            };
        }
    };

    TEST_F(CookMaterialAdapterTest, InspectionRetainsOnlyOneExactDependencyBlockAndAcceptsShortCaptureReads)
    {
        Capture.Chunk                  = 7;
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, &Diagnostics).Succeeded());
        EXPECT_GT(Capture.Reads, 1u);
        EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
        ASSERT_EQ(inspection.Dependencies().size(), 1u);
        EXPECT_EQ(inspection.Dependencies()[0].Identity, Resolved().Identity);
        EXPECT_EQ(inspection.Dependencies()[0].Slot, Core::Containers::StringView("albedo"));
        EXPECT_EQ(inspection.Dependencies()[0].SourcePath, Core::Containers::StringView("assets/tex.png"));
        const auto original_local    = inspection.LocalContentKey();
        const auto original_metadata = inspection.MetadataKey();
        ReplaceMaterialText(Capture.Bytes, "assets/tex.png", "/Assets/tex.png");
        CookAssetInspection moved = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &moved, &Diagnostics).Succeeded());
        EXPECT_EQ(moved.LocalContentKey(), original_local);
        EXPECT_EQ(moved.MetadataKey(), original_metadata);
        EXPECT_EQ(moved.Dependencies()[0].SourcePath, Core::Containers::StringView("/Assets/tex.png"));
        EXPECT_EQ(inspection.Dependencies()[0].SourcePath, Core::Containers::StringView("assets/tex.png"));
        EXPECT_EQ(Diagnostics.ErrorCount, 0u);
    }

    TEST_F(CookMaterialAdapterTest, RepeatedInspectionAndEncodingReuseOneBackingAllocation)
    {
        const auto child = Resolved();
        for (uint32_t i = 0; i < 128; ++i)
        {
            CookAssetInspection inspection = {};
            ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
            MaterialAdapterSink sink = {};
            ASSERT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Succeeded());
            EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
            EXPECT_EQ(Parent.Allocations, 1u);
        }
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
    }

    TEST_F(CookMaterialAdapterTest, NilSlotsAreLeavesWithNoRetainedAllocation)
    {
        SetTextureCount(0);
        Limits.MaxEdges                = 0;
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        EXPECT_STREQ(inspection.Format(), "zematerial-v1-json");
        EXPECT_TRUE(inspection.Dependencies().empty());
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        MaterialAdapterSink sink = {};
        EXPECT_TRUE(Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Succeeded());
        EXPECT_EQ(sink.Bytes, Capture.Bytes);
    }

    TEST_F(CookMaterialAdapterTest, AllFiveSlotsCanShareOneChildWhileRetainingDistinctCanonicalEdges)
    {
        SetTextureCount(5);
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        ASSERT_EQ(inspection.Dependencies().size(), 5u);
        Core::Containers::FixedArray<CookResolvedDependency, 5> resolved = {};
        for (size_t i = 0; i < resolved.size(); ++i)
        {
            EXPECT_EQ(inspection.Dependencies()[i].Slot, Core::Containers::StringView(Codec::MaterialTextureSlotKey(static_cast<Codec::MaterialTextureSlot>(i))));
            resolved[i] = Resolved(Codec::MaterialTextureSlotKey(static_cast<Codec::MaterialTextureSlot>(i)));
        }
        MaterialAdapterSink sink = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, resolved, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Succeeded());
        Codec::MaterialAssetDocument cooked = {};
        ASSERT_TRUE(Codec::ReadMaterialAssetDocument(AdapterBytes(sink.Bytes), &cooked, Input.Identity.UUID).Succeeded());
        for (const auto& texture : cooked.Textures)
        {
            EXPECT_STREQ(texture.Path, resolved[0].ObjectPath.CStr());
        }
    }

    TEST_F(CookMaterialAdapterTest, EncodingUsesResolvedPackagePathsAndOmitsNativeProvenance)
    {
        ReplaceMaterialText(Capture.Bytes, "\"material_version\":1,", "\"material_version\":1,\"source_material\":{\"path\":\"/missing/raw.glb\",\"importer\":\"MeshImporter\",\"index\":2},");
        const auto          child = Resolved();
        MaterialAdapterSink sink  = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, &Diagnostics).Succeeded());
        EXPECT_EQ(sink.Bytes.find("source_material"), std::string::npos);
        EXPECT_EQ(sink.Bytes.find("assets/tex.png"), std::string::npos);
        ASSERT_FALSE(sink.Bytes.empty());
        EXPECT_EQ(sink.Bytes.back(), '\n');
        Codec::MaterialAssetDocument document = {};
        ASSERT_TRUE(Codec::ReadMaterialAssetDocument(AdapterBytes(sink.Bytes), &document, Input.Identity.UUID).Succeeded());
        EXPECT_STREQ(document.Textures[0].Path, child.ObjectPath.CStr());
        EXPECT_EQ(document.Textures[0].UUID, child.Identity.UUID);
        EXPECT_FALSE(document.HasSource);
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
    }

    TEST_F(CookMaterialAdapterTest, InvalidMaterialAndMetadataPreservePriorInspection)
    {
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        const auto* prior = inspection.Dependencies().data();
        const auto  bytes = Capture.Bytes;
        for (uint32_t failure = 0; failure < 7; ++failure)
        {
            Capture.Bytes = bytes;
            if (failure == 0)
            {
                ReplaceMaterialText(Capture.Bytes, "\"material_version\":1", "\"material_version\":2");
            }
            if (failure == 1)
            {
                ReplaceMaterialText(Capture.Bytes, "\"name\":\"M\"", "\"name\":\"M\",\"unknown\":1");
            }
            if (failure == 2)
            {
                ReplaceMaterialText(Capture.Bytes, "assets/tex.png", "assets/../tex.png");
            }
            if (failure == 3)
            {
                Metadata.Data.AssetUUID = {};
            }
            if (failure == 4)
            {
                Metadata.Data.SettingsCount = 1;
            }
            if (failure == 5)
            {
                Metadata.Fields = 0;
            }
            if (failure == 6)
            {
                Limits.MaxEdges = 0;
            }
            Diagnostics.Clear();
            EXPECT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, &Diagnostics).Failed()) << failure;
            EXPECT_EQ(inspection.Dependencies().data(), prior);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Discover);
            EXPECT_EQ(Diagnostics.Entries[0].Source, Input.Source);
            EXPECT_EQ(Diagnostics.Entries[0].Asset, Input.Identity);
            Metadata.Data.AssetUUID     = MaterialUUID();
            Metadata.Data.SettingsCount = 0;
            Metadata.Fields             = static_cast<uint32_t>(VFS::MetaFileField::UUID) | static_cast<uint32_t>(VFS::MetaFileField::Importer) | static_cast<uint32_t>(VFS::MetaFileField::Settings);
        }
    }

    TEST_F(CookMaterialAdapterTest, MetadataInspectionAndSemanticKeyRefusalsPreserveInspectionAndResourceDiagnostics)
    {
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        const auto* prior        = inspection.Dependencies().data();
        const auto  local_key    = inspection.LocalContentKey();
        const auto  metadata_key = inspection.MetadataKey();
        const auto  live         = Parent.Statistics().LiveBytes;
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            for (uint32_t phase : {1u, 2u, 3u})
            {
                SCOPED_TRACE(phase);
                FailingAdapterAllocator allocator = {};
                allocator.Parent                  = &Scratch;
                allocator.FixedFailAt             = phase;
                allocator.FixedFailure            = error;
                Diagnostics                       = {};
                Capture.Reads                     = 0;
                EXPECT_EQ(Adapter.Inspect(nullptr, Input, allocator, &inspection, &Diagnostics).Error(), error);
                EXPECT_EQ(allocator.FixedCalls, phase);
                EXPECT_EQ(allocator.Calls, phase == 1 ? 0u : 1u);
                if (phase == 1)
                {
                    EXPECT_EQ(Capture.Reads, 0u);
                }
                EXPECT_EQ(inspection.Dependencies().data(), prior);
                EXPECT_EQ(inspection.LocalContentKey(), local_key);
                EXPECT_EQ(inspection.MetadataKey(), metadata_key);
                EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
                EXPECT_EQ(Parent.Statistics().LiveBytes, live);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
            }
        }
    }

    TEST_F(CookMaterialAdapterTest, PrincipalInspectionAndOutputBudgetsFitExactlyAndRefuseOneByteShortWithoutNewBacking)
    {
        const auto child = Resolved();
        for (const bool encode : {false, true})
        {
            SCOPED_TRACE(encode);
            CookAssetInspection     inspection = {};
            MaterialAdapterSink     measured   = {};
            FailingAdapterAllocator probe      = {};
            probe.Parent                       = &Scratch;
            ASSERT_TRUE((encode ? Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, measured, probe, nullptr) : Adapter.Inspect(nullptr, Input, probe, &inspection, nullptr)).Succeeded());
            EXPECT_EQ(probe.AllocationBytes[0], Capture.Size() + sizeof(Codec::MaterialAssetReader) + DEFAULT_ALIGNMENT);
            EXPECT_EQ(probe.Calls, encode ? 1u : 2u);
            ASSERT_EQ(probe.FixedCalls, encode ? 2u : 3u);
            const uint64_t metadata = probe.FixedRequests[0].Bytes;
            const uint64_t own      = probe.FixedRequests[1].Bytes;
            const uint64_t required = encode ? own : own + probe.FixedRequests[2].Bytes;
            EXPECT_EQ(probe.FixedRequests[1].LiveBefore, probe.FixedRequests[0].LiveBefore);
            if (!encode)
            {
                EXPECT_EQ(probe.FixedRequests[2].LiveBefore, probe.FixedRequests[1].LiveBefore + own);
            }
            ASSERT_GT(required - 1, metadata);
            const auto local_key    = inspection.LocalContentKey();
            const auto metadata_key = inspection.MetadataKey();
            const auto live         = Parent.Statistics().LiveBytes;
            for (const auto available : {required - 1, required})
            {
                ASSERT_LE(live + available, Limits.MaxWorkingBytes);
                CookMemoryCharge blocker = {};
                ASSERT_TRUE(Parent.TryCharge(Limits.MaxWorkingBytes - live - available, CookMemoryCategory::Codecs, &blocker).Succeeded());
                FailingAdapterAllocator allocator = {};
                allocator.Parent                  = &Scratch;
                MaterialAdapterSink sink          = {};
                const auto*         prior         = inspection.Dependencies().data();
                const auto          result        = encode ? Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, allocator, &Diagnostics) : Adapter.Inspect(nullptr, Input, allocator, &inspection, &Diagnostics);
                EXPECT_EQ(result.Succeeded(), available == required);
                EXPECT_EQ(Parent.Statistics().LiveBytes, live + blocker.Bytes());
                EXPECT_EQ(Parent.Allocations, 1u);
                EXPECT_EQ(Scratch.OutstandingAllocations(), encode ? 0u : 1u);
                if (available < required)
                {
                    EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                    EXPECT_EQ(sink.Calls, 0u);
                    EXPECT_TRUE(sink.Bytes.empty());
                    EXPECT_EQ(inspection.Dependencies().data(), prior);
                }
                else if (encode)
                {
                    EXPECT_EQ(sink.Bytes, measured.Bytes);
                }
                EXPECT_EQ(inspection.LocalContentKey(), local_key);
                EXPECT_EQ(inspection.MetadataKey(), metadata_key);
            }
        }
    }

    TEST_F(CookMaterialAdapterTest, OutputWorkspaceRefusalDrainsTheOwnedReaderBeforeWritingAnyBytes)
    {
        const auto child = Resolved();
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            const auto              live      = Parent.Statistics().LiveBytes;
            FailingAdapterAllocator allocator = {};
            allocator.Parent                  = &Scratch;
            allocator.FixedFailAt             = 2;
            allocator.FixedFailure            = error;
            Diagnostics                       = {};
            MaterialAdapterSink sink          = {};
            sink.Bytes                        = "previous";
            EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, allocator, &Diagnostics).Error(), error);
            EXPECT_EQ(allocator.FixedCalls, 2u);
            EXPECT_EQ(allocator.Calls, 1u);
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(sink.Bytes, "previous");
            EXPECT_EQ(Parent.Statistics().LiveBytes, live);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
        }
    }

    TEST_F(CookMaterialAdapterTest, EveryInspectionAllocationFailureRollsBackWithoutFallback)
    {
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        const auto* prior = inspection.Dependencies().data();
        for (uint32_t fail_at = 1; fail_at <= 2; ++fail_at)
        {
            FailingAdapterAllocator allocator = {};
            allocator.Parent                  = &Scratch;
            allocator.FailAt                  = fail_at;
            EXPECT_EQ(Adapter.Inspect(nullptr, Input, allocator, &inspection, &Diagnostics).Error(), VFS::VFSError::OutOfMemory);
            EXPECT_EQ(inspection.Dependencies().data(), prior);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
        }
    }

    TEST_F(CookMaterialAdapterTest, EncodedLimitsAndTruncatedOrFailedCapturesReleaseAllScratch)
    {
        for (uint32_t failure = 0; failure < 7; ++failure)
        {
            Limits.MaxMaterialBytes     = ZMega(1);
            Limits.MaxEncodedAssetBytes = ZMega(512);
            Limits.MaxDecodedAssetBytes = ZMega(512);
            Capture.Failure             = VFS::VFSError::OK;
            Capture.AdvertisedSize      = UINT64_MAX;
            if (failure == 0)
            {
                Limits.MaxMaterialBytes = Capture.Bytes.size() - 1;
            }
            if (failure == 1)
            {
                Limits.MaxEncodedAssetBytes = Capture.Bytes.size() - 1;
            }
            if (failure == 2)
            {
                Capture.AdvertisedSize = Capture.Bytes.size() + 1;
            }
            if (failure == 3)
            {
                Capture.Failure = VFS::VFSError::PermissionDenied;
            }
            if (failure == 4)
            {
                Capture.AdvertisedSize = 0;
            }
            if (failure == 5)
            {
                Capture.AdvertisedSize = UINT64_MAX - 1;
            }
            if (failure == 6)
            {
                Limits.MaxDecodedAssetBytes = sizeof(Codec::MaterialAssetDocument) - 1;
            }
            CookAssetInspection inspection = {};
            EXPECT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Failed()) << failure;
            EXPECT_EQ(inspection.Format(), nullptr);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            MaterialAdapterSink sink  = {};
            const auto          child = Resolved();
            EXPECT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Failed());
            EXPECT_EQ(sink.Calls, 0u);
        }
    }

    TEST_F(CookMaterialAdapterTest, MissingExtraWrongOrCorruptResolvedSlotsFailBeforeFirstWrite)
    {
        for (uint32_t failure = 0; failure < 9; ++failure)
        {
            CookResolvedDependency children[] = {Resolved(), Resolved("normal")};
            size_t                 count      = failure == 0 ? 0 : failure == 1 ? 2 : 1;
            if (failure == 2)
            {
                children[0].Slot = "normal";
            }
            if (failure == 3)
            {
                children[0].Identity.Kind = CookAssetKind::Material;
            }
            if (failure == 4)
            {
                children[0].Identity.UUID = MaterialUUID();
            }
            if (failure == 5)
            {
                children[0].Format = "zemesh-v1-le64";
            }
            if (failure == 6)
            {
                children[0].ObjectDigest = {};
            }
            if (failure == 7)
            {
                children[0].ObjectSize = 0;
            }
            if (failure == 8)
            {
                children[0].ObjectPath = VFS::VFSRelativePath::Parse("assets/tex.png").Value();
            }
            MaterialAdapterSink sink = {};
            EXPECT_TRUE(Adapter.Encode(nullptr, Input, {children, count}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Failed()) << failure;
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        SetTextureCount(5);
        Core::Containers::FixedArray<CookResolvedDependency, 5> children = {};
        for (size_t i = 0; i < children.size(); ++i)
        {
            children[i] = Resolved(Codec::MaterialTextureSlotKey(static_cast<Codec::MaterialTextureSlot>(i)));
        }
        std::swap(children[0], children[1]);
        MaterialAdapterSink sink = {};
        EXPECT_TRUE(Adapter.Encode(nullptr, Input, children, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Failed());
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST_F(CookMaterialAdapterTest, EncodingPreservesSinkErrorsAndEnforcesOutputSize)
    {
        const auto          child = Resolved();
        MaterialAdapterSink good  = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, good, Scratch, nullptr).Succeeded());
        for (uint32_t fail_at : {1u, good.Calls / 2, good.Calls})
        {
            MaterialAdapterSink sink = {};
            sink.FailAt              = fail_at;
            EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, &Diagnostics).Error(), VFS::VFSError::PermissionDenied);
            EXPECT_EQ(sink.Calls, fail_at);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        ASSERT_GT(good.Bytes.size(), Capture.Bytes.size());
        Limits.MaxMaterialBytes     = good.Bytes.size() - 1;
        MaterialAdapterSink bounded = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, bounded, Scratch, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_LT(bounded.Bytes.size(), good.Bytes.size());
    }

    TEST_F(CookMaterialAdapterTest, EncodingPropagatesSinkFailuresAndReportsChildBudgetRefusal)
    {
        auto child = Resolved();
        for (auto error : {VFS::VFSError::Cancelled, VFS::VFSError::IOError, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            MaterialAdapterSink sink = {};
            sink.FailAt              = 1;
            sink.Failure             = error;
            EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Error(), error);
            EXPECT_EQ(sink.Calls, 1u);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        child.ObjectSize         = Limits.MaxEncodedAssetBytes + 1;
        MaterialAdapterSink sink = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST_F(CookMaterialAdapterTest, UnsupportedTargetAndIncompleteInputDoNotReadOrAllocate)
    {
        MaterialAdapterSink sink = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {}, CookTarget::Invalid, sink, Scratch, nullptr).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Adapter.Inspect(nullptr, Input, Scratch, nullptr, nullptr).Error(), VFS::VFSError::InvalidPath);
        const auto valid = Input;
        for (uint32_t failure = 0; failure < 7; ++failure)
        {
            Input = valid;
            if (failure == 0)
            {
                Input.Identity.Kind = CookAssetKind::Texture;
            }
            if (failure == 1)
            {
                Input.Identity.UUID = {};
            }
            if (failure == 2)
            {
                Input.Artifact = nullptr;
            }
            if (failure == 3)
            {
                Input.Metadata = nullptr;
            }
            if (failure == 4)
            {
                Input.Limits = nullptr;
            }
            if (failure == 5)
            {
                Input.EnvironmentSource = &Capture;
            }
            if (failure == 6)
            {
                Input.Source = {};
            }
            CookAssetInspection inspection = {};
            EXPECT_EQ(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Error(), VFS::VFSError::InvalidPath);
        }
        EXPECT_EQ(Capture.Reads, 0u);
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        EXPECT_EQ(sink.Calls, 0u);
    }
} // namespace ZEngine::Cook
