#include <iostream>
#include "CookResourceTestSupport.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        using VerifyingTextureSink = Tests::VerifyingResourceSink;

        struct GeneratedTextureCapture final : Tests::GeneratedResourceCapture
        {
            void Configure(uint16_t width, uint16_t height, bool compressed)
            {
                ASSERT_GT(width, 0u);
                ASSERT_GT(height, 0u);
                ASSERT_EQ(width % 128, 0u);
                Reset();
                Core::Containers::FixedArray<uint8_t, 18> header       = {};
                header[2]                                              = compressed ? 10 : 2;
                header[12]                                             = static_cast<uint8_t>(width);
                header[13]                                             = static_cast<uint8_t>(width >> 8);
                header[14]                                             = static_cast<uint8_t>(height);
                header[15]                                             = static_cast<uint8_t>(height >> 8);
                header[16]                                             = 32;
                header[17]                                             = 0x28; // Top-left origin, eight alpha bits.
                const Core::Containers::FixedArray<uint8_t, 5> pattern = compressed ? decltype(pattern){0xff, 11, 42, 93, 255} : decltype(pattern){11, 42, 93, 255, 0};
                const size_t                                   period  = compressed ? 5 : 4;
                Append(header, header.size());
                Append({pattern.data(), period}, uint64_t(width) * height / (compressed ? 128 : 1) * period);
            }
        };
    } // namespace

    class SceneCookResourceProcessTest : public Tests::ResourceProcessTest
    {
    protected:
        GeneratedTextureCapture Capture = {};

        void                    SetUp() override
        {
            ResourceProcessTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            constexpr cstring meta = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"TextureImporter","settings":[]})";
            Adapter                = CookTextureAdapter();
            InitializeInput(CookAssetKind::Texture, meta, Capture, "assets/generated.tga");
        }
    };

    TEST_F(SceneCookResourceProcessTest, ActualDecodedCapAcceptsExactWorkingPeakAndRejectsOneByteLessBeforeMapping)
    {
        // Exactly 512 MiB of native RGBA pixels, encoded in about 5 MiB. Do
        // not replace with a small configured cap: this is the hard-cap gate.
        ASSERT_EQ(Limits.MaxDecodedAssetBytes, ZMega(512));
        ASSERT_EQ(Limits.MaxImageDimension, 16384u);
        Capture.Configure(16384, 8192, true);
        Capture.HashContents();
        ASSERT_EQ(uint64_t(16384) * 8192 * 4, Limits.MaxDecodedAssetBytes);
        ASSERT_LT(Capture.Size(), ZMega(6));
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Inspect(inspection).Succeeded());
        EXPECT_STREQ(inspection.Format(), "image-tga");
        EXPECT_EQ(inspection.LocalContentKey(), Capture.Digest());
        ExpectDrained();
        VerifyingTextureSink first = {};
        ASSERT_TRUE(Encode(first).Succeeded());
        EXPECT_EQ(first.Bytes, Capture.Size());
        EXPECT_GT(first.Calls, 1u);
        ExpectDrained();

        // Real native mapping headers/page rounding and existing principal
        // fixed reservations, not a complete compiler-stack/RSS measurement.
        const uint64_t peak = Budget.Statistics().PeakBytes;
        EXPECT_GT(peak, Limits.MaxDecodedAssetBytes);
        EXPECT_LT(peak, Limits.MaxDecodedAssetBytes + ZMega(1));
        const auto allocations = Native.AllocationCount();
        ResetBudget(peak - 1);
        EXPECT_EQ(Inspect(inspection).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(inspection.LocalContentKey(), Capture.Digest());
        VerifyingTextureSink refused = {};
        EXPECT_EQ(Encode(refused).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(refused.Calls, 0u);
        EXPECT_EQ(Native.AllocationCount(), allocations);
        ExpectDrained();

        ResetBudget(peak);
        VerifyingTextureSink exact = {};
        ASSERT_TRUE(Encode(exact).Succeeded());
        EXPECT_EQ(exact.Bytes, first.Bytes);
        EXPECT_EQ(exact.Calls, first.Calls);
        EXPECT_EQ(Budget.Statistics().PeakBytes, peak);
        ExpectDrained();
        std::cout << "Decoded-cap texture: " << Capture.Size() << " encoded bytes, " << Limits.MaxDecodedAssetBytes << " decoded bytes, " << peak << " modeled working bytes\n";
    }

    TEST_F(SceneCookResourceProcessTest, LowerDecodedAndDimensionLimitsAndOverCapAssetsPreservePriorInspection)
    {
        Capture.Configure(128, 1, true);
        Capture.HashContents();
        CookAssetInspection inspection = {};
        ASSERT_TRUE(Inspect(inspection).Succeeded());
        const auto previous    = inspection.LocalContentKey();
        const auto allocations = Native.AllocationCount();
        const auto reject      = [&]() {
            EXPECT_EQ(Inspect(inspection).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_STREQ(inspection.Format(), "image-tga");
            EXPECT_EQ(inspection.LocalContentKey(), previous);
            VerifyingTextureSink sink = {};
            EXPECT_EQ(Encode(sink).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(Native.AllocationCount(), allocations);
            ExpectDrained();
        };
        Capture.Configure(16384, 8192, true);
        --Limits.MaxDecodedAssetBytes;
        reject();
        ++Limits.MaxDecodedAssetBytes;
        --Limits.MaxImageDimension;
        reject();
        ++Limits.MaxImageDimension;
        Capture.Configure(16384, 8193, true);
        reject();
        Capture.Configure(16512, 1, true);
        reject();
    }

    TEST_F(SceneCookResourceProcessTest, NearEncodedCapStreamsWithoutASecondImageAndLowerCapsRejectBeforeOutput)
    {
        // A valid, uncompressed image within 64 KiB of BOTH real 512 MiB
        // caps. The input and sink remain constant-sized generated ranges.
        Capture.Configure(16384, 8191, false);
        ASSERT_EQ(Limits.MaxEncodedAssetBytes, ZMega(512));
        ASSERT_GT(Capture.Size(), Limits.MaxEncodedAssetBytes - ZKilo(64));
        ASSERT_LT(Capture.Size(), Limits.MaxEncodedAssetBytes);
        const auto allocations       = Native.AllocationCount();
        const auto reads             = Capture.Reads;
        Limits.MaxEncodedAssetBytes  = Capture.Size() - 1;
        VerifyingTextureSink refused = {};
        EXPECT_EQ(Encode(refused).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(refused.Calls, 0u);
        EXPECT_EQ(Capture.Reads, reads);
        EXPECT_EQ(Native.AllocationCount(), allocations);
        Limits.MaxEncodedAssetBytes = Capture.Size();
        Limits.MaxDecodedAssetBytes = uint64_t(16384) * 8191 * 4 - 1;
        EXPECT_EQ(Encode(refused).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(refused.Calls, 0u);
        EXPECT_EQ(Native.AllocationCount(), allocations);
        ++Limits.MaxDecodedAssetBytes;
        Capture.HashContents();
        VerifyingTextureSink sink = {};
        ASSERT_TRUE(Encode(sink).Succeeded());
        EXPECT_EQ(sink.Bytes, Capture.Size());
        EXPECT_EQ(sink.Calls, (Capture.Size() + ZKilo(64) - 1) / ZKilo(64));
        EXPECT_LT(Budget.Statistics().PeakBytes, ZMega(513));
        ExpectDrained();
        std::cout << "Near-encoded-cap texture: " << Capture.Size() << " streamed bytes, " << Budget.Statistics().PeakBytes << " modeled working bytes\n";
    }
} // namespace ZEngine::Cook

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    if (GTEST_FLAG_GET(repeat) != 1 || GTEST_FLAG_GET(list_tests))
    {
        return 1;
    }
    const int   result = RUN_ALL_TESTS();
    const auto* tests  = testing::UnitTest::GetInstance();
    // This is an acceptance process, not an optionally filtered unit shard.
    // Inherited GTEST_FILTER/repeat settings must not silently skip its gates.
    return result || !tests->total_test_count() || tests->successful_test_count() != tests->total_test_count() ? 1 : 0;
}
