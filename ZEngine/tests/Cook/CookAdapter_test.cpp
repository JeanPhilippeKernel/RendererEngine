#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookInputPath.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <string>
#include <type_traits>
#include <utility>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSResult<void> RegistryInspect(const void*, const CookAssetInput&, ICookAllocator&, CookAssetInspection*, CookDiagnostics*)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }

        VFS::VFSResult<void> RegistryEncode(const void*, const CookAssetInput&, Core::Containers::ArrayView<const CookResolvedDependency>, CookTarget, ICookByteSink&, ICookAllocator&, CookDiagnostics*)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }

        Core::Containers::FixedArray<CookAdapterFns, 4> RegistryEntries()
        {
            Core::Containers::FixedArray<CookAdapterFns, 4> entries = {};
            for (size_t i = 0; i < entries.size(); ++i)
                entries[i] = {static_cast<CookAssetKind>(i + 1), 1, nullptr, RegistryInspect, RegistryEncode};
            return entries;
        }

        CookAssetIdentity DependencyIdentity()
        {
            return {CookAssetKind::Texture, uuids::uuid::from_string("00000000-0000-0000-0000-000000000002").value()};
        }

        Digest256 AdapterDigest()
        {
            return Digest256::ParseHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef").Value();
        }

        struct PassthroughCapture final : ICookCapturedFile
        {
            Core::Containers::FixedArray<uint8_t, 8> Bytes       = {1, 2, 3, 4, 5, 6, 7, 8};
            Digest256                                Expected    = {};
            mutable uint32_t                         Reads       = 0;
            uint32_t                                 FailAt      = 0;
            bool                                     WrongDigest = false;

            PassthroughCapture()
            {
                Core::Hash::SHA256 hash = {};
                EXPECT_TRUE(hash.Update(Bytes).Succeeded());
                EXPECT_TRUE(hash.Finalize(&Expected).Succeeded());
            }
            uint64_t Size() const noexcept override
            {
                return Bytes.size();
            }
            Digest256 Digest() const noexcept override
            {
                auto result = Expected;
                if (WrongDigest)
                    result.Bytes[0] ^= 1;
                return result;
            }
            VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const override
            {
                if (++Reads == FailAt)
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::PermissionDenied);
                if (offset >= Bytes.size() || output.empty())
                    return VFS::VFSResult<size_t>::Ok(0);
                // Deliberately fragmented capture: the shared exact-read loop
                // must finish it without enlarging or allocating a stream.
                output[0] = Bytes[static_cast<size_t>(offset)];
                return VFS::VFSResult<size_t>::Ok(1);
            }
        };

        struct PassthroughSink final : ICookByteSink
        {
            Core::Containers::FixedArray<uint8_t, 8> Bytes   = {};
            size_t                                   Written = 0;
            uint32_t                                 Calls   = 0;
            uint32_t                                 FailAt  = 0;

            VFS::VFSResult<void>                     Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                if (++Calls == FailAt)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::IOError);
                if (bytes.size() > Bytes.size() - Written)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                Helpers::secure_memcpy(Bytes.data() + Written, Bytes.size() - Written, bytes.data(), bytes.size());
                Written += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct OpaquePassthroughAllocator final : ICookAllocator
        {
            uint32_t              Calls = 0;
            VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
            {
                ++Calls;
                return VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory);
            }
            void                 Free(void*) noexcept override {}
            CookMemoryStatistics Statistics() const noexcept override
            {
                return {};
            }
            // Intentionally no fixed-state contract: never silently waive it.
        };
    } // namespace

    TEST(CookAdapterTest, RegistryIsFixedCompleteImmutableAndConstantTimeIndexed)
    {
        CookAdapterRegistry registry = {};
        EXPECT_FALSE(registry.IsInitialized());
        EXPECT_EQ(registry.Find(CookAssetKind::Material), nullptr);
        auto entries = RegistryEntries();
        std::swap(entries[0], entries[3]);
        entries[1] = CookMaterialAdapter();
        ASSERT_TRUE(registry.Initialize(entries).Succeeded());
        for (uint32_t i = 1; i <= 4; ++i)
        {
            const auto* found = registry.Find(static_cast<CookAssetKind>(i));
            ASSERT_NE(found, nullptr);
            EXPECT_EQ(found->Kind, static_cast<CookAssetKind>(i));
            EXPECT_EQ(found->Revision, 1u);
        }
        EXPECT_EQ(registry.Find(CookAssetKind::Unknown), nullptr);
        EXPECT_EQ(registry.Find(static_cast<CookAssetKind>(255)), nullptr);
        EXPECT_EQ(registry.Initialize(entries).Error(), VFS::VFSError::InvalidPath);
        entries[1].Revision = 99;
        EXPECT_EQ(registry.Find(CookAssetKind::Material)->Revision, 1u);
    }

    TEST(CookAdapterTest, RegistryRejectsPartialDuplicateUnknownAndIncompleteCallbacksWithoutChangingState)
    {
        for (uint32_t failure = 0; failure < 8; ++failure)
        {
            CookAdapterRegistry registry = {};
            auto                entries  = RegistryEntries();
            if (failure == 0)
                entries[3] = entries[0];
            if (failure == 1)
                entries[0].Kind = CookAssetKind::Unknown;
            if (failure == 2)
                entries[0].Kind = static_cast<CookAssetKind>(255);
            if (failure == 3)
                entries[0].Revision = 0;
            if (failure == 4)
                entries[0].Inspect = nullptr;
            if (failure == 5)
                entries[0].Encode = nullptr;
            const Core::Containers::ArrayView<const CookAdapterFns> view = failure == 6 ? Core::Containers::ArrayView<const CookAdapterFns>{entries.data(), 3} : failure == 7 ? Core::Containers::ArrayView<const CookAdapterFns>{nullptr, 4} : Core::Containers::ArrayView<const CookAdapterFns>{entries};
            EXPECT_TRUE(registry.Initialize(view).Failed()) << failure;
            EXPECT_FALSE(registry.IsInitialized());
            EXPECT_EQ(registry.Find(CookAssetKind::Mesh), nullptr);
            entries = RegistryEntries();
            EXPECT_TRUE(registry.Initialize(entries).Succeeded());
        }
    }

    TEST(CookAdapterTest, FormatTableAndContentAddressedObjectDescriptorsAgreeExactly)
    {
        constexpr cstring keys[] = {"zenvmap-v2-le64", "zematerial-v1-json", "zemesh-v1-le64", "image-png", "image-jpeg", "image-bmp", "image-tga", "image-gif", "image-psd", "image-pic"};
        const auto        digest = AdapterDigest();
        for (cstring key : keys)
        {
            const auto* format = FindCookAssetFormat(key);
            ASSERT_NE(format, nullptr);
            EXPECT_EQ(format->Revision, 1u);
            const auto        hex  = digest.Hex();
            const std::string text = std::string("objects/") + hex.data() + "." + format->Extension;
            const auto        path = VFS::VFSRelativePath::Parse(text.data(), text.size()).Value();
            EXPECT_TRUE(IsCookObjectDescriptorValid(*format, path, digest, 1));
            EXPECT_TRUE(IsCookObjectDescriptorValid(*format, path, digest, ZGiga(16)));
            EXPECT_FALSE(IsCookObjectDescriptorValid(*format, path, digest, 0));
            EXPECT_FALSE(IsCookObjectDescriptorValid(*format, path, digest, ZGiga(16) + 1));
            EXPECT_FALSE(IsCookObjectDescriptorValid(*format, path, {}, 1));
            auto wrong = *format;
            ++wrong.Revision;
            EXPECT_FALSE(IsCookObjectDescriptorValid(wrong, path, digest, 1));
            wrong           = *format;
            wrong.Extension = "other";
            EXPECT_FALSE(IsCookObjectDescriptorValid(wrong, path, digest, 1));
        }
        EXPECT_EQ(FindCookAssetFormat({}), nullptr);
        EXPECT_EQ(FindCookAssetFormat("image-jpg"), nullptr);
        EXPECT_EQ(FindCookAssetFormat("image-PNG"), nullptr);
        const auto* png = FindCookAssetFormat("image-png");
        ASSERT_NE(png, nullptr);
        EXPECT_FALSE(IsCookObjectDescriptorValid(*png, VFS::VFSRelativePath::Parse("objects/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.jpg").Value(), digest, 1));
    }

    TEST(CookAdapterTest, InspectionDeepCopiesActualDependenciesAndTextInOneAllocation)
    {
        Tests::FixedAllocator<4096, 3> allocator  = {};
        CookAssetInspection            inspection = {};
        char                           slot[]     = "albedo";
        char                           path[]     = "/Assets/tex.png";
        const CookDependency           dependency = {DependencyIdentity(), slot, path};
        ASSERT_TRUE(inspection.Assign("zematerial-v1-json", AdapterDigest(), {}, {&dependency, 1}, allocator).Succeeded());
        EXPECT_EQ(allocator.Allocations, 1u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, sizeof(CookDependency) + sizeof(slot) + sizeof(path));
        Helpers::secure_memset(slot, 'x', sizeof(slot) - 1, sizeof(slot) - 1);
        Helpers::secure_memset(path, 'x', sizeof(path) - 1, sizeof(path) - 1);
        ASSERT_EQ(inspection.Dependencies().size(), 1u);
        EXPECT_EQ(inspection.Dependencies()[0].Slot, Core::Containers::StringView("albedo"));
        EXPECT_EQ(inspection.Dependencies()[0].SourcePath, Core::Containers::StringView("/Assets/tex.png"));
        EXPECT_EQ(inspection.LocalContentKey(), AdapterDigest());
        EXPECT_STREQ(inspection.Format(), "zematerial-v1-json");
        // Self-aliased assignment must copy before releasing old storage.
        EXPECT_TRUE(inspection.Assign(inspection.Format(), inspection.LocalContentKey(), inspection.MetadataKey(), inspection.Dependencies(), allocator).Succeeded());
        EXPECT_EQ(inspection.Dependencies()[0].Slot, Core::Containers::StringView("albedo"));
        EXPECT_EQ(allocator.Frees, 1u);
        inspection.Reset();
        inspection.Reset();
        EXPECT_EQ(allocator.Frees, 2u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
    }

    TEST(CookAdapterTest, LeafInspectionsNeedNoResultAllocationAndMovesTransferOneOwner)
    {
        static_assert(!std::is_copy_constructible_v<CookAssetInspection>);
        static_assert(std::is_nothrow_move_constructible_v<CookAssetInspection>);
        Tests::FixedAllocator<4096, 3> allocator = {};
        CookAssetInspection            first = {}, second = {};
        ASSERT_TRUE(first.Assign("image-png", AdapterDigest(), {}, {}, allocator).Succeeded());
        EXPECT_EQ(allocator.Allocations, 0u);
        const CookDependency dependency = {DependencyIdentity(), "albedo", "assets/tex.png"};
        ASSERT_TRUE(first.Assign("zematerial-v1-json", AdapterDigest(), {}, {&dependency, 1}, allocator).Succeeded());
        ASSERT_TRUE(second.Assign("zematerial-v1-json", {}, {}, {&dependency, 1}, allocator).Succeeded());
        CookAssetInspection moved(std::move(first));
        EXPECT_EQ(first.Format(), nullptr);
        EXPECT_TRUE(first.Dependencies().empty());
        second = std::move(moved);
        EXPECT_EQ(moved.Format(), nullptr);
        EXPECT_EQ(allocator.Frees, 1u);
        auto* same = &second;
        second     = std::move(*same);
        EXPECT_EQ(second.LocalContentKey(), AdapterDigest());
        second.Reset();
        EXPECT_EQ(allocator.Frees, 2u);
    }

    TEST(CookAdapterTest, InvalidInspectionAndAllocationFailurePreserveExistingResult)
    {
        Tests::FixedAllocator<4096, 3> allocator  = {};
        CookAssetInspection            inspection = {};
        const CookDependency           valid      = {DependencyIdentity(), "albedo", "assets/tex.png"};
        ASSERT_TRUE(inspection.Assign("zematerial-v1-json", AdapterDigest(), {}, {&valid, 1}, allocator).Succeeded());
        const auto* original = inspection.Dependencies().data();
        for (uint32_t failure = 0; failure < 8; ++failure)
        {
            CookDependency dependencies[] = {
                valid, {DependencyIdentity(), "normal", {}}
            };
            if (failure == 0)
                dependencies[0].Identity = {};
            if (failure == 1)
                dependencies[0].Slot = {};
            if (failure == 2)
                dependencies[0].Slot = {nullptr, 3};
            if (failure == 3)
                dependencies[0].SourcePath = "/Assets/../tex.png";
            if (failure == 4)
                dependencies[1].Slot = "albedo";
            if (failure == 5)
                dependencies[0].Slot = "specular";
            if (failure == 6)
                allocator.Failure = VFS::VFSError::OutOfMemory;
            EXPECT_TRUE(inspection.Assign(failure == 7 ? "unknown" : "zematerial-v1-json", {}, {}, dependencies, allocator).Failed()) << failure;
            EXPECT_EQ(inspection.Dependencies().data(), original);
            EXPECT_EQ(inspection.LocalContentKey(), AdapterDigest());
            allocator.Failure = VFS::VFSError::OK;
        }
        EXPECT_EQ(allocator.Statistics().LiveBytes, sizeof(CookDependency) + sizeof("albedo") + sizeof("assets/tex.png"));
    }

    TEST(CookAdapterTest, PassthroughChargesExactPrincipalHashStateWithoutAllocating)
    {
        Tests::FixedAllocator<4096, 1> allocator = {};
        constexpr uint64_t             bytes     = Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(CookMemoryCharge);
        for (uint64_t shortfall : {1u, 0u})
        {
            SCOPED_TRACE(shortfall);
            ASSERT_TRUE(allocator.Ledger.Initialize(bytes - shortfall).Succeeded());
            PassthroughCapture                       capture = {};
            PassthroughSink                          sink    = {};
            Core::Containers::FixedArray<uint8_t, 2> stream  = {};
            EXPECT_EQ(Detail::CopyCapturedAsset(capture, sink, stream, capture.Size(), allocator).Error(), shortfall ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::OK);
            if (shortfall)
            {
                EXPECT_EQ(capture.Reads, 0u);
                EXPECT_EQ(sink.Calls, 0u);
            }
            else
            {
                EXPECT_EQ(sink.Bytes, capture.Bytes);
                EXPECT_EQ(sink.Written, capture.Size());
                EXPECT_EQ(capture.Reads, capture.Size());
                EXPECT_EQ(allocator.Statistics().PeakBytes, bytes);
            }
            EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
            EXPECT_EQ(allocator.Allocations, 0u);
        }
    }

    TEST(CookAdapterTest, PassthroughUnknownFixedContractRefusesBeforeReadOrWrite)
    {
        OpaquePassthroughAllocator               allocator = {};
        PassthroughCapture                       capture   = {};
        PassthroughSink                          sink      = {};
        Core::Containers::FixedArray<uint8_t, 2> stream    = {};
        EXPECT_EQ(Detail::CopyCapturedAsset(capture, sink, stream, capture.Size(), allocator).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(capture.Reads, 0u);
        EXPECT_EQ(sink.Calls, 0u);
        EXPECT_EQ(allocator.Calls, 0u);
    }

    TEST(CookAdapterTest, PassthroughReadWriteAndDigestFailuresReleasePrincipalHashCharge)
    {
        for (uint32_t failure = 0; failure < 3; ++failure)
        {
            SCOPED_TRACE(failure);
            Tests::FixedAllocator<4096, 1>           allocator = {};
            PassthroughCapture                       capture   = {};
            PassthroughSink                          sink      = {};
            Core::Containers::FixedArray<uint8_t, 2> stream    = {};
            capture.FailAt                                     = failure == 0 ? 3 : 0;
            sink.FailAt                                        = failure == 1 ? 2 : 0;
            capture.WrongDigest                                = failure == 2;
            const auto error                                   = failure == 0 ? VFS::VFSError::PermissionDenied : failure == 1 ? VFS::VFSError::IOError : VFS::VFSError::Corrupted;
            EXPECT_EQ(Detail::CopyCapturedAsset(capture, sink, stream, capture.Size(), allocator).Error(), error);
            EXPECT_GT(allocator.Statistics().PeakBytes, 0u);
            EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
            EXPECT_EQ(allocator.Allocations, 0u);
        }
    }

    TEST(CookAdapterTest, PersistedInputPathParsingRejectsOriginalAliasesBeforeNormalization)
    {
        const auto logical = ParseCookInputPath("assets/tex.png");
        ASSERT_TRUE(logical.Succeeded());
        EXPECT_TRUE(logical.Value().Logical.IsValid());
        EXPECT_FALSE(logical.Value().Context.IsValid());
        const auto context = ParseCookInputPath("/Assets/tex.png");
        ASSERT_TRUE(context.Succeeded());
        EXPECT_FALSE(context.Value().Logical.IsValid());
        EXPECT_STREQ(context.Value().Context.CStr(), "/Assets/tex.png");
        for (cstring invalid : {"", "/", "tex.png", "Assets/tex.png", "/Assets//tex.png", "/Assets/./tex.png", "/Assets/../tex.png", "//server/tex.png", "C:/tex.png", "assets\\tex.png", "assets/tex.png/", "/Assets/tex.png\n"})
            EXPECT_TRUE(ParseCookInputPath(invalid).Failed()) << invalid;
        EXPECT_TRUE(ParseCookInputPath({"assets/tex.png\0ignored", 22}).Failed());
        EXPECT_TRUE(ParseCookInputPath({nullptr, 20}).Failed());
    }
} // namespace ZEngine::Cook
