#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Renderers/ZUIPass.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::UI;

namespace
{
    ZUIRenderPayload Payload(float width, float height)
    {
        ZUIRenderPayload payload{};
        payload.Scale[0]     = 2.f / width;
        payload.Scale[1]     = 2.f / height;
        payload.Translate[0] = payload.Translate[1] = -1.f;
        return payload;
    }

    ZUIDrawListCmd Clip(float x, float y, float width, float height)
    {
        ZUIDrawListCmd cmd{};
        cmd.ClipX = x;
        cmd.ClipY = y;
        cmd.ClipW = width;
        cmd.ClipH = height;
        return cmd;
    }
} // namespace

TEST(ZUIRenderScaleTest, ClipUsesActualSwapchainRatherThanWindowDpi)
{
    const auto payload = Payload(1982, 1211);
    const auto cmd     = Clip(280, 50, 100, 20);
    // Regression: window framebuffer=2478x1514, but old swapchain=1500x800.
    // The old 1.25 multiplier produced x=350; projected geometry starts at x=212.
    const auto rect    = payload.GetScissorRect(cmd, {1500, 800});
    EXPECT_EQ(rect.offset.x, 211);
    EXPECT_EQ(rect.offset.y, 33);
    EXPECT_EQ(rect.extent.width, 77u);
    EXPECT_EQ(rect.extent.height, 14u);
}

TEST(ZUIRenderScaleTest, FractionalDpiRoundsOutwardPerAxis)
{
    const auto rect = Payload(1000, 800).GetScissorRect(Clip(10.2f, 20.2f, 30.1f, 40.1f), {1250, 1001});
    EXPECT_EQ(rect.offset.x, 12);
    EXPECT_EQ(rect.offset.y, 25);
    EXPECT_EQ(rect.extent.width, 39u);
    EXPECT_EQ(rect.extent.height, 51u);
}

TEST(ZUIRenderScaleTest, NativeAndRetinaScissorsStayAligned)
{
    const auto payload = Payload(1500, 800);
    const auto cmd     = Clip(280, 50, 100, 20);
    const auto native  = payload.GetScissorRect(cmd, {1500, 800});
    EXPECT_EQ(native.offset.x, 280);
    EXPECT_EQ(native.offset.y, 50);
    EXPECT_EQ(native.extent.width, 100u);
    EXPECT_EQ(native.extent.height, 20u);

    const auto retina = payload.GetScissorRect(cmd, {3000, 1600});
    EXPECT_EQ(retina.offset.x, 560);
    EXPECT_EQ(retina.offset.y, 100);
    EXPECT_EQ(retina.extent.width, 200u);
    EXPECT_EQ(retina.extent.height, 40u);
}

TEST(ZUIRenderScaleTest, FullScreenClipCoversRoundedFractionalFramebuffer)
{
    const auto rect = Payload(1982, 1211).GetScissorRect(Clip(0, 0, 1982, 1211), {2478, 1514});
    EXPECT_EQ(rect.offset.x, 0);
    EXPECT_EQ(rect.offset.y, 0);
    EXPECT_EQ(rect.extent.width, 2478u);
    EXPECT_EQ(rect.extent.height, 1514u);
}

TEST(ZUIRenderScaleTest, ClampsPartiallyAndFullyOffscreenClips)
{
    const auto payload = Payload(100, 100);
    auto       rect    = payload.GetScissorRect(Clip(-10, -20, 50, 60), {200, 200});
    EXPECT_EQ(rect.offset.x, 0);
    EXPECT_EQ(rect.offset.y, 0);
    EXPECT_EQ(rect.extent.width, 80u);
    EXPECT_EQ(rect.extent.height, 80u);

    rect = payload.GetScissorRect(Clip(90, 90, 50, 60), {200, 200});
    EXPECT_EQ(rect.offset.x, 180);
    EXPECT_EQ(rect.offset.y, 180);
    EXPECT_EQ(rect.extent.width, 20u);
    EXPECT_EQ(rect.extent.height, 20u);

    rect = payload.GetScissorRect(Clip(110, 110, 10, 10), {200, 200});
    EXPECT_EQ(rect.extent.width, 0u);
    EXPECT_EQ(rect.extent.height, 0u);
    rect = payload.GetScissorRect(Clip(-50, -50, 10, 10), {200, 200});
    EXPECT_EQ(rect.extent.width, 0u);
    EXPECT_EQ(rect.extent.height, 0u);
}

TEST(ZUIRenderScaleTest, EmptyOrUninitializedPayloadDoesNotDraw)
{
    const auto payload = Payload(100, 100);
    EXPECT_EQ(payload.GetScissorRect(Clip(0, 0, 0, 10), {200, 200}).extent.width, 0u);
    EXPECT_EQ(payload.GetScissorRect(Clip(0, 0, 10, -1), {200, 200}).extent.height, 0u);
    EXPECT_EQ(payload.GetScissorRect(Clip(0, 0, 10, 10), {0, 0}).extent.width, 0u);
    EXPECT_EQ(ZUIRenderPayload{}.GetScissorRect(Clip(0, 0, 10, 10), {200, 200}).extent.width, 0u);
}

TEST(ZUIRenderScaleTest, HoveringScrollWhitespaceDoesNotPaintARegionSizedOverlay)
{
    ZEngine::Core::Memory::MemoryManager  memory = {};
    ZEngine::Core::Memory::ArenaAllocator arena  = {};
    ZUIContext                            ctx    = {};
    memory.Initialize(ZMega(8), {});
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(2), ZMega(1), 128, 128);
    memory.MainArena.CreateSubArena(ZMega(2), &arena, "ScrollHover/Payload");
    ctx.ScreenW = ctx.ScreenH = 200;
    ctx.MousePos[0] = ctx.MousePos[1] = 50.f;
    for (int frame = 0; frame < 5; ++frame)
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(200), ZPx(200));
        ZUIBeginScrollRegion(&ctx, "scroll", ZPx(100), ZPx(100));
        ZUISpacer(&ctx, 20.f);
        ZUIEndScrollRegion(&ctx);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    }
    const auto key = ZUIHashStr("scroll", 6);
    ASSERT_EQ(ctx.HotKey, key);
    ASSERT_GT(ZUIStateGetOrInsert(&ctx.StateStore, key)->HotT, .01f);
    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    pass.PreparePayload(&ctx, &payload, &arena);
    EXPECT_EQ(payload.IdxCount, 0u);
    ZUIContextDestroy(&ctx);
    arena.Shutdown();
}

