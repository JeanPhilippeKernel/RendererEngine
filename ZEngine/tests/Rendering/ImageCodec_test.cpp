#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/ImageCodec.h>
#include <gtest/gtest.h>
#include <stb/stb_image.h>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>

namespace ZEngine::Importers::ImageCodec
{
    namespace
    {
        // Same one-pixel PNG already used by the importer fixture; no renderer,
        // image writer, fixture file, or GPU initialization is needed to decode it.
        constexpr uint8_t Png[] = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00, 0x03, 0x03, 0x02, 0x00, 0xef, 0x9a, 0xde, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
        };

        struct CountingImageRoute final : IAllocationRoute, Core::Memory::IWorkingMemoryBudget
        {
            struct alignas(std::max_align_t) Header
            {
                CountingImageRoute* Owner = nullptr;
                size_t              Bytes = 0;
            };

            size_t                                  Attempts        = 0;
            size_t                                  RefuseAtAttempt = std::numeric_limits<size_t>::max();
            size_t                                  LiveBlocks      = 0;
            size_t                                  LiveBytes       = 0;
            size_t                                  PeakBytes       = 0;
            size_t                                  Reallocations   = 0;
            bool                                    AccountFixed    = false;
            size_t                                  FixedLive       = 0;
            size_t                                  FixedPeak       = 0;
            size_t                                  CombinedPeak    = 0;
            size_t                                  WorkingCalls    = 0;
            size_t                                  RefuseWorkingAt = SIZE_MAX;
            Core::Containers::FixedArray<size_t, 4> WorkingBytes    = {};
            Core::Containers::FixedArray<size_t, 4> HeapAtWorking   = {};

            Core::Memory::IWorkingMemoryBudget*     WorkingMemoryBudget() noexcept override
            {
                return AccountFixed ? this : nullptr;
            }
            bool TryReserveWorkingBytes(size_t bytes) noexcept override
            {
                if (WorkingCalls < WorkingBytes.size())
                {
                    WorkingBytes[WorkingCalls]  = bytes;
                    HeapAtWorking[WorkingCalls] = LiveBytes;
                }
                if (++WorkingCalls == RefuseWorkingAt)
                {
                    return false;
                }
                FixedLive    += bytes;
                FixedPeak     = std::max(FixedPeak, FixedLive);
                CombinedPeak  = std::max(CombinedPeak, FixedLive + LiveBytes);
                return true;
            }
            void ReleaseWorkingBytes(size_t bytes) noexcept override
            {
                EXPECT_LE(bytes, FixedLive);
                FixedLive -= bytes;
            }

            void* Allocate(size_t bytes) noexcept override
            {
                if (++Attempts >= RefuseAtAttempt || bytes > std::numeric_limits<size_t>::max() - sizeof(Header))
                {
                    return nullptr;
                }
                void* raw = std::malloc(sizeof(Header) + bytes);
                if (!raw)
                {
                    return nullptr;
                }
                auto* header = new (raw) Header{this, bytes};
                ++LiveBlocks;
                LiveBytes    += sizeof(Header) + bytes;
                PeakBytes     = std::max(PeakBytes, LiveBytes);
                CombinedPeak  = std::max(CombinedPeak, FixedLive + LiveBytes);
                return header + 1;
            }
            void* Reallocate(void* pointer, size_t old_bytes, size_t new_bytes) noexcept override
            {
                ++Reallocations;
                if (!pointer)
                {
                    return Allocate(new_bytes);
                }
                const auto* old = static_cast<Header*>(pointer) - 1;
                EXPECT_EQ(old->Owner, this);
                EXPECT_LE(old_bytes, old->Bytes);
                void* replacement = Allocate(new_bytes);
                if (!replacement)
                {
                    return nullptr;
                }
                const auto count = std::min({old_bytes, old->Bytes, new_bytes});
                if (count)
                {
                    EXPECT_EQ(Helpers::secure_memcpy(replacement, new_bytes, pointer, count), Helpers::MEMORY_OP_SUCCESS);
                }
                Free(pointer);
                return replacement;
            }
            void Free(void* pointer) noexcept override
            {
                auto* header = static_cast<Header*>(pointer) - 1;
                EXPECT_EQ(header->Owner, this);
                --LiveBlocks;
                LiveBytes -= sizeof(Header) + header->Bytes;
                header->~Header();
                std::free(header);
            }
        };

