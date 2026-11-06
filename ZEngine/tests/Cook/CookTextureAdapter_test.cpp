#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <stb/stb_image_write.h>
#include <algorithm>
#include <string>
#include <vector>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        constexpr uint8_t Png[] = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00, 0x03, 0x03, 0x02, 0x00, 0xef, 0x9a, 0xde, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
        };

        struct TextureCapture final : ICookCapturedFile
        {
            std::vector<uint8_t> Bytes          = {std::begin(Png), std::end(Png)};
            size_t               Chunk          = SIZE_MAX;
            uint64_t             AdvertisedSize = UINT64_MAX;
            mutable uint32_t     Reads          = 0;
            uint32_t             FailAt         = UINT32_MAX;
            bool                 WrongDigest    = false;
            bool                 Overreport     = false;

            uint64_t             Size() const noexcept override
            {
                return AdvertisedSize == UINT64_MAX ? Bytes.size() : AdvertisedSize;
            }
            Digest256 Digest() const noexcept override
            {
                Core::Hash::SHA256 hash   = {};
                Digest256          digest = {};
                (void) hash.Update({Bytes.data(), Bytes.size()});
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
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::PermissionDenied);
                }
                if (Overreport)
                {
                    return VFS::VFSResult<size_t>::Ok(output.size() + 1);
                }
                const size_t count = offset >= Bytes.size() ? 0 : std::min({output.size(), Bytes.size() - static_cast<size_t>(offset), Chunk});
                if (count)
                {
                    Helpers::secure_memcpy(output.data(), output.size(), Bytes.data() + offset, count);
                }
                return VFS::VFSResult<size_t>::Ok(count);
            }
        };

        struct TextureSink final : ICookByteSink
        {
            std::vector<uint8_t> Bytes  = {};
            uint32_t             Calls  = 0;
            uint32_t             FailAt = UINT32_MAX;

            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                if (++Calls == FailAt)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::PermissionDenied);
                }
                Bytes.insert(Bytes.end(), bytes.begin(), bytes.end());
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct TextureFaultAllocator final : ICookAllocator
        {
            ICookAllocator*      Parent = nullptr;
            uint32_t             Calls  = 0;
            uint32_t             FailAt = UINT32_MAX;
            VFS::VFSError        Error  = VFS::VFSError::OutOfMemory;

            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                return Parent->TryCharge(bytes, category, output);
            }

            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return ++Calls == FailAt ? VFS::VFSResult<void*>::Fail(Error) : Parent->Allocate(bytes, alignment, category);
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

        void AppendImage(void* context, void* bytes, int count)
        {
            auto&       output = *static_cast<std::vector<uint8_t>*>(context);
            const auto* first  = static_cast<uint8_t*>(bytes);
            output.insert(output.end(), first, first + count);
        }
    } // namespace

    class CookTextureAdapterTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(256), 1> Parent      = {};
        CookScratchAllocator                 Scratch     = {};
        TextureCapture                       Capture     = {};
        VFS::StrictMetaFileData              Metadata    = {};
        CookLimits                           Limits      = {.MaxWorkingBytes = ZKilo(512)};
        CookAssetInput                       Input       = {};
        CookAdapterFns                       Adapter     = CookTextureAdapter();
        CookDiagnostics                      Diagnostics = {};

        void                                 SetUp() override
        {
            constexpr cstring meta = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"TextureImporter","settings":[]})";
            ASSERT_TRUE(VFS::MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(meta), Helpers::secure_strlen(meta)}, &Metadata).Succeeded());
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Scratch.Initialize(Parent, ZKilo(256), CookMemoryCategory::Codecs).Succeeded());
            Input = {
                {CookAssetKind::Texture, Metadata.Data.AssetUUID},
                &Capture, nullptr, &Metadata, &Limits, VFS::VFSRelativePath::Parse("assets/texture.png").Value()
            };
        }
        void TearDown() override
        {
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Scratch.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            EXPECT_EQ(Parent.Frees, 1u);
            EXPECT_EQ(Parent.Statistics().LiveBytes, 0u);
        }
        VFS::VFSResult<void> Inspect(CookAssetInspection& result)
        {
            return Adapter.Inspect(nullptr, Input, Scratch, &result, &Diagnostics);
        }
        VFS::VFSResult<void> Encode(TextureSink& sink)
        {
            return Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, sink, Scratch, &Diagnostics);
        }
        void ExpectRoundTrip(cstring format)
        {
            CookAssetInspection result = {};
            ASSERT_TRUE(Inspect(result).Succeeded());
            EXPECT_STREQ(result.Format(), format);
            EXPECT_EQ(result.LocalContentKey(), Capture.Digest());
            EXPECT_TRUE(result.Dependencies().empty());
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            TextureSink sink = {};
            ASSERT_TRUE(Encode(sink).Succeeded());
            EXPECT_EQ(sink.Bytes, Capture.Bytes);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
    };

    TEST_F(CookTextureAdapterTest, PNGIsALeafWithSemanticMetadataAndExactPassthrough)
    {
        EXPECT_EQ(Adapter.Kind, CookAssetKind::Texture);
        EXPECT_EQ(Adapter.Revision, 1u);
        EXPECT_EQ(Adapter.Context, nullptr);
        ExpectRoundTrip("image-png");
        CookAssetInspection result = {};
        ASSERT_TRUE(Inspect(result).Succeeded());
        Digest256 key = {};
        ASSERT_TRUE(ComputeCookMetadataKey(Scratch, Metadata, &key).Succeeded());
        EXPECT_EQ(result.MetadataKey(), key);
    }

    TEST_F(CookTextureAdapterTest, DetectsContentRatherThanExtensionAndSupportsJPEGAndBMPAndTGA)
    {
        const uint8_t pixels[3] = {200, 100, 50};
        Input.Source            = VFS::VFSRelativePath::Parse("assets/not-a-texture.zemesh").Value();
        Capture.Bytes.clear();
        ASSERT_NE(stbi_write_jpg_to_func(AppendImage, &Capture.Bytes, 1, 1, 3, pixels, 90), 0);
        ExpectRoundTrip("image-jpeg");
        // JPEG marker fill is accepted by the shared decoder and must remain
        // accepted by the allocation-free format discriminator too.
        Capture.Bytes.insert(Capture.Bytes.begin(), 7, uint8_t{0xff});
        Capture.Chunk = 1;
        ExpectRoundTrip("image-jpeg");
        Capture.Chunk = SIZE_MAX;
        Capture.Bytes.clear();
        ASSERT_NE(stbi_write_bmp_to_func(AppendImage, &Capture.Bytes, 1, 1, 3, pixels), 0);
        ExpectRoundTrip("image-bmp");
        Capture.Bytes.clear();
        ASSERT_NE(stbi_write_tga_to_func(AppendImage, &Capture.Bytes, 1, 1, 3, pixels), 0);
        ExpectRoundTrip("image-tga");
    }

    TEST_F(CookTextureAdapterTest, PNGSupportsAllRuntimeChannelCountsAndDoesNotInferAlphaBehavior)
    {
        constexpr uint8_t pixels[8] = {255, 0, 64, 0, 128, 255, 255, 255};
        for (int channels = 1; channels <= 4; ++channels)
        {
            SCOPED_TRACE(channels);
            Capture.Bytes.clear();
            ASSERT_NE(stbi_write_png_to_func(AppendImage, &Capture.Bytes, 2, 1, channels, pixels, channels * 2), 0);
            ExpectRoundTrip("image-png");
        }
    }

    TEST_F(CookTextureAdapterTest, TopDownBMPUsesAbsoluteHeightAndRejectsINTMINBeforeDecode)
    {
        constexpr uint8_t pixels[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
        Capture.Bytes.clear();
        ASSERT_NE(stbi_write_bmp_to_func(AppendImage, &Capture.Bytes, 2, 2, 3, pixels), 0);
        // BITMAPINFOHEADER's signed little-endian height at offset 22.
        Capture.Bytes[22] = 0xfe;
        Capture.Bytes[23] = Capture.Bytes[24] = Capture.Bytes[25] = 0xff;
        Limits.MaxDecodedAssetBytes                               = 16;
        Limits.MaxImageDimension                                  = 2;
        ExpectRoundTrip("image-bmp");
        Capture.Bytes[22] = Capture.Bytes[23] = Capture.Bytes[24] = 0;
        Capture.Bytes[25]                                         = 0x80;
        TextureFaultAllocator allocator                           = {};
        allocator.Parent                                          = &Scratch;
        CookAssetInspection result                                = {};
        EXPECT_EQ(Adapter.Inspect(nullptr, Input, allocator, &result, nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(allocator.Calls, 0u);
    }

    TEST_F(CookTextureAdapterTest, SupportsFirstFrameGIFAndUncompressedPSDAndPIC)
    {
        // One-pixel GIF89a with one image frame and its trailer.
        Capture.Bytes = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0, 0, 0, 0, 255, 255, 255, 0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 1, 0, 0x3b};
        ExpectRoundTrip("image-gif");
        // PSD v1, three channels, 1x1 RGB8, no optional blocks, no compression.
        Capture.Bytes = {'8', 'B', 'P', 'S', 0, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 1, 0, 8, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 200, 100, 50};
        ExpectRoundTrip("image-psd");
        Capture.Bytes.assign(104, 0);
        Capture.Bytes[0]  = 0x53;
        Capture.Bytes[1]  = 0x80;
        Capture.Bytes[2]  = 0xf6;
        Capture.Bytes[3]  = 0x34;
        Capture.Bytes[88] = 'P';
        Capture.Bytes[89] = 'I';
        Capture.Bytes[90] = 'C';
        Capture.Bytes[91] = 'T';
        Capture.Bytes[93] = 1;
        Capture.Bytes[95] = 1;
        Capture.Bytes.insert(Capture.Bytes.end(), {0, 8, 0, 0xe0, 200, 100, 50});
        ExpectRoundTrip("image-pic");
    }

    TEST_F(CookTextureAdapterTest, RepeatedShortReadsAndAssetsReuseOneBackingBlock)
    {
        Capture.Chunk = 1;
        for (uint32_t i = 0; i < 128; ++i)
        {
            SCOPED_TRACE(i);
            ExpectRoundTrip("image-png");
        }
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookTextureAdapterTest, LargeImageDecodesWithoutAnEncodedCopyAndStreamsWithLateSinkFailures)
    {
        // Encoded BMP (~120 KiB) + RGBA pixels (~160 KiB) cannot coexist in
        // this fixture's 256 KiB pool. Decode directly from borrowed reads.
        std::vector<uint8_t> pixels(200 * 200 * 3, 127);
        Capture.Bytes.clear();
        ASSERT_NE(stbi_write_bmp_to_func(AppendImage, &Capture.Bytes, 200, 200, 3, pixels.data()), 0);
        ASSERT_GT(Capture.Bytes.size(), ZKilo(64));
        ASSERT_GT(Capture.Bytes.size() + 200 * 200 * 4, ZKilo(256));
        Limits.MaxDecodedAssetBytes = 200 * 200 * 4;
        Limits.MaxEncodedAssetBytes = Capture.Bytes.size();
        ExpectRoundTrip("image-bmp");
        TextureSink sink = {};
        sink.FailAt      = 2;
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(sink.Calls, 2u);
        EXPECT_EQ(sink.Bytes.size(), ZKilo(64));
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookTextureAdapterTest, ActualDimensionCapAcceptsHorizontalAndVerticalStripsAndRejectsOneAbove)
    {
        constexpr uint32_t maximum = 16384;
        ASSERT_EQ(Limits.MaxImageDimension, maximum);
        Core::Containers::FixedArray<uint8_t, (maximum + 1) * 4> pixels = {};
        std::fill(pixels.begin(), pixels.end(), uint8_t{127});
        for (uint32_t format = 0; format < 3; ++format)
        {
            SCOPED_TRACE(format);
            for (bool vertical : {false, true})
            {
                SCOPED_TRACE(vertical);
                for (uint32_t length : {maximum, maximum + 1})
                {
                    SCOPED_TRACE(length);
                    const int width  = static_cast<int>(vertical ? 1 : length);
                    const int height = static_cast<int>(vertical ? length : 1);
                    Capture.Bytes.clear();
                    if (!format)
                    {
                        // RGB keeps PNG's two filter scanlines plus inflate/
                        // RGBA conversion inside this fixture's 256 KiB pool.
                        ASSERT_NE(stbi_write_png_to_func(AppendImage, &Capture.Bytes, width, height, 3, pixels.data(), width * 3), 0);
                    }
                    else if (format == 1)
                    {
                        ASSERT_NE(stbi_write_bmp_to_func(AppendImage, &Capture.Bytes, width, height, 4, pixels.data()), 0);
                    }
                    else
                    {
                        ASSERT_NE(stbi_write_tga_to_func(AppendImage, &Capture.Bytes, width, height, 4, pixels.data()), 0);
                    }
                    Limits.MaxEncodedAssetBytes = Capture.Bytes.size();
                    Limits.MaxDecodedAssetBytes = uint64_t(length) * 4;
                    if (length == maximum)
                    {
                        ExpectRoundTrip(!format ? "image-png" : format == 1 ? "image-bmp" : "image-tga");
                    }
                    else
                    {
                        CookAssetInspection result = {};
                        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
                        TextureSink sink = {};
                        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::SizeLimitExceeded);
                        EXPECT_EQ(sink.Calls, 0u);
                    }
                    EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
                }
            }
        }
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookTextureAdapterTest, PNG16PreflightsItsNativePixelStorageBeforeDecode)
    {
        // One RGBA16 pixel, with a stored DEFLATE block and valid chunk CRCs.
        Capture.Bytes = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 1, 0, 0, 0, 1, 16, 6, 0, 0, 0, 0x4f, 0x85, 0x18, 0xca, 0, 0, 0, 20, 'I', 'D', 'A', 'T', 0x78, 0x01, 0x01, 0x09, 0, 0xf6, 0xff, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x23, 0xe5, 0x07, 0xf9, 0x75, 0xf6, 0x33, 0xb6, 0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82,
        };
        Limits.MaxDecodedAssetBytes = 7;
        CookAssetInspection result  = {};
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxDecodedAssetBytes = 8;
        ExpectRoundTrip("image-png");
    }

    TEST_F(CookTextureAdapterTest, JPEGAndBMPAndTGATruncatedPixelPayloadsAreRejectedBeforeOutput)
    {
        const uint8_t pixels[3] = {200, 100, 50};
        for (uint32_t format = 0; format < 3; ++format)
        {
            Capture.Bytes.clear();
            if (!format)
            {
                ASSERT_NE(stbi_write_jpg_to_func(AppendImage, &Capture.Bytes, 1, 1, 3, pixels, 90), 0);
            }
            else if (format == 1)
            {
                ASSERT_NE(stbi_write_bmp_to_func(AppendImage, &Capture.Bytes, 1, 1, 3, pixels), 0);
            }
            else
            {
                ASSERT_NE(stbi_write_tga_to_func(AppendImage, &Capture.Bytes, 1, 1, 3, pixels), 0);
            }
            const auto complete = Capture.Bytes;
            for (size_t length = 0; length < complete.size(); ++length)
            {
                SCOPED_TRACE(format);
                SCOPED_TRACE(length);
                Capture.Bytes.assign(complete.begin(), complete.begin() + length);
                TextureSink sink = {};
                EXPECT_TRUE(Encode(sink).Failed());
                EXPECT_EQ(sink.Calls, 0u);
                EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            }
        }
    }

    TEST_F(CookTextureAdapterTest, EveryTruncatedPNGIsRejectedBeforeOutputAndPreservesInspection)
    {
        CookAssetInspection result = {};
        ASSERT_TRUE(Inspect(result).Succeeded());
        const auto previous = result.LocalContentKey();
        for (size_t size = 0; size < sizeof(Png); ++size)
        {
            SCOPED_TRACE(size);
            Capture.Bytes.assign(Png, Png + size);
            EXPECT_TRUE(Inspect(result).Failed());
            EXPECT_STREQ(result.Format(), "image-png");
            EXPECT_EQ(result.LocalContentKey(), previous);
            TextureSink sink = {};
            EXPECT_TRUE(Encode(sink).Failed());
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookTextureAdapterTest, AllDecoderAndStreamAllocationRefusalsRollBackWithoutHeapFallback)
    {
        TextureFaultAllocator allocator = {};
        allocator.Parent                = &Scratch;
        TextureSink baseline            = {};
        ASSERT_TRUE(Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, baseline, allocator, nullptr).Succeeded());
        const auto attempts = allocator.Calls;
        ASSERT_GT(attempts, 1u);
        for (uint32_t failure = 1; failure <= attempts; ++failure)
        {
            SCOPED_TRACE(failure);
            allocator.Calls     = 0;
            allocator.FailAt    = failure;
            TextureSink sink    = {};
            const auto  encoded = Adapter.Encode(nullptr, Input, {}, CookTarget::DesktopVulkanLE64, sink, allocator, nullptr);
            ASSERT_TRUE(encoded.Failed());
            EXPECT_EQ(encoded.Error(), VFS::VFSError::OutOfMemory);
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookTextureAdapterTest, EncodedDimensionsAndDecodedBudgetsAreEnforced)
    {
        CookAssetInspection result  = {};
        Limits.MaxEncodedAssetBytes = sizeof(Png) - 1;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxEncodedAssetBytes = sizeof(Png);
        Limits.MaxDecodedAssetBytes = 3;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxDecodedAssetBytes = 4;
        ASSERT_TRUE(Inspect(result).Succeeded());
        // PNG IHDR width = 2; shared header preflight runs before decoding.
        Capture.Bytes[19]        = 2;
        Limits.MaxImageDimension = 1;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxImageDimension    = UINT32_MAX;
        Limits.MaxDecodedAssetBytes = UINT64_MAX;
        Capture.Bytes[16]           = 0x7f;
        Capture.Bytes[17]           = 0xff;
        Capture.Bytes[18]           = 0xff;
        Capture.Bytes[19]           = 0xff;
        EXPECT_TRUE(Inspect(result).Failed());
    }

    TEST_F(CookTextureAdapterTest, ZeroResourceCapsAndOversizedProviderFailBeforeAnyReadOrAllocation)
    {
        CookAssetInspection result = {};
        Limits.MaxImageDimension   = 0;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxImageDimension    = 1;
        Limits.MaxDecodedAssetBytes = 0;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxDecodedAssetBytes = 4;
        Limits.MaxEncodedAssetBytes = 0;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxEncodedAssetBytes = UINT64_MAX;
        Capture.AdvertisedSize      = UINT64_MAX - 1;
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Capture.Reads, 0u);
    }

    TEST_F(CookTextureAdapterTest, UnsupportedContentAndInvalidPixelsCannotBePassedThrough)
    {
        const std::vector<std::vector<uint8_t>> invalid = {
            {'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 200, 100, 50},
            {'#', '?', 'R', 'A', 'D', 'I', 'A', 'N', 'C', 'E', '\n'},
            {0x76, 0x2f, 0x31, 0x01, 0, 0, 0, 0},
            {'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e'},
        };
        for (const auto& bytes : invalid)
        {
            Capture.Bytes    = bytes;
            TextureSink sink = {};
            EXPECT_TRUE(Encode(sink).Failed());
            EXPECT_EQ(sink.Calls, 0u);
        }
        Capture.Bytes.assign(std::begin(Png), std::end(Png));
        Capture.Bytes[41] ^= 0xff; // Invalid DEFLATE payload; header still probes.
        TextureSink sink   = {};
        EXPECT_TRUE(Encode(sink).Failed());
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST_F(CookTextureAdapterTest, ProviderFailuresAtEveryReadAndDishonestRangesCannotSucceed)
    {
        TextureSink baseline = {};
        ASSERT_TRUE(Encode(baseline).Succeeded());
        const auto reads = Capture.Reads;
        for (uint32_t failure = 1; failure <= reads; ++failure)
        {
            SCOPED_TRACE(failure);
            Capture.Reads       = 0;
            Capture.FailAt      = failure;
            TextureSink sink    = {};
            const auto  encoded = Encode(sink);
            EXPECT_TRUE(encoded.Failed());
            EXPECT_EQ(encoded.Error(), VFS::VFSError::PermissionDenied);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        Capture.FailAt             = UINT32_MAX;
        Capture.Overreport         = true;
        CookAssetInspection result = {};
        EXPECT_EQ(Inspect(result).Error(), VFS::VFSError::Corrupted);
        Capture.Overreport     = false;
        Capture.AdvertisedSize = Capture.Bytes.size() + 1;
        TextureSink sink       = {};
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::Corrupted);
    }

    TEST_F(CookTextureAdapterTest, BadDigestAndSinkFailureRemainPrecommitFailures)
    {
        Capture.WrongDigest = true;
        TextureSink sink    = {};
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::Corrupted);
        Capture.WrongDigest = false;
        sink                = {};
        sink.FailAt         = 1;
        EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(sink.Calls, 1u);
    }

    TEST_F(CookTextureAdapterTest, InvalidRequestsMetadataTargetsAndDependenciesAreRejectedWithoutReads)
    {
        CookAssetInspection result = {};
        auto                bad    = Input;
        bad.Identity.Kind          = CookAssetKind::Mesh;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        bad                   = Input;
        bad.EnvironmentSource = &Capture;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        bad          = Input;
        bad.Artifact = nullptr;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        bad          = Input;
        bad.Metadata = nullptr;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        auto metadata           = Metadata;
        metadata.Data.AssetUUID = {};
        bad                     = Input;
        bad.Metadata            = &metadata;
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        constexpr cstring settings = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"TextureImporter","settings":[{"key":"unknown","value":"true"}]})";
        ASSERT_TRUE(VFS::MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(settings), Helpers::secure_strlen(settings)}, &metadata).Succeeded());
        EXPECT_TRUE(Adapter.Inspect(nullptr, bad, Scratch, &result, nullptr).Failed());
        EXPECT_TRUE(Adapter.Inspect(nullptr, Input, Scratch, nullptr, nullptr).Failed());
        TextureSink sink = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {}, static_cast<CookTarget>(255), sink, Scratch, nullptr).Error(), VFS::VFSError::Unsupported);
        CookResolvedDependency child = {};
        EXPECT_EQ(Adapter.Encode(nullptr, Input, {&child, 1}, CookTarget::DesktopVulkanLE64, sink, Scratch, nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Capture.Reads, 0u);
        EXPECT_EQ(sink.Calls, 0u);
    }
} // namespace ZEngine::Cook
