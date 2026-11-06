#pragma once
#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <type_traits>

namespace ZEngine::Cook::Tests
{
    namespace VFS = Core::VFS;

    // Constant-storage immutable captures: a few bounded periods generate an
    // arbitrarily long wire stream, without allocating input/output copies.
    struct GeneratedResourceCapture : ICookCapturedFile
    {
        struct Segment
        {
            Core::Containers::FixedArray<uint8_t, 512> Pattern = {};
            size_t                                     Period  = 0;
            uint64_t                                   Bytes   = 0;
        };

        Core::Containers::FixedArray<Segment, 8> Segments     = {};
        size_t                                   SegmentCount = 0;
        uint64_t                                 EncodedBytes = 0;
        Digest256                                Key          = {};
        mutable uint64_t                         Reads        = 0;
        mutable uint64_t                         MaxReadEnd   = 0;
        size_t                                   Chunk        = SIZE_MAX;
        uint64_t                                 FailOffset   = UINT64_MAX;
        uint64_t                                 PatchOffset  = UINT64_MAX;
        Core::Containers::FixedArray<uint8_t, 4> Patch        = {};

        void                                     Reset()
        {
            Segments     = {};
            SegmentCount = 0;
            EncodedBytes = 0;
            Key          = {};
            Reads        = 0;
            MaxReadEnd   = 0;
            Chunk        = SIZE_MAX;
            FailOffset = PatchOffset = UINT64_MAX;
            Patch                    = {};
        }
        void Append(Core::Containers::ArrayView<const uint8_t> period, uint64_t bytes)
        {
            if (!bytes)
            {
                return;
            }
            ASSERT_LT(SegmentCount, Segments.size());
            ASSERT_GT(period.size(), 0u);
            ASSERT_LE(period.size(), Segments[SegmentCount].Pattern.size());
            ASSERT_LE(bytes, UINT64_MAX - EncodedBytes);
            auto& segment = Segments[SegmentCount++];
            (void) Helpers::secure_memcpy(segment.Pattern.data(), segment.Pattern.size(), period.data(), period.size());
            segment.Period  = period.size();
            segment.Bytes   = bytes;
            EncodedBytes   += bytes;
        }
        uint64_t Size() const noexcept override
        {
            return EncodedBytes;
        }
        Digest256 Digest() const noexcept override
        {
            return Key;
        }
        VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const override
        {
            ++Reads;
            if (offset > EncodedBytes)
            {
                return VFS::VFSResult<size_t>::Fail(VFS::VFSError::Corrupted);
            }
            const size_t count = static_cast<size_t>(std::min<uint64_t>(std::min(output.size(), Chunk), EncodedBytes - offset));
            if (offset <= FailOffset && FailOffset - offset < count)
            {
                return VFS::VFSResult<size_t>::Fail(VFS::VFSError::PermissionDenied);
            }
            MaxReadEnd           = std::max(MaxReadEnd, offset + count);
            const uint64_t first = offset;
            uint64_t       base  = 0;
            size_t         done  = 0;
            for (size_t i = 0; i < SegmentCount && done < count; ++i)
            {
                const auto& segment = Segments[i];
                if (offset >= base + segment.Bytes)
                {
                    base += segment.Bytes;
                    continue;
                }
                const uint64_t at      = offset - base;
                const size_t   payload = static_cast<size_t>(std::min<uint64_t>(count - done, segment.Bytes - at));
                auto*          bytes   = output.data() + done;
                size_t         filled  = std::min(payload, segment.Period);
                for (size_t j = 0; j < filled; ++j)
                {
                    bytes[j] = segment.Pattern[(at + j) % segment.Period];
                }
                // Linear bytes copied, logarithmic nonoverlapping copies.
                while (filled < payload)
                {
                    const size_t next = std::min(filled, payload - filled);
                    (void) Helpers::secure_memcpy(bytes + filled, payload - filled, bytes, next);
                    filled += next;
                }
                offset += payload;
                done   += payload;
                base   += segment.Bytes;
            }
            if (done != count)
            {
                return VFS::VFSResult<size_t>::Fail(VFS::VFSError::Corrupted);
            }
            if (PatchOffset != UINT64_MAX)
            {
                for (size_t i = 0; i < Patch.size(); ++i)
                {
                    if (PatchOffset <= UINT64_MAX - i && PatchOffset + i >= first && PatchOffset + i - first < count)
                    {
                        output[static_cast<size_t>(PatchOffset + i - first)] = Patch[i];
                    }
                }
            }
            return VFS::VFSResult<size_t>::Ok(count);
        }
        void HashContents()
        {
            Core::Containers::FixedArray<uint8_t, ZKilo(64)> bytes = {};
            Core::Hash::SHA256                               hash  = {};
            for (uint64_t offset = 0; offset < Size();)
            {
                const auto read = Read(offset, bytes);
                ASSERT_TRUE(read.Succeeded());
                ASSERT_GT(read.Value(), 0u);
                ASSERT_TRUE(hash.Update({bytes.data(), read.Value()}).Succeeded());
                offset += read.Value();
            }
            ASSERT_TRUE(hash.Finalize(&Key).Succeeded());
        }
    };