TEST(ZUIRenderScaleTest, DragGhostDrawsAboveLaterChromeWithoutClippingOrCapturingDrops)
{
    ZEngine::Core::Memory::MemoryManager  memory        = {};
    ZEngine::Core::Memory::ArenaAllocator payload_arena = {};
    ZUIContext                            ctx           = {};
    memory.Initialize(ZMega(16), {});
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(2), ZMega(1), 128, 128);
    memory.MainArena.CreateSubArena(ZMega(2), &payload_arena, "ForegroundTest/Payload");
    ctx.ScreenW          = 400;
    ctx.ScreenH          = 300;
    ctx.Theme.PanelBg[0] = 0.f;
    ctx.Theme.PanelBg[1] = 0.f;
    ctx.Theme.PanelBg[2] = 1.f;
    ctx.Theme.PanelBg[3] = 1.f;
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    auto* root         = ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(300));
    auto* panel        = ZUIBeginColumn(&ctx, "panel", ZPx(200), ZPx(100));
    panel->Flags       = ZUI_FloatX | ZUI_FloatY | ZUI_ClipChildren;
    panel->FloatPos[0] = 50.f;
    panel->FloatPos[1] = 100.f;
    ZUIDockGhostHeader(&ctx, "ghost", "Importer", 200.f, 40.f);
    EXPECT_EQ(ctx.Current, panel);
    ASSERT_NE(ctx.ForegroundRoot, nullptr);
    auto* foreground = ctx.ForegroundRoot;
    auto* header     = foreground->LastChild;
    ASSERT_NE(header, nullptr);
    ZUIEndColumn(&ctx);
    // Reproduce the editor build order: shell chrome arrives AFTER the ghost.
    auto* chrome    = ZUIPushBox(&ctx, "chrome", 6, ZUI_DrawBackground | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY);
    chrome->Size[0] = ZPx(400);
    chrome->Size[1] = ZPx(100);
    ZUIBoxSetColor(chrome, 0.f, 1.f, 0.f, 1.f);
    ZUIPopBox(&ctx);
    ZUIEndColumn(&ctx);
    EXPECT_EQ(root->LastChild, chrome);
    ctx.MousePos[0]      = 200.f;
    ctx.MousePos[1]      = 40.f;
    ctx.DragSourceKey    = 1234;
    ctx.MouseReleased[0] = true;
    ZUIEndFrame(&ctx);
    EXPECT_EQ(root->LastChild, foreground);
    EXPECT_EQ(foreground->Parent, root);
    EXPECT_EQ(foreground->PrevSib, chrome);
    EXPECT_EQ(chrome->NextSib, foreground);
    EXPECT_EQ(foreground->NextSib, nullptr);
    EXPECT_FLOAT_EQ(header->ScreenMin[0], 200.f - header->ComputedSize[0] * 0.3f);
    EXPECT_FLOAT_EQ(header->ScreenMin[1], 40.f - ctx.Style.TabBarHeight * 0.5f);
    EXPECT_EQ(ctx.HotKey, chrome->Key);
    EXPECT_TRUE(ctx.DragDropFired);
    EXPECT_EQ(ctx.DragTargetKey, chrome->Key);

    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    const uint32_t green        = ZUIPackColor(0.f, 1.f, 0.f, 1.f);
    const uint32_t blue         = ZUIPackColor(0.f, 0.f, 1.f, 1.f);
    uint32_t       last_chrome  = 0;
    uint32_t       first_ghost  = payload.IdxCount;
    bool           found_chrome = false;
    for (uint32_t i = 0; i < payload.IdxCount; ++i)
    {
        const auto color = payload.Vtx[payload.Idx[i]].col;
        if (color == green)
        {
            last_chrome  = i;
            found_chrome = true;
        }
        if (color == blue && first_ghost == payload.IdxCount)
            first_ghost = i;
    }
    EXPECT_TRUE(found_chrome);
    ASSERT_LT(first_ghost, payload.IdxCount);
    EXPECT_GT(first_ghost, last_chrome);
    bool found_clip = false;
    for (uint32_t i = 0; i < payload.CmdCount; ++i)
    {
        const auto& cmd = payload.Cmds[i];
        if (first_ghost >= cmd.IdxOffset && first_ghost < cmd.IdxOffset + cmd.ElemCount)
        {
            found_clip = true;
            EXPECT_LE(cmd.ClipY, header->ScreenMin[1]);
            EXPECT_GE(cmd.ClipY + cmd.ClipH, header->ScreenMax[1]);
        }
    }
    EXPECT_TRUE(found_clip);
    ZUIContextDestroy(&ctx);
}

class ZUIShadowTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager  memory        = {};
    ZEngine::Core::Memory::ArenaAllocator payload_arena = {};
    ZUIContext                            ctx           = {};

    void                                  SetUp() override
    {
        memory.Initialize(ZMega(16), {});
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(4), ZMega(1), 2048, 2048);
        memory.MainArena.CreateSubArena(ZMega(4), &payload_arena, "ShadowTest/Payload");
        ctx.ScreenW = 400;
        ctx.ScreenH = 300;
        ZUIBeginFrame(&ctx, 1.f / 60.f);
    }

    void TearDown() override
    {
        ZUIContextDestroy(&ctx);
    }
};