        bool DecodeAndRelease()
        {
            int   width = 0, height = 0, channels = 0;
            auto* pixels = stbi_load_from_memory(Png, static_cast<int>(sizeof(Png)), &width, &height, &channels, STBI_rgb_alpha);
            if (!pixels)
            {
                return false;
            }
            EXPECT_EQ(width, 1);
            EXPECT_EQ(height, 1);
            EXPECT_EQ(channels, 2);
            EXPECT_EQ(pixels[0], 255);
            EXPECT_EQ(pixels[1], 255);
            EXPECT_EQ(pixels[2], 255);
            EXPECT_EQ(pixels[3], 255);
            stbi_image_free(pixels);
            return true;
        }

        struct WorkerSlabScope
        {
            Core::Memory::TLSFSlab* Previous = Helpers::GetWorkerSlab();
            explicit WorkerSlabScope(Core::Memory::TLSFSlab* slab)
            {
                Helpers::SetWorkerSlab(slab);
            }
            ~WorkerSlabScope()
            {
                Helpers::SetWorkerSlab(Previous);
            }
        };

        struct BorrowedImageSource
        {
            Core::Containers::ArrayView<const uint8_t> Bytes      = {Png, sizeof(Png)};
            size_t                                     Chunk      = SIZE_MAX;
            uint32_t                                   Reads      = 0;
            Core::VFS::VFSError                        Error      = Core::VFS::VFSError::OK;
            bool                                       Overreport = false;

            AssetCodec::AssetByteSource                View()
            {
                return {
                    this,
                    [](void* context, Core::Containers::ArrayView<uint8_t> bytes, uint64_t offset) {
                        auto& self = *static_cast<BorrowedImageSource*>(context);
                        ++self.Reads;
                        if (self.Error != Core::VFS::VFSError::OK)
                        {
                            return Core::VFS::VFSResult<size_t>::Fail(self.Error);
                        }
                        if (self.Overreport)
                        {
                            return Core::VFS::VFSResult<size_t>::Ok(bytes.size() + 1);
                        }
                        const size_t count = offset >= self.Bytes.size() ? 0 : std::min({bytes.size(), self.Bytes.size() - static_cast<size_t>(offset), self.Chunk});
                        if (count)
                        {
                            Helpers::secure_memcpy(bytes.data(), bytes.size(), self.Bytes.data() + offset, count);
                        }
                        return Core::VFS::VFSResult<size_t>::Ok(count);
                    },
                    Bytes.size()};
            }
        };
        struct GeneratedBmpSource
        {
            inline static constexpr uint64_t          Size           = 54 + uint64_t(512) * 512 * 4;
            Core::Containers::FixedArray<uint8_t, 54> Header         = {};
            uint64_t                                  FailOffset     = UINT64_MAX;
            uint32_t                                  Reads          = 0;
            size_t                                    LargestRequest = 0;