    struct VerifyingResourceSink final : ICookByteSink
    {
        const ICookCapturedFile*                         Capture = nullptr;
        Core::Containers::FixedArray<uint8_t, ZKilo(64)> Buffer  = {};
        uint64_t                                         Bytes   = 0;
        uint64_t                                         Calls   = 0;

        VFS::VFSResult<void>                             Write(Core::Containers::ArrayView<const uint8_t> bytes) override
        {
            ++Calls;
            if (!Capture || bytes.size() > Buffer.size())
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            const auto read = Capture->Read(Bytes, {Buffer.data(), bytes.size()});
            if (read.Failed() || read.Value() != bytes.size() || !std::equal(bytes.begin(), bytes.end(), Buffer.begin()))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            Bytes += bytes.size();
            return VFS::VFSResult<void>::Ok();
        }
    };

    struct ResourceWireBytes
    {
        Core::Containers::FixedArray<uint8_t, 512> Bytes = {};
        size_t                                     Count = 0;

        template <typename T>
        void Append(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            ASSERT_LE(sizeof(T), Bytes.size() - Count);
            (void) Helpers::secure_memcpy(Bytes.data() + Count, Bytes.size() - Count, &value, sizeof(T));
            Count += sizeof(T);
        }
        Core::Containers::ArrayView<const uint8_t> View() const
        {
            return {Bytes.data(), Count};
        }
    };

