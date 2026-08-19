#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "HeadlessWindow.h"

using namespace ZEngine;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Hardwares;

class SwapchainResizeFixture : public ::testing::Test
{
protected:
    static constexpr uint32_t kWidth  = 320;
    static constexpr uint32_t kHeight = 240;

    MemoryManager             m_mem;
    Tests::HeadlessWindow*    m_window = nullptr;
    VulkanDevice*             m_device = nullptr;
    bool                      m_ok     = false;

    void                      SetUp() override
    {
        if (!Tests::HeadlessWindow::IsSupported())
            GTEST_SKIP() << "VK_EXT_headless_surface not supported on this system";

        m_mem.Initialize(ZMega(128), {});
        m_window = ZPushStructCtorArgs(&m_mem.MainArena, Tests::HeadlessWindow, &m_mem.MainArena, kWidth, kHeight);
        m_device = ZPushStructCtor(&m_mem.MainArena, VulkanDevice);
        m_device->Initialize(&m_mem.MainArena, m_window, 1);
        m_ok = true;
    }

    void TearDown() override
    {
        if (m_ok && m_device)
            m_device->Deinitialize();
        m_mem.Shutdown();
    }

    DeviceSwapchain* Swapchain() const
    {
        return m_device->SwapchainPtr;
    }

    bool RunMinimalFrame(uint32_t frame_idx = 0)
    {
        auto* sc = Swapchain();
        sc->AcquireNextImage(frame_idx % sc->BufferredFrameCount);
        if (!sc->IsFrameValid())
        {
            sc->Present();
            return false;
        }
        sc->Present();
        return true;
    }
};

TEST_F(SwapchainResizeFixture, NormalFrame_Succeeds)
{
    EXPECT_TRUE(RunMinimalFrame());
    EXPECT_EQ(Swapchain()->Recreation, RecreationState::None);
    EXPECT_TRUE(Swapchain()->IsFrameValid());
}

TEST_F(SwapchainResizeFixture, PendingRecreation_CompletesOnNextAcquire)
{
    ASSERT_TRUE(RunMinimalFrame(0));

    Swapchain()->ForceRecreation(RecreationState::Pending);
    RunMinimalFrame(1);

    EXPECT_EQ(Swapchain()->Recreation, RecreationState::None);
    EXPECT_TRUE(Swapchain()->IsFrameValid());
}

TEST_F(SwapchainResizeFixture, FrameAborted_PresentSkipsGPUWork)
{
    ASSERT_TRUE(RunMinimalFrame(0));

    Swapchain()->ForceRecreation(RecreationState::FrameAborted);
    EXPECT_FALSE(Swapchain()->IsFrameValid());

    Swapchain()->Present();
    EXPECT_EQ(Swapchain()->Recreation, RecreationState::FrameAborted);

    RunMinimalFrame(1);
    EXPECT_EQ(Swapchain()->Recreation, RecreationState::None);
    EXPECT_TRUE(Swapchain()->IsFrameValid());
}

TEST_F(SwapchainResizeFixture, RecreationFiresOnSwapchainResizedCallback)
{
    uint32_t cb_w = 0, cb_h = 0;
    bool     fired                  = false;

    Swapchain()->OnSwapchainResized = [](uint32_t w, uint32_t h, void* ctx) {
        auto* p          = static_cast<std::tuple<uint32_t*, uint32_t*, bool*>*>(ctx);
        *std::get<0>(*p) = w;
        *std::get<1>(*p) = h;
        *std::get<2>(*p) = true;
    };
    auto ctx                           = std::make_tuple(&cb_w, &cb_h, &fired);
    Swapchain()->OnSwapchainResizedCtx = &ctx;

    ASSERT_TRUE(RunMinimalFrame(0));
    Swapchain()->ForceRecreation(RecreationState::Pending);
    RunMinimalFrame(1);

    EXPECT_TRUE(fired);
    EXPECT_GT(cb_w, 0u);
    EXPECT_GT(cb_h, 0u);
}

TEST_F(SwapchainResizeFixture, RapidRecreation_TenCycles_NoHang)
{
    for (int i = 0; i < 10; ++i)
    {
        ASSERT_TRUE(RunMinimalFrame(static_cast<uint32_t>(i * 2))) << "cycle " << i;
        Swapchain()->ForceRecreation(RecreationState::Pending);
        RunMinimalFrame(static_cast<uint32_t>(i * 2 + 1));
        ASSERT_EQ(Swapchain()->Recreation, RecreationState::None) << "cycle " << i;
    }
}

// 100 cycles crosses 3 full pool rotations — needed to surface latent
// fence-tracking bugs that only appear after several rotations.
TEST_F(SwapchainResizeFixture, AggressiveRecreation_HundredCycles_NoHang)
{
    for (int i = 0; i < 100; ++i)
    {
        ASSERT_TRUE(RunMinimalFrame(static_cast<uint32_t>(i * 2))) << "cycle " << i;
        Swapchain()->ForceRecreation(RecreationState::Pending);
        RunMinimalFrame(static_cast<uint32_t>(i * 2 + 1));
        ASSERT_EQ(Swapchain()->Recreation, RecreationState::None) << "cycle " << i;
    }
}