TEST_F(ZUIShadowTest, DashedRoundedBorderIsBoundedBatchedAndRejectsInvalidGeometry)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &ctx.FrameArena, 4096, 8192, 0.f, 0.f, 0);
    ZUIDrawListAddRectDashed(&draw, 10.f, 20.f, 110.f, 60.f, 0xffffffffu, 6.f);
    EXPECT_GT(draw.VtxCount, 100u);
    EXPECT_LT(draw.VtxCount, 4096u);
    EXPECT_EQ(draw.CmdCount, 1u);
    for (uint32_t i = 0; i < draw.VtxCount; ++i)
    {
        EXPECT_TRUE(std::isfinite(draw.Vtx[i].x));
        EXPECT_TRUE(std::isfinite(draw.Vtx[i].y));
        EXPECT_GE(draw.Vtx[i].x, 9.f);
        EXPECT_LE(draw.Vtx[i].x, 111.f);
        EXPECT_GE(draw.Vtx[i].y, 19.f);
        EXPECT_LE(draw.Vtx[i].y, 61.f);
    }
    const auto count = draw.VtxCount;
    ZUIDrawListAddRectDashed(&draw, 0.f, 0.f, 0.f, 30.f, 0xffffffffu);
    ZUIDrawListAddRectDashed(&draw, 0.f, 0.f, 100.f, 30.f, 0xffffffffu, 6.f, -1.f);
    ZUIDrawListAddRectDashed(&draw, 0.f, 0.f, std::numeric_limits<float>::infinity(), 30.f, 0xffffffffu);
    EXPECT_EQ(draw.VtxCount, count);
}

TEST_F(ZUIShadowTest, ColorFieldUsesOneQuadAndSeparatesEffectsAndModelParameters)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &ctx.FrameArena, 16, 24, 0.f, 0.f, 7);
    ZUIDrawListPushClipRect(&draw, 0, 0, 400, 300);
    ZUIDrawListAddColorField(&draw, 120, 116, 96, 0.89f, 0.62f, false);
    EXPECT_EQ(draw.VtxCount, 4u);
    EXPECT_EQ(draw.IdxCount, 6u);
    ASSERT_EQ(draw.CmdCount, 1u);
    EXPECT_FLOAT_EQ(draw.Cmds[0].ColorField.Enabled, 1.f);
    EXPECT_FLOAT_EQ(draw.Cmds[0].ColorField.Brightness, 0.89f);
    EXPECT_FLOAT_EQ(draw.Vtx[0].u, -97.f / 96.f);
    ZUIDrawListAddColorField(&draw, 120, 116, 96, 0.89f, 0.62f, false);
    EXPECT_EQ(draw.CmdCount, 1u);
    EXPECT_EQ(draw.Cmds[0].ElemCount, 12u);
    ZUIDrawListAddColorField(&draw, 120, 116, 96, 0.89f, 0.62f, true);
    ASSERT_EQ(draw.CmdCount, 2u);
    EXPECT_FLOAT_EQ(draw.Cmds[1].ColorField.LightnessMode, 1.f);
    ZUIDrawListAddColorField(&draw, 120, 116, 96, 2.f, -1.f, true);
    ASSERT_EQ(draw.CmdCount, 3u);
    EXPECT_FLOAT_EQ(draw.Cmds[2].ColorField.Brightness, 1.f);
    EXPECT_FLOAT_EQ(draw.Cmds[2].ColorField.Lightness, 0.f);
    ZUIDrawListAddRectFilledNoAA(&draw, 0, 0, 10, 10, 0xffffffffu);
    ASSERT_EQ(draw.CmdCount, 4u);
    EXPECT_FLOAT_EQ(draw.Cmds[3].ColorField.Enabled, 0.f);
    ZUIDrawListAddShadow(&draw, 20, 20, 60, 60, 0xffffffffu, 6, 28);
    ASSERT_EQ(draw.CmdCount, 5u);
    EXPECT_FLOAT_EQ(draw.Cmds[4].ColorField.Enabled, 0.f);
    EXPECT_GT(draw.Cmds[4].Shadow.Sigma, 0.f);
}

TEST_F(ZUIShadowTest, ColorFieldRejectsInvalidClippedAndUnindexableGeometry)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &ctx.FrameArena, 16, 24, 0.f, 0.f, 0);
    ZUIDrawListPushClipRect(&draw, 0, 0, 400, 300);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ZUIDrawListAddColorField(&draw, nan, 116, 96, 1, 0.5f, false);
    ZUIDrawListAddColorField(&draw, 120, 116, 0, 1, 0.5f, false);
    ZUIDrawListAddColorField(&draw, 120, 116, std::numeric_limits<float>::denorm_min(), 1, 0.5f, false);
    ZUIDrawListAddColorField(&draw, 120, 116, 96, nan, 0.5f, false);
    ZUIDrawListAddColorField(&draw, 800, 116, 96, 1, 0.5f, false);
    EXPECT_EQ(draw.VtxCount, 0u);
    EXPECT_EQ(draw.IdxCount, 0u);
    draw.VtxCount = UINT16_MAX - 2;
    ZUIDrawListAddColorField(&draw, 120, 116, 96, 1, 0.5f, false);
    EXPECT_EQ(draw.VtxCount, UINT16_MAX - 2u);
    EXPECT_EQ(draw.IdxCount, 0u);
}

TEST_F(ZUIShadowTest, CustomDrawReceivesSolvedBoundsAndClipBetweenBackgroundAndBorder)
{
    struct Observation
    {
        float    Min[2]        = {};
        float    Clip[4]       = {};
        uint32_t PriorVertices = 0;
        uint32_t AfterVertices = 0;
        bool     Called        = false;
    } observation    = {};
    auto* root       = ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(300));
    auto* box        = ZUIBeginColumn(&ctx, "custom", ZPx(100), ZPx(60));
    box->Flags       = ZUI_DrawBackground | ZUI_DrawBorder | ZUI_ClipChildren | ZUI_FloatX | ZUI_FloatY;
    box->FloatPos[0] = 20;
    box->FloatPos[1] = 30;
    ZUIBoxSetColor(box, 1, 0, 0, 1);
    box->BorderColor[3]  = 1;
    box->BorderThickness = 1;
    box->CustomDrawData  = &observation;
    box->CustomDraw      = [](ZUIDrawList* draw, const ZUIBox* solved, const void* raw) {
        auto& result  = *const_cast<Observation*>(static_cast<const Observation*>(raw));
        result.Called = true;
        result.Min[0] = solved->ScreenMin[0];
        result.Min[1] = solved->ScreenMin[1];
        ZEngine::Helpers::secure_memcpy(result.Clip, sizeof(result.Clip), draw->ClipStack[draw->ClipDepth - 1], sizeof(result.Clip));
        result.PriorVertices = draw->VtxCount;
        ZUIDrawListAddColorField(draw, 70, 60, 20, 1, 0.5f, false);
        result.AfterVertices = draw->VtxCount;
    };
    ZUIEndColumn(&ctx);
    ZUIEndColumn(&ctx);
    EXPECT_EQ(ctx.Root, root);
    ZUIEndFrame(&ctx);
    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    EXPECT_TRUE(observation.Called);
    EXPECT_FLOAT_EQ(observation.Min[0], 20);
    EXPECT_FLOAT_EQ(observation.Min[1], 30);
    EXPECT_FLOAT_EQ(observation.Clip[0], 20);
    EXPECT_FLOAT_EQ(observation.Clip[1], 30);
    EXPECT_FLOAT_EQ(observation.Clip[2], 120);
    EXPECT_FLOAT_EQ(observation.Clip[3], 90);
    EXPECT_GT(observation.PriorVertices, 0u);
    EXPECT_EQ(observation.AfterVertices - observation.PriorVertices, 4u);
    EXPECT_GT(payload.VtxCount, observation.AfterVertices);
}