            GeneratedBmpSource()
            {
                const auto word = [&](size_t offset, uint32_t value, size_t bytes = 4) {
                    for (size_t i = 0; i < bytes; ++i)
                    {
                        Header[offset + i] = static_cast<uint8_t>(value >> (8 * i));
                    }
                };
                Header[0] = 'B';
                Header[1] = 'M';
                word(2, static_cast<uint32_t>(Size));
                word(10, 54);
                word(14, 40);
                word(18, 512);
                word(22, 512);
                word(26, 1, 2);
                word(28, 32, 2);
                word(34, static_cast<uint32_t>(Size - 54));
            }
            AssetCodec::AssetByteSource View()
            {
                return {
                    this,
                    [](void* context, Core::Containers::ArrayView<uint8_t> output, uint64_t offset) {
                        auto& self = *static_cast<GeneratedBmpSource*>(context);
                        ++self.Reads;
                        self.LargestRequest = std::max(self.LargestRequest, output.size());
                        if (offset > Size)
                        {
                            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::Corrupted);
                        }
                        const auto count = static_cast<size_t>(std::min<uint64_t>(output.size(), Size - offset));
                        if (offset <= self.FailOffset && self.FailOffset - offset < count)
                        {
                            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::PermissionDenied);
                        }
                        constexpr uint8_t pixel[] = {11, 42, 93, 255};
                        for (size_t i = 0; i < count; ++i)
                        {
                            output[i] = offset + i < self.Header.size() ? self.Header[static_cast<size_t>(offset) + i] : pixel[(offset + i - self.Header.size()) % sizeof(pixel)];
                        }
                        return Core::VFS::VFSResult<size_t>::Ok(count);
                    },
                    Size};
            }
        };
    } // namespace

    TEST(ImageCodecTest, TinyDecoderReadsUseBoundedReadAheadWithoutAnotherAllocation)
    {
        GeneratedBmpSource source      = {};
        CountingImageRoute route       = {};
        route.AccountFixed             = true;
        EncodedImageDescription output = {};
        ASSERT_TRUE(ValidateEncodedImage(source.View(), route, {}, &output).Succeeded());
        EXPECT_EQ(output.Kind, EncodedImageKind::BMP);
        EXPECT_EQ(output.Width, 512u);
        EXPECT_EQ(output.Height, 512u);
        EXPECT_LE(source.Reads, GeneratedBmpSource::Size / ZKilo(16) + 8);
        EXPECT_EQ(source.LargestRequest, ZKilo(16));
        EXPECT_EQ(route.Attempts, 1u); // Native RGBA pixels, no cache allocation.
        EXPECT_EQ(route.LiveBlocks, 0u);
        EXPECT_EQ(route.FixedLive, 0u);
        EXPECT_GE(route.FixedPeak, ZKilo(16));

        source.Reads          = 0;
        source.LargestRequest = 0;
        const auto attempts   = route.Attempts;
        const auto previous   = output;
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, {.MaxDimension = 511}, &output).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_LE(source.Reads, 3u);
        EXPECT_LE(source.LargestRequest, 128u);
        EXPECT_EQ(route.Attempts, attempts);
        EXPECT_EQ(output.Width, previous.Width);
        EXPECT_EQ(output.Kind, previous.Kind);
    }

    TEST(ImageCodecTest, ReadAheadSourceFailurePreservesDescriptionAndReleasesPixelsAndFixedState)
    {
        GeneratedBmpSource source      = {};
        source.FailOffset              = ZKilo(32);
        CountingImageRoute route       = {};
        route.AccountFixed             = true;
        EncodedImageDescription output = {.Kind = EncodedImageKind::PNG, .Width = 77, .Height = 83, .SourceChannels = 2, .BitsPerChannel = 16};
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, {}, &output).Error(), Core::VFS::VFSError::PermissionDenied);
        EXPECT_EQ(output.Kind, EncodedImageKind::PNG);
        EXPECT_EQ(output.Width, 77u);
        EXPECT_EQ(output.Height, 83u);
        EXPECT_EQ(output.SourceChannels, 2u);
        EXPECT_EQ(output.BitsPerChannel, 16u);
        EXPECT_EQ(route.LiveBlocks, 0u);
        EXPECT_EQ(route.LiveBytes, 0u);
        EXPECT_EQ(route.FixedLive, 0u);
    }

    TEST(ImageCodecTest, NamedPNGFixedStatesShareTheBudgetWithoutAddingAnyDecoderAllocation)
    {
        CountingImageRoute      ordinary = {}, bounded = {};
        BorrowedImageSource     source = {};
        EncodedImageDescription output = {};
        ASSERT_TRUE(ValidateEncodedImage(source.View(), ordinary, {}, &output).Succeeded());
        bounded.AccountFixed = true;
        ASSERT_TRUE(ValidateEncodedImage(source.View(), bounded, {}, &output).Succeeded());
        EXPECT_EQ(bounded.WorkingCalls, 3u);
        EXPECT_GT(bounded.WorkingBytes[0], 0u);
        EXPECT_GT(bounded.WorkingBytes[1], 0u);
        EXPECT_GT(bounded.WorkingBytes[2], 4096u);
        EXPECT_EQ(bounded.FixedPeak, bounded.WorkingBytes[0] + bounded.WorkingBytes[1] + bounded.WorkingBytes[2]);
        EXPECT_EQ(bounded.Attempts, ordinary.Attempts);
        EXPECT_EQ(bounded.LiveBytes, 0u);
        EXPECT_EQ(bounded.FixedLive, 0u);
        EXPECT_EQ(output.Width, 1u);
    }

    TEST(ImageCodecTest, EachFixedStateRefusalPreservesTheDescriptionAndReleasesAllEarlierCharges)
    {
        for (size_t seam : {size_t(1), size_t(2), size_t(3)})
        {
            CountingImageRoute route       = {};
            route.AccountFixed             = true;
            route.RefuseWorkingAt          = seam;
            BorrowedImageSource     source = {};
            EncodedImageDescription output = {.Width = 77};
            EXPECT_EQ(ValidateEncodedImage(source.View(), route, {}, &output).Error(), Core::VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(route.WorkingCalls, seam);
            EXPECT_EQ(route.FixedLive, 0u);
            EXPECT_EQ(route.LiveBytes, 0u);
            EXPECT_EQ(output.Width, 77u);
            if (seam == 1)
            {
                EXPECT_EQ(source.Reads, 0u);
                EXPECT_EQ(route.Attempts, 0u);
            }
        }
    }

    TEST(ImageCodecTest, LargeGIFFixedDecodeStateStartsOnlyAfterItsRoutedHeaderStateIsReleased)
    {
        constexpr uint8_t  gif[]       = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0, 0, 0, 0, 255, 255, 255, 0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 1, 0, 0x3b};
        CountingImageRoute route       = {};
        route.AccountFixed             = true;
        BorrowedImageSource source     = {};
        source.Bytes                   = gif;
        EncodedImageDescription output = {};
        ASSERT_TRUE(ValidateEncodedImage(source.View(), route, {}, &output).Succeeded());
        EXPECT_EQ(output.Kind, EncodedImageKind::GIF);
        EXPECT_EQ(route.WorkingCalls, 2u);
        EXPECT_GT(route.WorkingBytes[1], 32000u);
        EXPECT_EQ(route.HeapAtWorking[1], 0u);
        EXPECT_LT(route.CombinedPeak, route.WorkingBytes[0] + 2 * route.WorkingBytes[1]);
        EXPECT_EQ(route.FixedLive, 0u);
        EXPECT_EQ(route.LiveBytes, 0u);
    }

    TEST(ImageCodecTest, CPUDecodeUsesAndReleasesEveryAllocationThroughTheExplicitRoute)
    {
        CountingImageRoute route = {};
        AllocationScope    scope(route);
        ASSERT_TRUE(DecodeAndRelease());
        EXPECT_GT(route.Attempts, 0u);
        EXPECT_GT(route.PeakBytes, 4u);
        EXPECT_EQ(route.LiveBlocks, 0u);
        EXPECT_EQ(route.LiveBytes, 0u);
    }

    TEST(ImageCodecTest, RefusalAtEachDecodeAllocationFailsWithoutLeakingOrHeapFallback)
    {
        CountingImageRoute baseline = {};
        {
            AllocationScope scope(baseline);
            ASSERT_TRUE(DecodeAndRelease());
        }
        for (size_t failure = 1; failure <= baseline.Attempts; ++failure)
        {
            SCOPED_TRACE(failure);
            CountingImageRoute route = {};
            route.RefuseAtAttempt    = failure;
            AllocationScope scope(route);
            EXPECT_FALSE(DecodeAndRelease());
            EXPECT_GE(route.Attempts, failure);
            EXPECT_EQ(route.LiveBlocks, 0u);
            EXPECT_EQ(route.LiveBytes, 0u);
        }
    }

    TEST(ImageCodecTest, SizedReallocationPreservesOldStorageOnRefusal)
    {
        CountingImageRoute route = {};
        AllocationScope    scope(route);
        auto*              original = static_cast<uint8_t*>(Allocate(4));
        ASSERT_NE(original, nullptr);
        original[0]           = 7;
        route.RefuseAtAttempt = route.Attempts + 1;
        EXPECT_EQ(Reallocate(original, 4, 8), nullptr);
        EXPECT_EQ(original[0], 7u);
        EXPECT_EQ(route.LiveBlocks, 1u);
        route.RefuseAtAttempt = std::numeric_limits<size_t>::max();
        auto* replacement     = static_cast<uint8_t*>(Reallocate(original, 4, 8));
        ASSERT_NE(replacement, nullptr);
        EXPECT_EQ(replacement[0], 7u);
        EXPECT_EQ(route.LiveBlocks, 1u);
        Free(replacement);
        EXPECT_EQ(route.LiveBytes, 0u);
    }

    TEST(ImageCodecTest, NestedRoutesRestoreThePreviousRoute)
    {
        CountingImageRoute outer = {}, inner = {};
        inner.RefuseAtAttempt = 1;
        AllocationScope scope(outer);
        ASSERT_TRUE(DecodeAndRelease());
        const auto attempts = outer.Attempts;
        {
            AllocationScope nested(inner);
            EXPECT_FALSE(DecodeAndRelease());
            EXPECT_EQ(outer.Attempts, attempts);
        }
        EXPECT_TRUE(DecodeAndRelease());
        EXPECT_GT(outer.Attempts, attempts);
        EXPECT_EQ(outer.LiveBytes, 0u);
        EXPECT_EQ(inner.LiveBytes, 0u);
    }

    TEST(ImageCodecTest, RoutesAreThreadLocalRatherThanGlobalDecoderState)
    {
        CountingImageRoute outer = {};
        outer.RefuseAtAttempt    = 1;
        AllocationScope scope(outer);
        bool            decoded = false;
        std::thread     other([&] {
            CountingImageRoute local = {};
            AllocationScope    local_scope(local);
            decoded = DecodeAndRelease();
            EXPECT_GT(local.Attempts, 0u);
            EXPECT_EQ(local.LiveBytes, 0u);
        });
        other.join();
        EXPECT_TRUE(decoded);
        EXPECT_EQ(outer.Attempts, 0u);
        EXPECT_FALSE(DecodeAndRelease());
    }

    TEST(ImageCodecTest, ExplicitScopeOverridesWorkerSlabAndRestoresTheExistingDefault)
    {
        Core::Memory::ArenaAllocator arena = {};
        arena.Initialize(ZKilo(128), 4096, "ImageCodecTest");
        Core::Memory::TLSFSlab slab = {};
        slab.Init(&arena, ZKilo(64));
        WorkerSlabScope worker(&slab);
        EXPECT_TRUE(DecodeAndRelease());
        CountingImageRoute refusing = {};
        refusing.RefuseAtAttempt    = 1;
        {
            AllocationScope scope(refusing);
            EXPECT_FALSE(DecodeAndRelease());
            EXPECT_GT(refusing.Attempts, 0u);
        }
        EXPECT_TRUE(DecodeAndRelease());
        slab.Shutdown();
    }

    TEST(ImageCodecTest, MainThreadDefaultStillWorksWithoutRendererInitialization)
    {
        WorkerSlabScope worker(nullptr);
        EXPECT_TRUE(DecodeAndRelease());
    }

    TEST(ImageCodecTest, BoundedValidationDescribesAndReleasesPNGThroughTheRequiredRoute)
    {
        BorrowedImageSource source     = {};
        source.Chunk                   = 1;
        CountingImageRoute      route  = {};
        EncodedImageDescription output = {};
        ASSERT_TRUE(ValidateEncodedImage(source.View(), route, {}, &output).Succeeded());
        EXPECT_EQ(output.Kind, EncodedImageKind::PNG);
        EXPECT_EQ(output.Width, 1u);
        EXPECT_EQ(output.Height, 1u);
        EXPECT_EQ(output.SourceChannels, 2u);
        EXPECT_EQ(output.BitsPerChannel, 8u);
        EXPECT_GT(source.Reads, sizeof(Png));
        EXPECT_GT(route.Attempts, 0u);
        EXPECT_EQ(route.LiveBlocks, 0u);
        EXPECT_EQ(route.LiveBytes, 0u);
    }

    TEST(ImageCodecTest, TGADetectionAndDimensionPreflightDoNotAllocateSpeculativeJPEGState)
    {
        constexpr uint8_t tga[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 0x28, 11, 42, 93, 255, 11, 42, 93, 255};
        for (bool with_id : {false, true})
        {
            SCOPED_TRACE(with_id);
            Core::Containers::FixedArray<uint8_t, sizeof(tga) + 255> bytes = {};
            (void) Helpers::secure_memcpy(bytes.data(), bytes.size(), tga, 18);
            bytes[0] = with_id ? 255 : 0;
            (void) Helpers::secure_memcpy(bytes.data() + 18 + bytes[0], bytes.size() - 18 - bytes[0], tga + 18, sizeof(tga) - 18);
            BorrowedImageSource source     = {};
            source.Bytes                   = {bytes.data(), sizeof(tga) + bytes[0]};
            source.Chunk                   = 1;
            CountingImageRoute refused     = {};
            refused.RefuseAtAttempt        = 1;
            EncodedImageLimits      limits = {.MaxDimension = 1};
            EncodedImageDescription output = {.Kind = EncodedImageKind::PNG, .Width = 77};
            EXPECT_EQ(ValidateEncodedImage(source.View(), refused, limits, &output).Error(), Core::VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(refused.Attempts, 0u);
            EXPECT_EQ(output.Kind, EncodedImageKind::PNG);
            EXPECT_EQ(output.Width, 77u);
            CountingImageRoute route = {};
            limits.MaxDimension      = 2;
            ASSERT_TRUE(ValidateEncodedImage(source.View(), route, limits, &output).Succeeded());
            EXPECT_EQ(output.Kind, EncodedImageKind::TGA);
            EXPECT_EQ(output.Width, 2u);
            EXPECT_EQ(output.Height, 1u);
            EXPECT_EQ(route.Attempts, 1u); // Only the real eight-byte pixel block.
            EXPECT_EQ(route.LiveBlocks, 0u);
            EXPECT_EQ(route.LiveBytes, 0u);
        }
    }

    TEST(ImageCodecTest, UnsupportedFormatDetectionDoesNotAllocateOrModifyTheDescription)
    {
        constexpr uint8_t   invalid[]  = {'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e'};
        BorrowedImageSource source     = {};
        source.Bytes                   = invalid;
        CountingImageRoute route       = {};
        route.RefuseAtAttempt          = 1;
        EncodedImageDescription output = {.Width = 77};
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, {}, &output).Error(), Core::VFS::VFSError::Unsupported);
        EXPECT_EQ(route.Attempts, 0u);
        EXPECT_EQ(output.Width, 77u);
    }

    TEST(ImageCodecTest, BoundedValidationPreservesDescriptionAcrossProviderAndPayloadFailures)
    {
        BorrowedImageSource     source = {};
        CountingImageRoute      route  = {};
        EncodedImageDescription output = {EncodedImageKind::JPEG, 10, 20, 3, 8};
        source.Error                   = Core::VFS::VFSError::PermissionDenied;
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, {}, &output).Error(), source.Error);
        source.Error      = Core::VFS::VFSError::OK;
        source.Overreport = true;
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, {}, &output).Error(), Core::VFS::VFSError::Corrupted);
        source.Overreport = false;
        source.Chunk      = 0;
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, {}, &output).Error(), Core::VFS::VFSError::Corrupted);
        source.Chunk = SIZE_MAX;
        source.Bytes = {Png, sizeof(Png) - 1};
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, {}, &output).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(output.Kind, EncodedImageKind::JPEG);
        EXPECT_EQ(output.Width, 10u);
        EXPECT_EQ(output.Height, 20u);
        EXPECT_EQ(output.SourceChannels, 3u);
        EXPECT_EQ(output.BitsPerChannel, 8u);
        EXPECT_EQ(route.LiveBytes, 0u);
    }

    TEST(ImageCodecTest, BoundedValidationChecksRequestsAndBudgetsBeforeReadingOrAllocating)
    {
        BorrowedImageSource     source = {};
        CountingImageRoute      route  = {};
        EncodedImageDescription output = {};
        auto                    bytes  = source.View();
        bytes.Read                     = nullptr;
        EXPECT_EQ(ValidateEncodedImage(bytes, route, {}, &output).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, {}, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        bytes      = source.View();
        bytes.Size = 0;
        EXPECT_EQ(ValidateEncodedImage(bytes, route, {}, &output).Error(), Core::VFS::VFSError::Corrupted);
        bytes.Size = UINT64_MAX;
        EXPECT_EQ(ValidateEncodedImage(bytes, route, {}, &output).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EncodedImageLimits limits = {};
        limits.MaxEncodedBytes    = 0;
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, limits, &output).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        limits                 = {};
        limits.MaxDecodedBytes = 0;
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, limits, &output).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        limits              = {};
        limits.MaxDimension = 0;
        EXPECT_EQ(ValidateEncodedImage(source.View(), route, limits, &output).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(source.Reads, 0u);
        EXPECT_EQ(route.Attempts, 0u);
    }

    TEST(ImageCodecTest, BoundedValidationNestsItsRequiredRouteAndRestoresThePreviousOne)
    {
        BorrowedImageSource source = {};
        CountingImageRoute  outer = {}, inner = {};
        outer.RefuseAtAttempt = 1;
        AllocationScope         previous(outer);
        EncodedImageDescription output = {};
        ASSERT_TRUE(ValidateEncodedImage(source.View(), inner, {}, &output).Succeeded());
        EXPECT_EQ(outer.Attempts, 0u);
        EXPECT_EQ(inner.LiveBytes, 0u);
        EXPECT_FALSE(DecodeAndRelease());
    }
} // namespace ZEngine::Importers::ImageCodec
