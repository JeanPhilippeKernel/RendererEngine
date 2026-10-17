#include <Tetragrama/Panels/ConsolePanel.h>
#include <Tetragrama/Panels/MemoryProfilerPanel.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>

using namespace ZEngine::UI;

class UIWidgetMetricsTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory = {};
    ZUIContext                           ctx    = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(16), {});
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(1), 2048, 2048);
        ctx.ScreenW = 800;
        ctx.ScreenH = 600;
        ZUIBeginFrame(&ctx, 1.f / 60.f);
    }
    void TearDown() override
    {
        ZUIContextDestroy(&ctx);
    }
    const ZUIBox* Find(const char* key)
    {
        const auto hash        = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        ZUIBox*    stack[2048] = {};
        uint32_t   count       = 0;
        if (ctx.Root)
            stack[count++] = ctx.Root;
        while (count)
        {
            const auto* box = stack[--count];
            if (box->Key == hash)
                return box;
            for (auto* child = box->FirstChild; child && count < 2048; child = child->NextSib)
                stack[count++] = child;
        }
        return nullptr;
    }
    void ExpectButtonFitsRow(const char* button_key, const char* row_key)
    {
        const auto* button = Find(button_key);
        const auto* row    = Find(row_key);
        ASSERT_NE(button, nullptr);
        ASSERT_NE(row, nullptr);
        EXPECT_FLOAT_EQ(button->ComputedSize[1], 26.f);
        EXPECT_GE(button->ScreenMin[1], row->ScreenMin[1]);
        EXPECT_LE(button->ScreenMax[1], row->ScreenMax[1]);
    }
};

TEST_F(UIWidgetMetricsTest, ConsoleToolbarAccommodatesControlHeight)
{
    Tetragrama::Panels::ConsolePanel panel = {};
    panel.m_initialized                    = true; // Rendering test: no logger subscription required.
    float rect[4]                          = {0.f, 0.f, 800.f, 600.f};
    panel.BuildContent(&ctx, rect);
    ZUIEndFrame(&ctx);
    ExpectButtonFitsRow("Clear##con", "##con_tb");
}

TEST_F(UIWidgetMetricsTest, MemoryProfilerHeaderAccommodatesControlHeight)
{
    Tetragrama::Panels::MemoryProfilerPanel panel   = {};
    float                                   rect[4] = {0.f, 0.f, 800.f, 600.f};
    panel.BuildContent(&ctx, rect);
    ZUIEndFrame(&ctx);
    ExpectButtonFitsRow("Reset Peaks##mp_rst", "##mp_hdr");
}

TEST_F(UIWidgetMetricsTest, CursorChipAndDragLabelUseFigmaTokensAndForegroundCoordinates)
{
    auto* root      = ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(600));
    ctx.MousePos[0] = 100.f;
    ctx.MousePos[1] = 200.f;
    ZUIDragLabel(&ctx, "Move: Cube");
    ZUIDragCursor(&ctx); // The shell emits the shared chip after panels build their labels.
    EXPECT_EQ(ctx.Current, root);
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    const auto* chip  = Find("##file_drag_ghost");
    const auto* label = Find("##drag_tip");
    ASSERT_NE(chip, nullptr);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(chip->Parent, ctx.ForegroundRoot);
    EXPECT_EQ(label->Parent, ctx.ForegroundRoot);
    EXPECT_EQ(ctx.ForegroundRoot->FirstChild, chip);
    EXPECT_EQ(ctx.ForegroundRoot->LastChild, label);
    EXPECT_FLOAT_EQ(chip->ComputedSize[0], 28.f);
    EXPECT_FLOAT_EQ(chip->ComputedSize[1], 28.f);
    EXPECT_FLOAT_EQ(chip->ScreenMin[0], 86.f);
    EXPECT_FLOAT_EQ(chip->ScreenMin[1], 186.f);
    EXPECT_FLOAT_EQ(chip->BorderThickness, 1.f);
    EXPECT_FLOAT_EQ(chip->CornerRadii[0], 4.f);
    EXPECT_FLOAT_EQ(label->ScreenMin[0], 112.f);
    EXPECT_FLOAT_EQ(label->ScreenMin[1], 212.f);
    EXPECT_FLOAT_EQ(label->Padding[0], 8.f);
    EXPECT_FLOAT_EQ(label->Padding[1], 6.f);
    EXPECT_FLOAT_EQ(label->Padding[2], 8.f);
    EXPECT_FLOAT_EQ(label->Padding[3], 6.f);
    EXPECT_FLOAT_EQ(label->BorderThickness, 1.f);
    EXPECT_FLOAT_EQ(label->CornerRadii[0], 4.f);
    EXPECT_FALSE(label->Flags & ZUI_DropShadow);
    EXPECT_EQ(label->FontSize, ZUIFontSize::Body);
    const float fill[4]       = ZUI_TOKEN(Tokens::AccentSelection);
    const float border[4]     = ZUI_TOKEN(Tokens::AccentBlue);
    const float tip_fill[4]   = ZUI_TOKEN(Tokens::BgSurface);
    const float tip_border[4] = ZUI_TOKEN(Tokens::BorderStrong);
    const float text[4]       = ZUI_TOKEN(Tokens::TextPrimary);
    for (int channel = 0; channel < 4; ++channel)
    {
        EXPECT_FLOAT_EQ(chip->Colors[0][channel], fill[channel]);
        EXPECT_FLOAT_EQ(chip->BorderColor[channel], border[channel]);
        EXPECT_FLOAT_EQ(label->Colors[0][channel], tip_fill[channel]);
        EXPECT_FLOAT_EQ(label->BorderColor[channel], tip_border[channel]);
        EXPECT_FLOAT_EQ(label->TextColor[channel], text[channel]);
    }
}