TEST_F(ZUIShadowTest, CompleteColorPickerPayloadStaysWithinThe16BitIndexBudget)
{
    ctx.ScreenH    = 900;
    float color[4] = {87.f / 255.f, 157.f / 255.f, 227.f / 255.f, 0.5f};
    ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(900));
    ZUIColorPicker4(&ctx, "color", color);
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    EXPECT_GT(payload.VtxCount, 100u);
    EXPECT_LT(payload.VtxCount, 16000u);
    int fields = 0;
    for (uint32_t i = 0; i < payload.CmdCount; ++i)
        if (payload.Cmds[i].ColorField.Enabled == 1.f)
        {
            ++fields;
            EXPECT_EQ(payload.Cmds[i].ElemCount, 6u);
        }
    EXPECT_EQ(fields, 1);
    for (uint32_t i = 0; i < payload.IdxCount; ++i)
        EXPECT_LT(payload.Idx[i], payload.VtxCount);
}

TEST_F(ZUIShadowTest, ColorPopupOnDenseEditorHasEnoughIndexStorageForAaGeometry)
{
    ctx.ScreenH          = 900;
    ctx.RecentColorCount = 24;
    for (int i = 0; i < 24; ++i)
        ctx.RecentColors[i] = {i * 10, 64, 128, 64}; // Worst-case checker-backed Recent swatches.
    ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(900));
    for (int i = 0; i < 160; ++i)
    {
        char key[32] = {};
        snprintf(key, sizeof(key), "field%d", i);
        auto* box            = ZUIBeginColumn(&ctx, key, ZPx(48), ZPx(22));
        box->Flags           = ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        box->FloatPos[0]     = (i % 8) * 50.f;
        box->FloatPos[1]     = (i / 8) * 26.f;
        box->BorderThickness = 1.f;
        box->BorderColor[3]  = 1.f;
        ZUIBoxSetColor(box, 1, 0, 0, 1);
        ZUIBoxSetCornerRadius(box, 3);
        ZUIEndColumn(&ctx);
    }
    float color[4] = {0.6f, 0.6f, 0.6f, 1.f};
    ZUIColorPicker4(&ctx, "color", color);
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    EXPECT_GT(payload.IdxCount, 131072u); // Old upload limit dropped the final popup batch.
    EXPECT_LE(payload.VtxCount, ZUIPass::VertexCapacity);
    EXPECT_LE(payload.IdxCount, ZUIPass::IndexCapacity);
    for (uint32_t i = 0; i < payload.CmdCount; ++i)
        EXPECT_LE(payload.Cmds[i].IdxOffset + payload.Cmds[i].ElemCount, ZUIPass::IndexCapacity);
}

TEST_F(ZUIShadowTest, MenuAndModalRecipesUseSeparateBlurOffsetAndOpacity)
{
    ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(300));
    auto* menu        = ZUIBeginColumn(&ctx, "menu", ZPx(100), ZPx(60));
    menu->Flags       = ZUI_DropShadow | ZUI_FloatX | ZUI_FloatY;
    menu->FloatPos[0] = 50.f;
    menu->FloatPos[1] = 50.f;
    ZUIBoxSetCornerRadius(menu, 6.f);
    ZUIEndColumn(&ctx);
    auto* modal        = ZUIBeginColumn(&ctx, "modal", ZPx(200), ZPx(100));
    modal->Flags       = ZUI_DropShadow | ZUI_FloatX | ZUI_FloatY;
    modal->ShadowKind  = ZUIShadowKind::Modal;
    modal->FloatPos[0] = 150.f;
    modal->FloatPos[1] = 150.f;
    ZUIBoxSetCornerRadius(modal, 6.f);
    ZUIEndColumn(&ctx);
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);

    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    ASSERT_EQ(payload.VtxCount, 8u);
    ASSERT_EQ(payload.IdxCount, 12u);
    ASSERT_GE(payload.CmdCount, 2u);
    const auto& menu_cmd  = payload.Cmds[0];
    const auto& modal_cmd = payload.Cmds[1];
    EXPECT_FLOAT_EQ(menu_cmd.Shadow.Sigma, 14.f);
    EXPECT_FLOAT_EQ(menu_cmd.Shadow.Radius, 6.f);
    EXPECT_FLOAT_EQ(menu_cmd.Shadow.HalfSize[0], 50.f);
    EXPECT_FLOAT_EQ(menu_cmd.Shadow.HalfSize[1], 30.f);
    EXPECT_EQ(payload.Vtx[0].col, ZUIPackColor(0.f, 0.f, 0.f, 0.55f));
    EXPECT_FLOAT_EQ(payload.Vtx[0].x, 50.f - 42.f);
    EXPECT_FLOAT_EQ(payload.Vtx[0].y, 50.f + 10.f - 42.f);
    EXPECT_FLOAT_EQ(modal_cmd.Shadow.Sigma, 12.f);
    EXPECT_EQ(payload.Vtx[4].col, ZUIPackColor(0.f, 0.f, 0.f, 0.5f));
    EXPECT_FLOAT_EQ(payload.Vtx[4].x, 150.f - 36.f);
    EXPECT_FLOAT_EQ(payload.Vtx[4].y, 150.f + 8.f - 36.f);
}

