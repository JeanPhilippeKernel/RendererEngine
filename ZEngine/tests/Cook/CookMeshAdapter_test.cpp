#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <gtest/gtest.h>
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
        struct MeshCapture final : ICookCapturedFile
        {
            std::string      Bytes          = {};
            size_t           Chunk          = SIZE_MAX;
            uint64_t         AdvertisedSize = UINT64_MAX;
            mutable uint32_t Reads          = 0;
            uint32_t         FailAt         = UINT32_MAX;
            bool             WrongDigest    = false;
            VFS::VFSError    ReadError      = VFS::VFSError::PermissionDenied;

            uint64_t         Size() const noexcept override
            {
                return AdvertisedSize == UINT64_MAX ? Bytes.size() : AdvertisedSize;
            }
            Digest256 Digest() const noexcept override
            {
                Core::Hash::SHA256 hash   = {};
                Digest256          digest = {};
                (void) hash.Update({reinterpret_cast<const uint8_t*>(Bytes.data()), Bytes.size()});
                (void) hash.Finalize(&digest);
                if (WrongDigest)
                {
                    digest.Bytes[0] ^= 1;
                }
                return digest;
            }
            VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const override
            {
                if (++Reads == FailAt)
                {
                    return VFS::VFSResult<size_t>::Fail(ReadError);
                }
                const size_t count = offset >= Bytes.size() ? 0 : std::min({output.size(), Bytes.size() - static_cast<size_t>(offset), Chunk});
                if (count)
                {
                    Helpers::secure_memcpy(output.data(), output.size(), Bytes.data() + offset, count);
                }
                return VFS::VFSResult<size_t>::Ok(count);
            }
        };

        struct MeshSink final : ICookByteSink
        {
            std::string          Bytes  = {};
            uint32_t             Calls  = 0;
            uint32_t             FailAt = UINT32_MAX;
            VFS::VFSError        Error  = VFS::VFSError::PermissionDenied;

            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                if (++Calls == FailAt)
                {
                    return VFS::VFSResult<void>::Fail(Error);
                }
                Bytes.append(reinterpret_cast<cstring>(bytes.data()), bytes.size());
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct MeshFaultAllocator final : ICookAllocator
        {
            ICookAllocator*       Parent            = nullptr;
            uint32_t              Calls             = 0;
            uint32_t              FailAt            = 0;
            size_t                FirstRequest      = 0;
            uint64_t              LargestCharge     = 0;
            uint64_t              RefuseChargeBytes = 0;
            VFS::VFSError         ChargeError       = VFS::VFSError::SizeLimitExceeded;
            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                if (!Calls)
                {
                    FirstRequest = bytes;
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
            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                LargestCharge = std::max(LargestCharge, bytes);
                if (RefuseChargeBytes && bytes == RefuseChargeBytes)
                {
                    return VFS::VFSResult<void>::Fail(ChargeError);
                }
                return Parent->TryCharge(bytes, category, output);
            }
        };

        uuids::uuid MeshUUID(cstring text = "00000000-0000-0000-0000-000000000001")
        {
            return uuids::uuid::from_string(text).value();
        }
    } // namespace

    class CookMeshAdapterTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager          Memory        = {};
        VFS::VFSMemoryBackend                Backend       = {};
        VFS::VFSContext                      Context       = {};
        Importers::AssetMesh                 Mesh          = {};
        Importers::AssetNodeHierarchy        Hierarchy     = {};
        Codec::ImportConfiguration           Configuration = {};
        Tests::FixedAllocator<ZKilo(256), 1> Parent        = {};
        CookScratchAllocator                 Scratch       = {};
        MeshCapture                          Capture       = {};
        VFS::StrictMetaFileData              Metadata      = {};
        CookLimits                           Limits        = {.MaxWorkingBytes = ZKilo(512)};
        CookAssetInput                       Input         = {};
        CookAdapterFns                       Adapter       = CookMeshAdapter();
        CookDiagnostics                      Diagnostics   = {};

        void                                 SetUp() override
        {
            Memory.Initialize(ZMega(4), {});
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(4), 32).Succeeded());
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            Configuration.VFS = &Context;
            Configuration.OutputAssetsPath.init(&Memory.MainArena, "/assets");
            Configuration.OutputAssetFile.init(&Memory.MainArena, "mesh.zemesh");
            constexpr cstring meta = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"MeshImporter","settings":[],"source_path":"/not-present/source.fbx"})";
            ASSERT_TRUE(VFS::MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(meta), Helpers::secure_strlen(meta)}, &Metadata).Succeeded());
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Scratch.Initialize(Parent, ZKilo(256), CookMemoryCategory::Codecs).Succeeded());
            Input = {
                {CookAssetKind::Mesh, MeshUUID()},
                &Capture, nullptr, &Metadata, &Limits, VFS::VFSRelativePath::Parse("assets/mesh.zemesh").Value()
            };
            Bake(1);
        }

        void TearDown() override
        {
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Scratch.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            EXPECT_EQ(Parent.Frees, 1u);
            EXPECT_EQ(Parent.Statistics().LiveBytes, 0u);
            Context.Shutdown();
        }

        void Bake(uint32_t count, bool nil_materials = false)
        {
            Mesh          = {};
            Hierarchy     = {};
            Mesh.MeshUUID = MeshUUID();
            Mesh.Vertices.init(&Memory.MainArena, 24, 24);
            Mesh.Vertices[8]  = 1;
            Mesh.Vertices[17] = 1;
            Mesh.Indices.init(&Memory.MainArena, size_t(count) * 3);
            Mesh.SubMeshes.init(&Memory.MainArena, count);
            for (uint32_t i = 0; i < count; ++i)
            {
                Mesh.Indices.push(0);
                Mesh.Indices.push(1);
                Mesh.Indices.push(2);
                Mesh.SubMeshes.push({.MaterialUUID = nil_materials ? uuids::uuid{} : MeshUUID("00000000-0000-0000-0000-000000000002"), .VertexCount = 3, .IndexCount = 3, .IndexOffset = i * 3, .IndexStreamOffset = i * 12, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
            }
            Hierarchy.NodeHierarchyUUID = MeshUUID("00000000-0000-0000-0000-000000000003");
            Hierarchy.MeshUUID          = Mesh.MeshUUID;
            Hierarchy.Hierarchies.init(&Memory.MainArena, 1);
            Hierarchy.LocalTransforms.init(&Memory.MainArena, 1);
            Hierarchy.GlobalTransforms.init(&Memory.MainArena, 1);
            Hierarchy.Names.init(&Memory.MainArena, 1);
            Hierarchy.MaterialNames.init(&Memory.MainArena, 0);
            Hierarchy.NodeNames.init(&Memory.MainArena, 16);
            Hierarchy.NodeMeshes.init(&Memory.MainArena, 16);
            Hierarchy.NodeMaterials.init(&Memory.MainArena, 16);
            ASSERT_EQ(Importers::AddNode(Hierarchy, -1, 0), 0);
            Hierarchy.Names.push_use({}).init(&Memory.MainArena, "Root");
            Hierarchy.NodeNames.insert(0, 0);
            Hierarchy.NodeMeshes.insert(0, 0);
            SaveCapture();
        }

        void SaveCapture()
        {
            ASSERT_TRUE(Codec::SerializeMeshAssetFile(&Memory.MainArena, Mesh, Hierarchy, Configuration).Succeeded());
            const auto opened = Context.Open(VFS::VFSPath::Parse("/assets/mesh.zemesh").Value(), VFS::VFSOpenFlags::Read);
            ASSERT_TRUE(opened.Succeeded());
            const auto size = opened.Value()->Size();
            ASSERT_TRUE(size.Succeeded());
            Capture.Bytes.resize(static_cast<size_t>(size.Value()));
            const auto read   = opened.Value()->ReadAll({reinterpret_cast<uint8_t*>(Capture.Bytes.data()), Capture.Bytes.size()});
            const auto closed = Context.CloseChecked(opened.Value());
            ASSERT_TRUE(read.Succeeded());
            ASSERT_EQ(read.Value(), Capture.Bytes.size());
            ASSERT_TRUE(closed.Succeeded());
        }

        CookResolvedDependency Resolved(Core::Containers::StringView slot = "submeshes/0/material")
        {
            return {
                slot, {CookAssetKind::Material, MeshUUID("00000000-0000-0000-0000-000000000002")},
                 {},
                 "zematerial-v1-json", VFS::VFSRelativePath::Parse("objects/1111111111111111111111111111111111111111111111111111111111111111.zematerial").Value(), Digest256::ParseHex("1111111111111111111111111111111111111111111111111111111111111111").Value(), 500
            };
        }

        template <typename T>
        void SetBytes(size_t offset, const T& value)
        {
            ASSERT_LE(offset + sizeof(T), Capture.Bytes.size());
            Helpers::secure_memcpy(Capture.Bytes.data() + offset, Capture.Bytes.size() - offset, &value, sizeof(T));
        }

        size_t IndicesCount() const
        {
            return 24 + 8 + Mesh.Vertices.size() * sizeof(float);
        }
        size_t SubMeshesCount() const
        {
            return IndicesCount() + 8 + Mesh.Indices.size() * sizeof(uint32_t);
        }
        size_t NodesCount() const
        {
            return SubMeshesCount() + 8 + Mesh.SubMeshes.size() * sizeof(Importers::AssetSubMesh) + 32;
        }
        size_t LocalsCount() const
        {
            return NodesCount() + 8 + Hierarchy.Hierarchies.size() * sizeof(Helpers::NodeHierarchy);
        }
        size_t NamesCount() const
        {
            return LocalsCount() + 8 + Hierarchy.LocalTransforms.size() * sizeof(Core::Maths::Mat4f) + 8 + Hierarchy.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f);
        }
    };

    TEST_F(CookMeshAdapterTest, InspectionReturnsOnlyTypedMaterialDependenciesAndCapturedSemanticKeys)
    {
        Capture.Chunk                  = 7;
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, &Diagnostics).Succeeded());
        ASSERT_EQ(inspection.Dependencies().size(), 1u);
        EXPECT_STREQ(inspection.Format(), "zemesh-v1-le64");
        EXPECT_EQ(inspection.Dependencies()[0].Identity, Resolved().Identity);
        EXPECT_EQ(inspection.Dependencies()[0].Slot, Core::Containers::StringView("submeshes/0/material"));
        EXPECT_EQ(inspection.Dependencies()[0].SourcePath.size(), 0u);
        EXPECT_EQ(inspection.LocalContentKey(), Capture.Digest());
        EXPECT_GT(Capture.Reads, 1u);
        EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
        EXPECT_EQ(Diagnostics.ErrorCount, 0u);
        const auto local_key = inspection.LocalContentKey();
        SetBytes(32, .25f);
        CookAssetInspection changed = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &changed, nullptr).Succeeded());
        EXPECT_NE(changed.LocalContentKey(), local_key);
        EXPECT_EQ(changed.MetadataKey(), inspection.MetadataKey());
        const auto child = Resolved();
        MeshSink   sink  = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Succeeded());
        EXPECT_EQ(sink.Bytes, Capture.Bytes);
    }

    TEST_F(CookMeshAdapterTest, SlotsUseCanonicalLexicalOrderAndSharedChildrenRemainDistinctEdges)
    {
        Bake(12);
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        ASSERT_EQ(inspection.Dependencies().size(), 12u);
        constexpr cstring                   expected[] = {"submeshes/0/material", "submeshes/1/material", "submeshes/10/material", "submeshes/11/material", "submeshes/2/material", "submeshes/3/material", "submeshes/4/material", "submeshes/5/material", "submeshes/6/material", "submeshes/7/material", "submeshes/8/material", "submeshes/9/material"};
        std::vector<CookResolvedDependency> children   = {};
        for (size_t i = 0; i < inspection.Dependencies().size(); ++i)
        {
            EXPECT_EQ(inspection.Dependencies()[i].Slot, Core::Containers::StringView(expected[i]));
            EXPECT_EQ(inspection.Dependencies()[i].Identity, Resolved().Identity);
            children.push_back(Resolved(inspection.Dependencies()[i].Slot));
        }
        MeshSink sink = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, children, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Succeeded());
        EXPECT_EQ(sink.Bytes, Capture.Bytes);
    }

    TEST_F(CookMeshAdapterTest, SparseSlotsKeepOriginalSubmeshIndicesAndRejectRepeatedOrReorderedEdges)
    {
        Bake(12);
        for (size_t index : {size_t(0), size_t(2), size_t(5), size_t(10)})
        {
            Mesh.SubMeshes[index].MaterialUUID = {};
        }
        SaveCapture();
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        ASSERT_EQ(inspection.Dependencies().size(), 8u);
        EXPECT_EQ(inspection.Dependencies()[0].Slot, Core::Containers::StringView("submeshes/1/material"));
        EXPECT_EQ(inspection.Dependencies()[1].Slot, Core::Containers::StringView("submeshes/11/material"));
        std::vector<CookResolvedDependency> children = {};
        for (const auto& dependency : inspection.Dependencies())
        {
            children.push_back(Resolved(dependency.Slot));
        }
        MeshSink valid = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, children, CookTarget::DesktopVulkanLE64, valid, Scratch, nullptr).Succeeded());
        for (uint32_t malformed = 0; malformed < 3; ++malformed)
        {
            auto changed = children;
            if (malformed == 0)
            {
                std::swap(changed[0], changed[1]);
            }
            if (malformed == 1)
            {
                changed[1] = changed[0];
            }
            if (malformed == 2)
            {
                changed[0].Slot = "submeshes/0/material";
            }
            MeshSink sink = {};
            EXPECT_TRUE(Adapter.Encode(nullptr, Input, changed, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Failed());
            EXPECT_EQ(sink.Calls, 0u);
        }
    }

    class CookMeshSlotOrderTest : public CookMeshAdapterTest, public testing::WithParamInterface<uint32_t>
    {
    };

    TEST_P(CookMeshSlotOrderTest, DenseAndSparseMaterialsMatchIndependentLexicalOrderAndPassthrough)
    {
        Limits.MaxWorkingBytes   = ZMega(16);
        CookPageAllocator native = {};
        ASSERT_TRUE(native.Initialize(Limits.MaxWorkingBytes).Succeeded());
        const auto idle = native.Statistics().LiveBytes;
        for (bool sparse : {false, true})
        {
            SCOPED_TRACE(sparse);
            const uint32_t count = GetParam();
            Bake(count);
            ASSERT_FALSE(HasFatalFailure());
            std::vector<std::pair<std::string, CookAssetIdentity>> expected = {};
            for (uint32_t i = 0; i < count; ++i)
            {
                auto& material = Mesh.SubMeshes[i].MaterialUUID;
                material       = i % 2 ? MeshUUID("00000000-0000-0000-0000-000000000002") : MeshUUID("00000000-0000-0000-0000-000000000003");
                // Missing parents must not hide their non-nil decimal children.
                if (sparse && (i % 3 == 0 || i == count / 2))
                {
                    material = {};
                }
                if (!material.is_nil())
                {
                    expected.emplace_back("submeshes/" + std::to_string(i) + "/material", CookAssetIdentity{CookAssetKind::Material, material});
                }
            }
            SaveCapture();
            ASSERT_FALSE(HasFatalFailure());
            std::sort(expected.begin(), expected.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
            Limits.MaxEdges               = static_cast<uint32_t>(expected.size());
            MeshFaultAllocator recorder   = {};
            recorder.Parent               = &native;
            CookAssetInspection inspected = {};
            ASSERT_TRUE(Adapter.Inspect(nullptr, Input, recorder, &inspected, nullptr).Succeeded());
            ASSERT_EQ(inspected.Dependencies().size(), expected.size());
            // No decimal traversal table, sort allocation or per-edge backing.
            EXPECT_EQ(recorder.Calls, expected.empty() ? 1u : 3u);
            EXPECT_EQ(native.OutstandingAllocations(), expected.empty() ? 0u : 1u);
            std::vector<CookResolvedDependency> children = {};
            for (size_t i = 0; i < expected.size(); ++i)
            {
                const auto& dependency = inspected.Dependencies()[i];
                EXPECT_EQ(dependency.Slot, Core::Containers::StringView(expected[i].first.c_str(), expected[i].first.size()));
                EXPECT_EQ(dependency.Identity, expected[i].second);
                auto child     = Resolved(dependency.Slot);
                child.Identity = dependency.Identity;
                children.push_back(child);
            }
            MeshSink sink = {};
            ASSERT_TRUE(Adapter.Encode(nullptr, Input, children, CookTarget::DesktopVulkanLE64, sink, recorder, nullptr).Succeeded());
            EXPECT_EQ(sink.Bytes, Capture.Bytes);
            EXPECT_EQ(recorder.Calls, expected.empty() ? 2u : 4u);
            if (!expected.empty())
            {
                const auto* previous = inspected.Dependencies().data();
                const auto  live     = native.Statistics().LiveBytes;
                --Limits.MaxEdges;
                EXPECT_EQ(Adapter.Inspect(nullptr, Input, recorder, &inspected, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(inspected.Dependencies().data(), previous);
                EXPECT_EQ(native.Statistics().LiveBytes, live);
                MeshSink rejected = {};
                EXPECT_EQ(Adapter.Encode(nullptr, Input, children, CookTarget::DesktopVulkanLE64, rejected, recorder, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(rejected.Calls, 0u);
            }
            inspected.Reset();
            EXPECT_EQ(native.OutstandingAllocations(), 0u);
            EXPECT_EQ(native.Statistics().LiveBytes, idle);
        }
    }

    INSTANTIATE_TEST_SUITE_P(DecimalBoundaries, CookMeshSlotOrderTest, testing::Values(1u, 2u, 9u, 10u, 11u, 19u, 20u, 99u, 100u, 101u, 109u, 110u, 999u, 1000u, 1001u, Codec::MeshAssetReadLimits{}.MaxSubMeshes));

    TEST_F(CookMeshAdapterTest, DecoderUsesMeasuredCapacityAndEncodingNeedsNoAdditionalStreamAllocation)
    {
        Detail::CapturedByteSource bridge(Capture);
        size_t                     capacity = 0;
        ASSERT_TRUE(Codec::MeasureMeshAssetReadWorkspace(bridge.View(), &capacity, Input.Identity.UUID).Succeeded());
        Limits.MaxDecodedAssetBytes    = capacity;
        MeshFaultAllocator recorder    = {};
        recorder.Parent                = &Scratch;
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, recorder, &inspection, nullptr).Succeeded());
        EXPECT_EQ(recorder.FirstRequest, capacity);
        EXPECT_EQ(recorder.Calls, 3u); // Decode, temporary sorted edges, compact result.
        recorder.Calls   = 0;
        MeshSink   sink  = {};
        const auto child = Resolved();
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, recorder, nullptr).Succeeded());
        EXPECT_EQ(recorder.FirstRequest, capacity);
        EXPECT_EQ(recorder.Calls, 1u); // Same decoded arena becomes the stream.
        EXPECT_EQ(sink.Bytes, Capture.Bytes);
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookMeshAdapterTest, ReaderFixedReservationRefusalsPrecedeCaptureReadsAndAllocationsAndPreservePriorInspection)
    {
        const uint64_t reader_bytes = Codec::MeshAssetReadFixedWorkingBytes();
        EXPECT_GE(reader_bytes, ZKilo(16) + sizeof(Importers::AssetMesh) + sizeof(Importers::AssetNodeHierarchy));
        EXPECT_LT(reader_bytes, ZKilo(32));
        const uint64_t      charged_bytes = reader_bytes + sizeof(Codec::MeshAssetReadLimits) + sizeof(Detail::CapturedByteSource) + sizeof(Codec::AssetByteSource) + sizeof(CookMemoryCharge);
        CookAssetInspection inspection    = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        const auto* previous      = inspection.Dependencies().data();
        const auto  previous_key  = inspection.LocalContentKey();
        const auto  previous_live = Parent.Statistics().LiveBytes;
        const auto  child         = Resolved();
        for (auto error : {VFS::VFSError::SizeLimitExceeded, VFS::VFSError::OutOfMemory, VFS::VFSError::Unsupported, VFS::VFSError::PermissionDenied, VFS::VFSError::Cancelled})
        {
            MeshFaultAllocator allocator = {};
            allocator.Parent             = &Scratch;
            allocator.RefuseChargeBytes  = charged_bytes;
            allocator.ChargeError        = error;
            Capture.Reads                = 0;
            Diagnostics.Clear();
            EXPECT_EQ(Adapter.Inspect(nullptr, Input, allocator, &inspection, &Diagnostics).Error(), error);
            EXPECT_EQ(inspection.Dependencies().data(), previous);
            EXPECT_EQ(inspection.LocalContentKey(), previous_key);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Discover);
            if (error == VFS::VFSError::Unsupported)
            {
                EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedBackend);
            }
            Diagnostics.Clear();
            MeshSink sink = {};
            EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, allocator, &Diagnostics).Error(), error);
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(Capture.Reads, 0u);
            EXPECT_EQ(allocator.Calls, 0u);
            EXPECT_EQ(allocator.LargestCharge, charged_bytes);
            EXPECT_EQ(Parent.Statistics().LiveBytes, previous_live);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Encode);
            if (error == VFS::VFSError::Unsupported)
            {
                EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedBackend);
            }
        }
    }

    TEST_F(CookMeshAdapterTest, ReaderFixedReservationDrainsAcrossSuccessReadAndAllocationFailuresWithoutExtraBacking)
    {
        Bake(1, true);
        const auto     idle         = Parent.Statistics().LiveBytes;
        const uint64_t reader_bytes = Codec::MeshAssetReadFixedWorkingBytes() + sizeof(Codec::MeshAssetReadLimits) + sizeof(Detail::CapturedByteSource) + sizeof(Codec::AssetByteSource) + sizeof(CookMemoryCharge);
        for (uint32_t failure = 0; failure < 3; ++failure)
        {
            MeshFaultAllocator allocator   = {};
            allocator.Parent               = &Scratch;
            allocator.FailAt               = failure == 2 ? 1 : 0;
            Capture.Reads                  = 0;
            Capture.FailAt                 = failure == 1 ? 1 : UINT32_MAX;
            CookAssetInspection inspection = {};
            const auto          status     = Adapter.Inspect(nullptr, Input, allocator, &inspection, nullptr);
            if (!failure)
            {
                ASSERT_TRUE(status.Succeeded());
            }
            else
            {
                EXPECT_EQ(status.Error(), failure == 1 ? VFS::VFSError::PermissionDenied : VFS::VFSError::OutOfMemory);
            }
            EXPECT_EQ(allocator.LargestCharge, reader_bytes);
            EXPECT_EQ(Parent.Statistics().LiveBytes, idle);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            EXPECT_EQ(Parent.Allocations, 1u);
        }
    }

    TEST_F(CookMeshAdapterTest, NilMaterialFallbacksCreateNoEdgesOrRetainedInspectionAllocation)
    {
        Bake(3, true);
        Limits.MaxEdges                = 0;
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        EXPECT_TRUE(inspection.Dependencies().empty());
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        MeshSink sink = {};
        EXPECT_TRUE(Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Succeeded());
        EXPECT_EQ(sink.Bytes, Capture.Bytes);
    }

    TEST_F(CookMeshAdapterTest, RepeatedValidationAndPassthroughUseOneBackingAllocationWithoutLiveVFS)
    {
        Context.Shutdown();
        const auto child = Resolved();
        for (uint32_t i = 0; i < 128; ++i)
        {
            CookAssetInspection inspection = {};
            ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
            MeshSink sink = {};
            ASSERT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Succeeded());
            EXPECT_EQ(sink.Bytes, Capture.Bytes);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
            EXPECT_EQ(Parent.Allocations, 1u);
        }
    }

    TEST_F(CookMeshAdapterTest, MalformedPayloadsPreservePriorInspectionAndFailEncodingBeforeFirstWrite)
    {
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        const auto* prior    = inspection.Dependencies().data();
        const auto  original = Capture.Bytes;
        const auto  child    = Resolved();
        for (uint32_t malformed = 0; malformed < 12; ++malformed)
        {
            Capture.Bytes = original;
            if (malformed == 0)
            {
                SetBytes(4, uint32_t(ASSET_FILE_VERSION + 1));
            }
            if (malformed == 1)
            {
                SetBytes(32, std::numeric_limits<float>::quiet_NaN());
            }
            if (malformed == 2)
            {
                SetBytes(IndicesCount() + 8, uint32_t(3));
            }
            if (malformed == 3)
            {
                SetBytes(NodesCount() + 8, int32_t(0));
            }
            if (malformed == 4)
            {
                SetBytes(LocalsCount() + 8, std::numeric_limits<float>::infinity());
            }
            if (malformed == 5)
            {
                SetBytes(NamesCount() + 4 + 8, uint8_t(0xc0));
            }
            if (malformed == 6)
            {
                SetBytes(8, MeshUUID("00000000-0000-0000-0000-000000000099"));
            }
            if (malformed == 7)
            {
                Capture.Bytes.push_back('x');
            }
            if (malformed == 8)
            {
                SetBytes(24, UINT64_MAX);
            }
            if (malformed == 9)
            {
                SetBytes(SubMeshesCount() + 8 + offsetof(Importers::AssetSubMesh, StreamOffset), uint32_t(1));
            }
            if (malformed == 10)
            {
                SetBytes(SubMeshesCount() + 8 + offsetof(Importers::AssetSubMesh, IndexStreamOffset), uint32_t(1));
            }
            if (malformed == 11)
            {
                SetBytes(SubMeshesCount() + 8 + offsetof(Importers::AssetSubMesh, TotalByteSize), uint32_t(0));
            }
            Diagnostics.Clear();
            EXPECT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, &Diagnostics).Failed()) << malformed;
            EXPECT_EQ(inspection.Dependencies().data(), prior);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Discover);
            EXPECT_EQ(Diagnostics.Entries[0].Source, Input.Source);
            EXPECT_EQ(Diagnostics.Entries[0].Asset, Input.Identity);
            MeshSink sink = {};
            EXPECT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Failed());
            EXPECT_EQ(sink.Calls, 0u);
        }
    }

    TEST_F(CookMeshAdapterTest, EveryAllocationRefusalRollsBackWithoutFallbackOrResultMutation)
    {
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Succeeded());
        const auto* prior = inspection.Dependencies().data();
        for (uint32_t fail_at = 1; fail_at <= 3; ++fail_at)
        {
            MeshFaultAllocator allocator = {};
            allocator.Parent             = &Scratch;
            allocator.FailAt             = fail_at;
            EXPECT_EQ(Adapter.Inspect(nullptr, Input, allocator, &inspection, nullptr).Error(), VFS::VFSError::OutOfMemory);
            EXPECT_EQ(inspection.Dependencies().data(), prior);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
        }
        MeshFaultAllocator allocator = {};
        allocator.Parent             = &Scratch;
        allocator.FailAt             = 1;
        MeshSink   sink              = {};
        const auto child             = Resolved();
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, allocator, nullptr).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST_F(CookMeshAdapterTest, EncodedDecodedAndEdgeBudgetsFailCleanly)
    {
        for (uint32_t policy = 0; policy < 3; ++policy)
        {
            Limits = {.MaxWorkingBytes = ZKilo(256)};
            if (policy == 0)
            {
                Limits.MaxEncodedAssetBytes = Capture.Bytes.size() - 1;
            }
            if (policy == 1)
            {
                Limits.MaxDecodedAssetBytes = 1;
            }
            if (policy == 2)
            {
                Limits.MaxEdges = 0;
            }
            CookAssetInspection inspection = {};
            EXPECT_EQ(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(inspection.Format(), nullptr);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookMeshAdapterTest, ResolvedSlotsRejectAliasesMissingExtraWrongUUIDAndCorruptDescriptorsBeforeOutput)
    {
        for (uint32_t malformed = 0; malformed < 14; ++malformed)
        {
            CookResolvedDependency children[] = {Resolved(), Resolved()};
            size_t                 count      = malformed == 0 ? 0 : malformed == 1 ? 2 : 1;
            if (malformed == 2)
            {
                children[0].Slot = "submeshes/00/material";
            }
            if (malformed == 3)
            {
                children[0].Slot = "submeshes/1/material";
            }
            if (malformed == 4)
            {
                children[0].Slot = "submeshes/+0/material";
            }
            if (malformed == 5)
            {
                children[0].Slot = "submeshes/4294967296/material";
            }
            if (malformed == 6)
            {
                children[0].Slot = "submeshes/0/material/";
            }
            if (malformed == 7)
            {
                children[0].Identity.UUID = {};
            }
            if (malformed == 8)
            {
                children[0].Identity.UUID = MeshUUID();
            }
            if (malformed == 9)
            {
                children[0].Identity.Kind = CookAssetKind::Texture;
            }
            if (malformed == 10)
            {
                children[0].Format = "image-png";
            }
            if (malformed == 11)
            {
                children[0].ObjectDigest = {};
            }
            if (malformed == 12)
            {
                children[0].ObjectPath = VFS::VFSRelativePath::Parse("assets/material.zematerial").Value();
            }
            if (malformed == 13)
            {
                children[0].ObjectSize = 0;
            }
            MeshSink sink = {};
            EXPECT_TRUE(Adapter.Encode(nullptr, Input, {children, count}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Failed()) << malformed;
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        auto child       = Resolved();
        child.ObjectSize = Limits.MaxMaterialBytes + 1;
        MeshSink sink    = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST_F(CookMeshAdapterTest, LargePassthroughReusesDecodeStorageAndPreservesLateSinkFailure)
    {
        Mesh.Vertices.reserve(32768);
        Mesh.Vertices.m_size = 32768;
        SaveCapture();
        ASSERT_GT(Capture.Bytes.size(), ZKilo(64));
        const auto child = Resolved();
        MeshSink   good  = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, good, Scratch, nullptr).Succeeded());
        EXPECT_EQ(good.Bytes, Capture.Bytes);
        EXPECT_GT(good.Calls, 1u);
        for (auto error : {VFS::VFSError::PermissionDenied, VFS::VFSError::IOError, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded, VFS::VFSError::Cancelled})
        {
            MeshSink sink = {};
            sink.FailAt   = 2;
            sink.Error    = error;
            EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Error(), error);
            EXPECT_EQ(sink.Calls, 2u);
            EXPECT_EQ(sink.Bytes.size(), ZKilo(64));
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookMeshAdapterTest, CapturedReadAndDigestFailuresNeverReturnSuccessfulPassthrough)
    {
        const auto          child      = Resolved();
        CookAssetInspection inspection = {};
        Capture.Reads                  = 0;
        MeshSink valid                 = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, valid, Scratch, nullptr).Succeeded());
        const auto read_count = Capture.Reads;
        ASSERT_GT(read_count, 1u);
        for (uint32_t fail_at = 1; fail_at <= read_count; ++fail_at)
        {
            Capture.Reads   = 0;
            Capture.FailAt  = fail_at;
            MeshSink failed = {};
            EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, failed, Scratch, nullptr).Error(), VFS::VFSError::PermissionDenied);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        Capture.Reads  = 0;
        Capture.FailAt = 1;
        EXPECT_EQ(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        Capture.FailAt      = UINT32_MAX;
        Capture.WrongDigest = true;
        MeshSink sink       = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        Capture.WrongDigest    = false;
        Capture.AdvertisedSize = Capture.Bytes.size() + 1;
        MeshSink truncated     = {};
        EXPECT_TRUE(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, truncated, Scratch, nullptr).Failed());
        EXPECT_EQ(truncated.Calls, 0u);
    }

    TEST_F(CookMeshAdapterTest, InvalidMetadataAndIncompleteInputsAreRejectedBeforeAnyRead)
    {
        const auto valid   = Input;
        const auto sidecar = Metadata;
        for (uint32_t invalid = 0; invalid < 9; ++invalid)
        {
            Input    = valid;
            Metadata = sidecar;
            if (invalid == 0)
            {
                Input.Identity.Kind = CookAssetKind::Texture;
            }
            if (invalid == 1)
            {
                Input.Artifact = nullptr;
            }
            if (invalid == 2)
            {
                Input.Metadata = nullptr;
            }
            if (invalid == 3)
            {
                Input.Limits = nullptr;
            }
            if (invalid == 4)
            {
                Input.Source = {};
            }
            if (invalid == 5)
            {
                Input.EnvironmentSource = &Capture;
            }
            if (invalid == 6)
            {
                Metadata.Data.SettingsCount = 1;
            }
            if (invalid == 7)
            {
                Metadata.Data.AssetUUID = {};
            }
            if (invalid == 8)
            {
                Metadata.Fields = 0;
            }
            CookAssetInspection inspection = {};
            EXPECT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, &inspection, nullptr).Failed()) << invalid;
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        MeshSink sink = {};
        EXPECT_EQ(Adapter.Encode(nullptr, valid, {}, CookTarget::Invalid, sink, Scratch, nullptr).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Adapter.Inspect(nullptr, valid, Scratch, nullptr, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Capture.Reads, 0u);
        EXPECT_EQ(sink.Calls, 0u);
    }
} // namespace ZEngine::Cook