// Mirrors the Intel/Ubuntu hang: OOD at acquire (FrameAborted) immediately
// followed by OOD at present (Pending) in the next frame boundary.
TEST_F(SwapchainResizeFixture, AlternatingAbortAndPending_NoHang)
{
    for (int i = 0; i < 20; ++i)
    {
        ASSERT_TRUE(RunMinimalFrame(static_cast<uint32_t>(i * 3))) << "cycle " << i;

        Swapchain()->ForceRecreation(RecreationState::FrameAborted);
        Swapchain()->Present();

        Swapchain()->AcquireNextImage(static_cast<uint32_t>((i * 3 + 1) % Swapchain()->BufferredFrameCount));

        if (Swapchain()->IsFrameValid())
        {
            Swapchain()->ForceRecreation(RecreationState::Pending);
            RunMinimalFrame(static_cast<uint32_t>(i * 3 + 2));
        }

        ASSERT_EQ(Swapchain()->Recreation, RecreationState::None) << "cycle " << i;
        ASSERT_TRUE(Swapchain()->IsFrameValid()) << "cycle " << i;
    }
}

// All ImageInFlights are null after consecutive aborts — tests fence drain
// when no GPU work was ever submitted before recreation.
TEST_F(SwapchainResizeFixture, ConsecutiveFrameAborts_ThenRecovery)
{
    ASSERT_TRUE(RunMinimalFrame(0));

    for (int i = 0; i < 5; ++i)
    {
        Swapchain()->ForceRecreation(RecreationState::FrameAborted);
        Swapchain()->Present();
    }

    RunMinimalFrame(1);
    EXPECT_EQ(Swapchain()->Recreation, RecreationState::None);
    EXPECT_TRUE(Swapchain()->IsFrameValid());

    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(RunMinimalFrame(static_cast<uint32_t>(i + 2))) << "recovery frame " << i;
}

TEST_F(SwapchainResizeFixture, SizeChangingRecreations_NoHang)
{
    static constexpr uint32_t kSizes[][2] = {
        { 160, 120},
        { 320, 240},
        { 640, 480},
        {1280, 720},
        { 640, 480},
        { 320, 240},
        { 160, 120},
        { 320, 240},
    };

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(RunMinimalFrame(static_cast<uint32_t>(i * 2))) << "cycle " << i;

        m_window->SetSize(kSizes[i][0], kSizes[i][1]);
        Swapchain()->ForceRecreation(RecreationState::Pending);
        RunMinimalFrame(static_cast<uint32_t>(i * 2 + 1));

        ASSERT_EQ(Swapchain()->Recreation, RecreationState::None) << "cycle " << i;
        EXPECT_EQ(Swapchain()->SwapchainImageWidth, kSizes[i][0]) << "cycle " << i;
        EXPECT_EQ(Swapchain()->SwapchainImageHeight, kSizes[i][1]) << "cycle " << i;
    }
}

TEST_F(SwapchainResizeFixture, MultipleMinimizeRestore_NoHang)
{
    for (int i = 0; i < 10; ++i)
    {
        ASSERT_TRUE(RunMinimalFrame(static_cast<uint32_t>(i * 2))) << "cycle " << i;

        m_window->SetSize(0, 0);
        Swapchain()->ForceRecreation(RecreationState::Pending);
        Swapchain()->AcquireNextImage(static_cast<uint32_t>((i * 2) % Swapchain()->BufferredFrameCount));

        ASSERT_EQ(Swapchain()->Recreation, RecreationState::Pending) << "cycle " << i;
        ASSERT_EQ(Swapchain()->SwapchainHandle, VK_NULL_HANDLE) << "cycle " << i;

        m_window->SetSize(kWidth, kHeight);
        RunMinimalFrame(static_cast<uint32_t>(i * 2 + 1));

        ASSERT_EQ(Swapchain()->Recreation, RecreationState::None) << "cycle " << i;
        ASSERT_NE(Swapchain()->SwapchainHandle, VK_NULL_HANDLE) << "cycle " << i;
    }
}

TEST_F(SwapchainResizeFixture, ZeroSizeGuard_SkipsRecreationUntilNonZero)
{
    ASSERT_TRUE(RunMinimalFrame(0));

    m_window->SetSize(0, 0);
    Swapchain()->ForceRecreation(RecreationState::Pending);
    Swapchain()->AcquireNextImage(0);

    EXPECT_EQ(Swapchain()->Recreation, RecreationState::Pending);
    EXPECT_EQ(Swapchain()->SwapchainHandle, VK_NULL_HANDLE);

    m_window->SetSize(kWidth, kHeight);
    RunMinimalFrame(0);

    EXPECT_EQ(Swapchain()->Recreation, RecreationState::None);
    EXPECT_NE(Swapchain()->SwapchainHandle, VK_NULL_HANDLE);
    EXPECT_TRUE(Swapchain()->IsFrameValid());
}