TEST_F(ZUIShadowTest, ShadowUsesAncestorClipNotItsOwnContentClipAndNormalBatchesResetEffect)
{
    auto* root         = ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(300));
    root->Flags        = ZUI_ClipChildren;
    auto* popup        = ZUIBeginColumn(&ctx, "popup", ZPx(100), ZPx(60));
    popup->Flags       = ZUI_DrawBackground | ZUI_ClipChildren | ZUI_DropShadow | ZUI_FloatX | ZUI_FloatY;
    popup->FloatPos[0] = 50.f;
    popup->FloatPos[1] = 50.f;
    ZUIBoxSetColor(popup, 1.f, 0.f, 0.f, 1.f);
    ZUIEndColumn(&ctx);
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    ASSERT_GE(payload.CmdCount, 2u);
    const auto& shadow     = payload.Cmds[0];
    const auto& background = payload.Cmds[1];
    EXPECT_EQ(shadow.ElemCount, 6u);
    EXPECT_GT(shadow.Shadow.Sigma, 0.f);
    EXPECT_FLOAT_EQ(shadow.ClipX, 0.f);
    EXPECT_FLOAT_EQ(shadow.ClipY, 0.f);
    EXPECT_FLOAT_EQ(shadow.ClipW, 400.f);
    EXPECT_FLOAT_EQ(shadow.ClipH, 300.f);
    EXPECT_FLOAT_EQ(background.Shadow.Sigma, 0.f);
    EXPECT_FLOAT_EQ(background.ClipX, 50.f);
    EXPECT_FLOAT_EQ(background.ClipY, 50.f);
    EXPECT_FLOAT_EQ(background.ClipW, 100.f);
    EXPECT_FLOAT_EQ(background.ClipH, 60.f);
}

TEST_F(ZUIShadowTest, AlternatingEffectsGrowCommandBufferWithoutLeakingIntoTexturedBatches)
{
    ZUIDrawList dl = {};
    ZUIDrawListInit(&dl, &payload_arena, 4096, 8192, 0.25f, 0.5f, 7);
    ZUIDrawListPushClipRect(&dl, 0.f, 0.f, 400.f, 300.f);
    const auto color = ZUIPackColor(0.f, 0.f, 0.f, 0.55f);
    for (int i = 0; i < 300; ++i)
    {
        ZUIDrawListAddShadow(&dl, 50.f, 50.f, 100.f, 100.f, color, 6.f, 28.f);
        ZUIDrawListAddRectFilledNoAA(&dl, 50.f, 50.f, 100.f, 100.f, color);
    }
    ASSERT_EQ(dl.CmdCount, 600u);
    EXPECT_GE(dl.CmdCapacity, dl.CmdCount);
    EXPECT_EQ(dl.IdxCount, 3600u);
    for (uint32_t i = 0; i < dl.CmdCount; ++i)
    {
        EXPECT_FLOAT_EQ(dl.Cmds[i].Shadow.Sigma, i % 2 == 0 ? 14.f : 0.f);
        EXPECT_EQ(dl.Cmds[i].TexIdx, 7u);
        EXPECT_EQ(dl.Cmds[i].ElemCount, 6u);
        EXPECT_EQ(dl.Cmds[i].IdxOffset, i * 6);
    }
}

TEST_F(ZUIShadowTest, EmptyNonFiniteTransparentAndFullyClippedShadowsEmitNothing)
{
    ZUIDrawList dl = {};
    ZUIDrawListInit(&dl, &payload_arena, 16, 24, 0.f, 0.f, 0);
    ZUIDrawListPushClipRect(&dl, 0.f, 0.f, 100.f, 100.f);
    const auto color = ZUIPackColor(0.f, 0.f, 0.f, 0.55f);
    ZUIDrawListAddShadow(&dl, 50.f, 50.f, 50.f, 100.f, color, 6.f, 28.f);
    ZUIDrawListAddShadow(&dl, 50.f, 50.f, 100.f, 100.f, color, 6.f, 0.f);
    ZUIDrawListAddShadow(&dl, 50.f, 50.f, 100.f, 100.f, 0, 6.f, 28.f);
    ZUIDrawListAddShadow(&dl, 150.f, 150.f, 200.f, 200.f, color, 6.f, 28.f);
    ZUIDrawListAddShadow(&dl, 50.f, 50.f, 100.f, 100.f, color, 6.f, std::numeric_limits<float>::quiet_NaN());
    const float largest = std::numeric_limits<float>::max();
    ZUIDrawListAddShadow(&dl, -largest, 0.f, largest, 100.f, color, 6.f, 28.f);
    EXPECT_EQ(dl.VtxCount, 0u);
    EXPECT_EQ(dl.IdxCount, 0u);
}

TEST_F(ZUIShadowTest, InsideBordersDoNotExpandWhenStrokeThicknessChanges)
{
    ZUIDrawList dl = {};
    ZUIDrawListInit(&dl, &payload_arena, 128, 256, 0.f, 0.f, 0);
    const auto color = ZUIPackColor(1.f, 1.f, 1.f, 1.f);
    for (float thickness : {1.f, 2.f, 4.f})
    {
        ZUIDrawListReset(&dl);
        ZUIDrawListAddRect(&dl, 10.f, 20.f, 110.f, 60.f, color, 0.f, 0xF, thickness);
        ASSERT_GT(dl.VtxCount, 0u);
        for (uint32_t i = 0; i < dl.VtxCount; ++i)
            if (dl.Vtx[i].col == color) // transparent AA fringe is allowed outside
            {
                EXPECT_GE(dl.Vtx[i].x, 10.f);
                EXPECT_LE(dl.Vtx[i].x, 110.f);
                EXPECT_GE(dl.Vtx[i].y, 20.f);
                EXPECT_LE(dl.Vtx[i].y, 60.f);
            }
    }
}