    // Shared constant-storage native-v1 mesh oracle for adapter-limit and
    // full-publication fixtures. It does not call the production mesh emitter.
    inline void ConfigureGeneratedMesh(GeneratedResourceCapture& capture, uint32_t vertices, uint32_t indices, const uuids::uuid& mesh, const uuids::uuid& hierarchy, const uuids::uuid& material = {}, uint64_t* index_offset = nullptr)
    {
        capture.Reset();
        const uint64_t    vertex_bytes = uint64_t(vertices) * 8 * sizeof(float);
        const uint64_t    index_bytes  = uint64_t(indices) * sizeof(uint32_t);
        ResourceWireBytes prefix       = {};
        prefix.Append(Importers::AssetCodec::AssetMeshFileHeader{ZEMESH_MAGIC, ASSET_FILE_VERSION, mesh});
        prefix.Append(uint64_t(vertices) * 8);
        capture.Append(prefix.View(), prefix.Count);
        constexpr float triangle[24] = {0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1};
        capture.Append({reinterpret_cast<const uint8_t*>(triangle), sizeof(triangle)}, vertex_bytes);
        ResourceWireBytes count = {};
        count.Append(uint64_t(indices));
        capture.Append(count.View(), count.Count);
        if (index_offset)
        {
            *index_offset = capture.Size();
        }
        constexpr uint32_t index[] = {0, 1, 2};
        capture.Append({reinterpret_cast<const uint8_t*>(index), sizeof(index)}, index_bytes);
        Core::Maths::Mat4f matrix = {};
        for (size_t i = 0; i < 4; ++i)
        {
            matrix.m_data[i][i] = 1.f;
        }
        ResourceWireBytes tail = {};
        tail.Append(uint64_t{1});
        tail.Append(Importers::AssetSubMesh{.MaterialUUID = material, .VertexCount = vertices, .IndexCount = indices, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = static_cast<uint32_t>(vertex_bytes + index_bytes)});
        tail.Append(hierarchy);
        tail.Append(mesh);
        tail.Append(uint64_t{1});
        tail.Append(Helpers::NodeHierarchy{.DepthLevel = 0});
        tail.Append(uint64_t{1});
        tail.Append(matrix);
        tail.Append(uint64_t{1});
        tail.Append(matrix);
        tail.Append(uint32_t{0}); // Names.
        tail.Append(uint32_t{0}); // Material names.
        tail.Append(uint64_t{0}); // Node-name map.
        tail.Append(uint64_t{1}); // Node-mesh map: root -> submesh 0.
        tail.Append(uint32_t{0});
        tail.Append(uint32_t{0});
        tail.Append(uint64_t{0}); // Node-material map.
        capture.Append(tail.View(), tail.Count);
    }

    class ResourceProcessTest : public testing::Test
    {
    protected:
        CookPageAllocator       Native   = {};
        CookBudgetAllocator     Budget   = {};
        VFS::StrictMetaFileData Metadata = {};
        CookLimits              Limits   = {.MaxWorkingBytes = ZMega(768)};
        CookAssetInput          Input    = {};
        CookAdapterFns          Adapter  = {};
        uint64_t                Idle     = 0;

        void                    SetUp() override
        {
            ASSERT_TRUE(Native.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Budget.Initialize(Native, Limits.MaxWorkingBytes).Succeeded());
            Idle = Budget.Statistics().LiveBytes;
        }
        void InitializeInput(CookAssetKind kind, cstring metadata, const ICookCapturedFile& artifact, cstring path, const ICookCapturedFile* source = nullptr)
        {
            ASSERT_TRUE(VFS::MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(metadata), Helpers::secure_strlen(metadata)}, &Metadata).Succeeded());
            Input = {
                {kind, Metadata.Data.AssetUUID},
                &artifact, source, &Metadata, &Limits, VFS::VFSRelativePath::Parse(path).Value()
            };
        }
        void TearDown() override
        {
            ExpectDrained();
            Budget.Release();
            Native.Release();
            EXPECT_EQ(Budget.Statistics().LiveBytes, 0u);
            EXPECT_EQ(Native.Statistics().LiveBytes, 0u);
        }
        void ExpectDrained()
        {
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
            EXPECT_EQ(Native.OutstandingAllocations(), 0u);
            EXPECT_EQ(Budget.Statistics().LiveBytes, Idle);
            EXPECT_EQ(Native.Statistics().LiveBytes, sizeof(Native));
        }
        VFS::VFSResult<void> Inspect(CookAssetInspection& output)
        {
            return Adapter.Inspect(nullptr, Input, Budget, &output, nullptr);
        }
        VFS::VFSResult<void> Encode(VerifyingResourceSink& sink)
        {
            sink.Capture = Input.Artifact;
            return Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, sink, Budget, nullptr);
        }
        void ResetBudget(uint64_t bytes)
        {
            ExpectDrained();
            Budget.Release();
            ASSERT_TRUE(Budget.Initialize(Native, bytes).Succeeded());
            Limits.MaxWorkingBytes = bytes;
            EXPECT_EQ(Budget.Statistics().LiveBytes, Idle);
        }
    };
} // namespace ZEngine::Cook::Tests