TEST_F(ZUIShadowTest, ThinStrokeCoverageHasRequestedWidthAtNativeRetinaAndFractionalDpi)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &payload_arena, 1024, 2048, 0.f, 0.f, 0);
    const auto coverage = [&](float x, float y) {
        float alpha = 0.f;
        for (uint32_t i = 0; i + 2 < draw.IdxCount; i += 3)
        {
            const auto& a   = draw.Vtx[draw.Idx[i]];
            const auto& b   = draw.Vtx[draw.Idx[i + 1]];
            const auto& c   = draw.Vtx[draw.Idx[i + 2]];
            const float det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
            if (std::abs(det) < 1e-8f)
                continue;
            const float u = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / det;
            const float v = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / det;
            const float w = 1.f - u - v;
            if (u >= -1e-6f && v >= -1e-6f && w >= -1e-6f)
                alpha = std::max(alpha, (u * (a.col >> 24) + v * (b.col >> 24) + w * (c.col >> 24)) / 255.f);
        }
        return alpha;
    };
    for (const float scale : {1.f, 1.25f, 2.f})
    {
        ZUIDrawListReset(&draw);
        draw.FringeScale = 1.f / scale;
        ZUIDrawListAddRect(&draw, 10, 20, 110, 60, 0xffffffffu, 3.f);
        float integral = 0.f;
        for (int sample = 0; sample < 600; ++sample)
            integral += coverage(50.f, 18.f + (sample + .5f) * .01f) * .01f;
        EXPECT_NEAR(integral, 1.f, .002f) << scale;
        EXPECT_FLOAT_EQ(coverage(50.f, 18.5f), 0.f);
    }
}

TEST_F(ZUIShadowTest, RoundedFillHasSymmetricUncorruptedNormals)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &payload_arena, 1024, 2048, 0.f, 0.f, 0);
    ZUIDrawListAddRectFilled(&draw, 10, 20, 110, 60, 0xffffffffu, 6.f);
    for (uint32_t i = 0; i < draw.VtxCount; ++i)
    {
        const auto& vertex    = draw.Vtx[i];
        bool        reflected = false;
        for (uint32_t j = 0; j < draw.VtxCount; ++j)
            if (draw.Vtx[j].col == vertex.col && std::abs(draw.Vtx[j].x + vertex.x - 120.f) < .001f && std::abs(draw.Vtx[j].y - vertex.y) < .001f)
                reflected = true;
        EXPECT_TRUE(reflected) << vertex.x << ", " << vertex.y;
    }
}

TEST_F(ZUIShadowTest, RoundedPaintSharesOneMaskAcrossStopsAndRestoresNormalDrawing)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &payload_arena, 256, 512, .1f, .2f, 7);
    ZUIDrawListPushClipRect(&draw, 0, 0, 400, 300);
    const uint32_t stops[] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u};
    ZUIDrawListAddColorRect(&draw, 10, 20, 110, 30, stops, 3, 5.f, true);
    ASSERT_EQ(draw.CmdCount, 1u);
    EXPECT_EQ(draw.VtxCount, 8u);
    EXPECT_EQ(draw.IdxCount, 12u);
    EXPECT_FLOAT_EQ(draw.Cmds[0].ColorField.Enabled, 2.f);
    EXPECT_FLOAT_EQ(draw.Cmds[0].ColorField.LightnessMode, 1.f);
    EXPECT_FLOAT_EQ(draw.Cmds[0].Shadow.Radius, 5.f);
    EXPECT_FLOAT_EQ(draw.Vtx[1].u, 0.f);
    EXPECT_FLOAT_EQ(draw.Vtx[4].u, 0.f); // No per-segment corner seams.
    ZUIDrawListAddRectFilledNoAA(&draw, 0, 0, 5, 5, 0xffffffffu);
    ASSERT_EQ(draw.CmdCount, 2u);
    EXPECT_FLOAT_EQ(draw.Cmds[1].ColorField.Enabled, 0.f);
    EXPECT_FLOAT_EQ(draw.Cmds[1].Shadow.Radius, 0.f);
    EXPECT_EQ(draw.Cmds[1].TexIdx, 7u);
    const auto vertices = draw.VtxCount;
    ZUIDrawListAddColorRect(&draw, 10, 20, 0, 30, stops, 3, 5.f);
    ZUIDrawListAddColorRect(&draw, 10, 20, 110, 30, nullptr, 3, 5.f);
    ZUIDrawListAddColorRect(&draw, 10, 20, 110, 30, stops, 17, 5.f);
    ZUIDrawListAddColorRect(&draw, 10, 20, 110, 30, stops, 3, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(draw.VtxCount, vertices);
}

TEST_F(ZUIShadowTest, InterleavedDrawListsGrowOnlyInTheirOwnArena)
{
    ZUIDrawList first = {};
    ZUIDrawList other = {};
    ZUIDrawListInit(&first, &payload_arena, 4, 6, 0.f, 0.f, 7);
    ZUIDrawListInit(&other, &ctx.FrameArena, 4, 6, 0.f, 0.f, 9);
    const auto other_bytes = ctx.FrameArena.m_current_offset;
    // Grow vertices, indices and path scratch after another list was initialized.
    ZUIDrawListAddRectFilledNoAA(&first, 0, 0, 10, 10, 0xffffffffu);
    ZUIDrawListAddCircleFilled(&first, 20, 20, 10, 0xffffffffu, 160);
    // Also force effect-command growth, independently of geometry growth.
    for (int i = 0; i < 300; ++i)
    {
        ZUIDrawListAddShadow(&first, 0, 0, 10, 10, 0xffffffffu, 2, 4);
        ZUIDrawListAddRectFilledNoAA(&first, 0, 0, 10, 10, 0xffffffffu);
    }
    EXPECT_GT(first.CmdCount, 512u);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, other_bytes);
    const auto in_first_arena = [&](const void* pointer) {
        const auto address = reinterpret_cast<uintptr_t>(pointer);
        const auto begin   = reinterpret_cast<uintptr_t>(payload_arena.m_memory);
        return address >= begin && address < begin + payload_arena.m_current_offset;
    };
    EXPECT_TRUE(in_first_arena(first.Vtx));
    EXPECT_TRUE(in_first_arena(first.Idx));
    EXPECT_TRUE(in_first_arena(first.Cmds));
    EXPECT_TRUE(in_first_arena(first.PathX));
    EXPECT_TRUE(in_first_arena(first.PathY));
    EXPECT_EQ(other.VtxCount, 0u);
    EXPECT_EQ(other.IdxCount, 0u);
}

TEST_F(ZUIShadowTest, PayloadRetainsIndexCapacityInsteadOfGrowingOnEverySmallIncrease)
{
    ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(300));
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    ctx.DrawList.IdxCount    = 131073;
    ctx.DrawList.IdxCapacity = 262144; // Capacity after one normal growth step.
    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    EXPECT_EQ(ctx.DrawList.IdxCapacity, 262144u);
    EXPECT_EQ(payload.IdxCount, 0u);
    payload_arena.Clear();
    // Temporarily sparse frames must not discard the retained growth headroom.
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    EXPECT_EQ(ctx.DrawList.IdxCapacity, 262144u);
}

TEST_F(ZUIShadowTest, MissingPayloadInputsClearRecycledDescriptors)
{
    ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(300));
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    ZUIPass          pass          = {};
    ZUIRenderPayload payload       = {};
    const auto       check_cleared = [&]() {
        EXPECT_EQ(payload.Vtx, nullptr);
        EXPECT_EQ(payload.Idx, nullptr);
        EXPECT_EQ(payload.Cmds, nullptr);
        EXPECT_EQ(payload.VtxCount, 0u);
        EXPECT_EQ(payload.IdxCount, 0u);
        EXPECT_EQ(payload.CmdCount, 0u);
        EXPECT_FLOAT_EQ(payload.Scale[0], 0.f);
        EXPECT_FLOAT_EQ(payload.Scale[1], 0.f);
        EXPECT_FLOAT_EQ(payload.Translate[0], 0.f);
        EXPECT_FLOAT_EQ(payload.Translate[1], 0.f);
    };
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    ASSERT_NE(payload.Vtx, nullptr);
    pass.PreparePayload(nullptr, &payload, &payload_arena);
    check_cleared();
    payload_arena.Clear();
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    ASSERT_NE(payload.Vtx, nullptr);
    pass.PreparePayload(&ctx, &payload, nullptr);
    check_cleared();
    payload_arena.Clear();
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    ctx.Root = nullptr;
    pass.PreparePayload(&ctx, &payload, &payload_arena);
    check_cleared();
    pass.PreparePayload(nullptr, nullptr, nullptr); // Null output is also a safe no-op.
}

TEST_F(ZUIShadowTest, EveryPrimitiveRespectsThe16BitVertexAddressLimit)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &payload_arena, 65536, 128, 0.f, 0.f, 7);
    draw.VtxCount = 65532;
    ZUIDrawListAddRectFilledNoAA(&draw, 0, 0, 10, 10, 0xffffffffu);
    ASSERT_EQ(draw.VtxCount, 65536u);
    EXPECT_EQ(draw.Idx[5], 65535u);
    const auto used_bytes = payload_arena.m_current_offset;
    ZUIDrawListAddRectFilledNoAA(&draw, 0, 0, 10, 10, 0xffffffffu);
    ZUIDrawListAddRectFilledMultiColor(&draw, 0, 0, 10, 10, 1, 2, 3, 4);
    ZUIDrawListAddImage(&draw, 9, 0, 0, 10, 10, 0, 0, 1, 1, 0xffffffffu);
    ZUIDrawListAddRectFilled(&draw, 0, 0, 10, 10, 0xffffffffu, 3);
    ZUIDrawListAddRect(&draw, 0, 0, 10, 10, 0xffffffffu, 3);
    ZUIDrawListAddLine(&draw, 0, 0, 10, 10, 0xffffffffu);
    ZUIDrawListAddCircleFilled(&draw, 20, 20, 10, 0xffffffffu, 16);
    ZUIDrawListAddCircle(&draw, 20, 20, 10, 0xffffffffu, 16);
    ZUIDrawListAddTriangleFilled(&draw, 0, 0, 10, 0, 0, 10, 0xffffffffu);
    const uint32_t stops[] = {0xffffffffu, 0xff000000u};
    ZUIDrawListAddColorRect(&draw, 0, 0, 10, 10, stops, 2, 3);
    ZUIDrawListAddColorField(&draw, 20, 20, 10, 1, .5f, false);
    ZUIDrawListAddShadow(&draw, 0, 0, 10, 10, 0xffffffffu, 2, 4);
    EXPECT_EQ(draw.VtxCount, 65536u);
    EXPECT_EQ(draw.IdxCount, 6u);
    EXPECT_EQ(draw.PathCount, 0u);
    EXPECT_EQ(payload_arena.m_current_offset, used_bytes);
}

TEST_F(ZUIShadowTest, ReusedDrawStorageInitializesEveryConsumedValueAndEffectDefault)
{
    // Recycled arena pages need not contain zeros. Only the live prefixes are
    // consumed; commands must explicitly regain their nonzero declared defaults.
    auto* dirty = payload_arena.Allocate(256 * 1024);
    ASSERT_NE(dirty, nullptr);
    ZEngine::Helpers::secure_memset(dirty, 0xa5, 256 * 1024, 256 * 1024);
    payload_arena.Clear();
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &payload_arena, 4, 6, .25f, .5f, 7);
    const uint32_t stops[] = {0xff112233u, 0xff445566u};
    ZUIDrawListAddColorRect(&draw, 0, 0, 10, 10, stops, 2, 3);
    ZUIDrawListAddRectFilledNoAA(&draw, 20, 20, 30, 30, 0xffabcdefu);
    ASSERT_EQ(draw.CmdCount, 2u);
    EXPECT_EQ(draw.Cmds[1].IdxOffset, 6u);
    EXPECT_EQ(draw.Cmds[1].ElemCount, 6u);
    EXPECT_EQ(draw.Cmds[1].TexIdx, 7u);
    EXPECT_FLOAT_EQ(draw.Cmds[1].ColorField.Enabled, 0.f);
    EXPECT_FLOAT_EQ(draw.Cmds[1].ColorField.Brightness, 1.f);
    EXPECT_FLOAT_EQ(draw.Cmds[1].ColorField.Lightness, .5f);
    EXPECT_FLOAT_EQ(draw.Cmds[1].Shadow.Sigma, 0.f);
    const ZUIDrawVtx expected[] = {
        { 0,  0,   -5,  -5,    stops[0]},
        {10,  0,    5,  -5,    stops[1]},
        {10, 10,    5,   5,    stops[1]},
        { 0, 10,   -5,   5,    stops[0]},
        {20, 20, .25f, .5f, 0xffabcdefu},
        {30, 20, .25f, .5f, 0xffabcdefu},
        {30, 30, .25f, .5f, 0xffabcdefu},
        {20, 30, .25f, .5f, 0xffabcdefu}
    };
    ASSERT_EQ(draw.VtxCount, 8u);
    for (uint32_t i = 0; i < draw.VtxCount; ++i)
    {
        EXPECT_FLOAT_EQ(draw.Vtx[i].x, expected[i].x);
        EXPECT_FLOAT_EQ(draw.Vtx[i].y, expected[i].y);
        EXPECT_FLOAT_EQ(draw.Vtx[i].u, expected[i].u);
        EXPECT_FLOAT_EQ(draw.Vtx[i].v, expected[i].v);
        EXPECT_EQ(draw.Vtx[i].col, expected[i].col);
    }
    for (uint32_t i = 0; i < draw.IdxCount; ++i)
        EXPECT_LT(draw.Idx[i], draw.VtxCount);
    ZUIDrawListReset(&draw);
    ASSERT_EQ(draw.CmdCount, 1u);
    EXPECT_EQ(draw.Cmds[0].ElemCount, 0u);
    EXPECT_EQ(draw.Cmds[0].TexIdx, 7u);
    EXPECT_FLOAT_EQ(draw.Cmds[0].ColorField.Brightness, 1.f);
    EXPECT_FLOAT_EQ(draw.Cmds[0].ColorField.Lightness, .5f);
    EXPECT_FLOAT_EQ(draw.Cmds[0].ColorField.Enabled, 0.f);
}

TEST_F(ZUIShadowTest, StrokeScratchUsesOneNormalPerPointAndRetainsBoundedGrowthHeadroom)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &payload_arena, 4096, 8192, 0.f, 0.f, 7);
    ZUIDrawListAddCircle(&draw, 20, 20, 10, 0xffffffffu, 128);
    EXPECT_EQ(draw.PathCap, 256u); // 128 points + 128 normals, not 384 coordinates.
    ZUIDrawListAddCircle(&draw, 20, 20, 10, 0xffffffffu, 129);
    EXPECT_EQ(draw.PathCap, 512u);
    for (int i = 0; i < 260; ++i)
    {
        ZUIDrawListAddShadow(&draw, 0, 0, 10, 10, 0xffffffffu, 2, 4);
        ZUIDrawListAddRectFilledNoAA(&draw, 0, 0, 10, 10, 0xffffffffu);
    }
    ASSERT_EQ(draw.CmdCapacity, 1024u);
    payload_arena.Clear();
    ZUIDrawListInit(&draw, &payload_arena, 4096, 8192, 0.f, 0.f, 7);
    EXPECT_EQ(draw.PathCap, 512u);
    EXPECT_EQ(draw.CmdCapacity, 1024u);
    EXPECT_EQ(draw.VtxCount, 0u);
    EXPECT_EQ(draw.IdxCount, 0u);
    EXPECT_EQ(draw.PathCount, 0u);
}

TEST_F(ZUIShadowTest, CircleAndPolygonInputsCannotRequestUnboundedOrInvalidTessellation)
{
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &payload_arena, 4096, 8192, 0.f, 0.f, 7);
    ZUIDrawListAddCircleFilled(&draw, 0, 0, .01f, 0xffffffffu);
    EXPECT_EQ(draw.VtxCount, 8u); // Tiny automatic radii still produce four segments.
    ZUIDrawListAddCircle(&draw, 0, 0, .01f, 0xffffffffu);
    EXPECT_EQ(draw.VtxCount, 24u);
    const auto  vertices = draw.VtxCount;
    const auto  bytes    = payload_arena.m_current_offset;
    const float point[]  = {0.f}; // Oversized requests must be rejected before array access.
    ZUIDrawListAddPolylineFilled(&draw, point, point, std::numeric_limits<int>::max(), 0xffffffffu);
    ZUIDrawListAddPolylineFilled(&draw, nullptr, point, 3, 0xffffffffu);
    ZUIDrawListAddCircleFilled(&draw, 0, 0, 10, 0xffffffffu, std::numeric_limits<int>::max());
    ZUIDrawListAddCircle(&draw, 0, 0, 10, 0xffffffffu, std::numeric_limits<int>::max());
    ZUIDrawListAddCircleFilled(&draw, 0, 0, std::numeric_limits<float>::quiet_NaN(), 0xffffffffu);
    ZUIDrawListAddCircle(&draw, 0, 0, std::numeric_limits<float>::infinity(), 0xffffffffu);
    ZUIDrawListAddCircle(&draw, 0, 0, 10, 0xffffffffu, 0, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(draw.VtxCount, vertices);
    EXPECT_EQ(draw.PathCount, 0u);
    EXPECT_EQ(payload_arena.m_current_offset, bytes);
}

TEST_F(ZUIShadowTest, IndexBudgetFitsTheDensestGeometryWithoutUnusedUploadCapacity)
{
    // Closed AA strokes emit four vertices and eighteen indices per point.
    // All other primitives have a lower index/vertex ratio.
    constexpr uint32_t points  = ZUIDrawList::VertexLimit / 4;
    constexpr uint32_t indices = points * 18;
    EXPECT_EQ(ZUIDrawList::IndexLimit, indices);
    EXPECT_EQ(ZUIPass::IndexCapacity, indices);
    auto& draw = ctx.DrawList;
    ZUIDrawListInit(&draw, &payload_arena, ZUIDrawList::VertexLimit, ZUIDrawList::IndexLimit, 0.f, 0.f, 7);
    ZUIDrawListAddCircle(&draw, 20, 20, 10, 0xffffffffu, points);
    ASSERT_EQ(draw.VtxCount, ZUIDrawList::VertexLimit);
    ASSERT_EQ(draw.IdxCount, indices);
    EXPECT_EQ(draw.IdxCapacity, indices);
    for (uint32_t i = 0; i < draw.IdxCount; ++i)
        ASSERT_LT(draw.Idx[i], draw.VtxCount);
    const auto bytes = payload_arena.m_current_offset;
    ZUIDrawListAddCircle(&draw, 20, 20, 10, 0xffffffffu, points);
    EXPECT_EQ(draw.VtxCount, ZUIDrawList::VertexLimit);
    EXPECT_EQ(draw.IdxCount, indices);
    EXPECT_EQ(draw.PathCount, 0u);
    EXPECT_EQ(payload_arena.m_current_offset, bytes);
}
