#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace ZEngine::UI;
using ZEngine::Helpers::secure_strlen;

namespace Tetragrama::Components
{
    struct ZUISettingsTestAccess
    {
        static void Open(ZUIDockspaceComponent& component, int page)
        {
            component.m_settings_open = true;
            component.m_settings_page = page;
        }
        static void OpenAll(ZUIDockspaceComponent& component)
        {
            for (auto& open : component.m_sky_sections)
                open = true;
        }
        static bool Section(const ZUIDockspaceComponent& component, int index)
        {
            return component.m_sky_sections[index];
        }
    };
} // namespace Tetragrama::Components

class ZUISettingsTest : public testing::Test
{
protected:
    using Access                                                = Tetragrama::Components::ZUISettingsTestAccess;
    ZEngine::Core::Memory::MemoryManager          memory        = {};
    ZUIContext                                    ctx           = {};
    ZUIGlyph                                      glyphs[96]    = {};
    ZUIFont                                       font          = {};
    ZUIFontAtlas                                  atlas         = {};
    Tetragrama::EditorScene                       scene         = {};
    Tetragrama::EditorConfiguration               configuration = {};
    std::unique_ptr<Tetragrama::Editor>           editor        = {};
    Tetragrama::Layers::ZUILayer                  layer         = {};
    Tetragrama::Components::ZUIDockspaceComponent component     = {};
    ZUIPanelManager                               panels        = {};

    void                                          SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(1), 4096, 4096);
        ctx.ScreenW     = 1200;
        ctx.ScreenH     = 900;
        ctx.MousePos[0] = ctx.MousePos[1] = -100.f;
        for (auto& glyph : glyphs)
            glyph.AdvanceX = 5.5f;
        font.Glyphs     = glyphs;
        font.GlyphCount = 96;
        font.LineHeight = 13.f;
        atlas.Body = atlas.Small = atlas.Large = atlas.Header = atlas.Heading = atlas.Tiny = &font;
        ctx.Atlas                                                                          = &atlas;
        editor                                                                             = std::make_unique<Tetragrama::Editor>();
        editor->CurrentScene                                                               = &scene;
        editor->Configuration                                                              = &configuration;
        layer.CurrentApp                                                                   = editor.get();
        component.Initialize(&layer);
        Access::Open(component, 1);
        panels.Init(&memory.MainArena);
        component.ShellPanelManager = &panels;
    }
    void TearDown() override
    {
        panels.Shutdown();
        ZUIContextDestroy(&ctx);
    }
    uint64_t Key(const char* key)
    {
        return ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
    }
    ZUIBox* Find(const char* key)
    {
        const auto hash = Key(key);
        for (auto* box = ctx.Root; box;)
        {
            if (box->Key == hash)
                return box;
            if (box->FirstChild)
                box = box->FirstChild;
            else
            {
                while (box && !box->NextSib)
                    box = box->Parent;
                if (box)
                    box = box->NextSib;
            }
        }
        return nullptr;
    }
    std::string Label(const char* key)
    {
        const auto* box = Find(key);
        return box && box->Label.Ptr ? std::string(box->Label.Ptr, box->Label.Len) : "";
    }
    void Frame()
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        component.BuildUI(&ctx);
        ZUIEndFrame(&ctx);
    }
    void Toggle(const char* key, bool space = false)
    {
        ctx.FocusKey      = Key(key);
        ctx.KeyboardFocus = true;
        ctx.SpacePressed  = space;
        ctx.EnterPressed  = !space;
        Frame();
    }
    std::string Text(ZUIBox* box = nullptr)
    {
        if (!box)
            box = ctx.Root;
        std::string result = box->Label.Ptr ? std::string(box->Label.Ptr, box->Label.Len) : "";
        for (auto* child = box->FirstChild; child; child = child->NextSib)
            result += Text(child);
        return result;
    }
};

TEST_F(ZUISettingsTest, DefaultsAndModeSwitchesPreserveEverySectionFlag)
{
    Frame();
    for (int i = 0; i < 11; ++i)
        EXPECT_EQ(Access::Section(component, i), i == 0 || i == 1 || i == 10);
    EXPECT_NE(Find("##sky_mode_r"), nullptr);
    EXPECT_EQ(Find("##sky_atm_planet_center_r"), nullptr);
    EXPECT_EQ(Find("##ch_Gradient"), nullptr);
    Toggle("##ch_Planet");
    EXPECT_NE(Find("##sky_atm_planet_center_r"), nullptr);
    EXPECT_TRUE(Access::Section(component, 2));
    scene.Sky.Mode = ZEngine::Rendering::Scenes::SkyMode::SkySphere;
    Frame();
    EXPECT_EQ(Find("##ch_Planet"), nullptr);
    EXPECT_NE(Find("##ch_Gradient"), nullptr);
    Toggle("##ch_Gradient", true);
    EXPECT_NE(Find("##sky_sphere_horizon_r"), nullptr);
    scene.Sky.Mode = ZEngine::Rendering::Scenes::SkyMode::HDRI;
    Frame();
    EXPECT_NE(Find("##sky_hdri_r"), nullptr);
    EXPECT_EQ(Find("##ch_Gradient"), nullptr);
    scene.Sky.Mode = ZEngine::Rendering::Scenes::SkyMode::Atmosphere;
    Frame();
    EXPECT_NE(Find("##sky_atm_planet_center_r"), nullptr);
    EXPECT_TRUE(Access::Section(component, 8));
    Toggle("##ch_Planet", true);
    EXPECT_EQ(Find("##sky_atm_planet_center_r"), nullptr);
}

TEST_F(ZUISettingsTest, AllOpenAtDefaultSizeUsesExactControlWidthsAndScrolls)
{
    Access::OpenAll(component);
    Frame();
    Frame();
    ASSERT_NE(Find("##stg_win"), nullptr);
    EXPECT_FLOAT_EQ(Find("##stg_win")->ComputedSize[0], 760.f);
    EXPECT_FLOAT_EQ(Find("##stg_win")->ComputedSize[1], 520.f);
    const auto* rows = Find("##settings_rows_Planet");
    ASSERT_NE(rows, nullptr);
    EXPECT_FLOAT_EQ(rows->Padding[0], 8.f);
    EXPECT_FLOAT_EQ(rows->Padding[1], 10.f);
    EXPECT_FLOAT_EQ(rows->Padding[3], 6.f);
    EXPECT_FLOAT_EQ(rows->Gap, 10.f);
    const auto* row = Find("##sky_atm_planet_center_r");
    ASSERT_NE(row, nullptr);
    EXPECT_FLOAT_EQ(row->ComputedSize[1], 25.f);
    EXPECT_FLOAT_EQ(row->FirstChild->ComputedSize[0], 152.f);
    const auto* vector = Find("##sky_atm_planet_center");
    ASSERT_NE(vector, nullptr);
    EXPECT_FLOAT_EQ(vector->ComputedSize[0], 304.f);
    EXPECT_FLOAT_EQ(vector->Gap, 8.f);
    for (auto* field = vector->FirstChild; field; field = field->NextSib)
    {
        EXPECT_FLOAT_EQ(field->ComputedSize[0], 96.f);
        EXPECT_FLOAT_EQ(field->ComputedSize[1], 22.f);
        EXPECT_LE(field->ScreenMax[0], vector->ScreenMax[0]);
        EXPECT_NE(field->CustomDraw, nullptr);
    }
    EXPECT_EQ(Label("##sky_atm_sun_radius"), "0.00465");
    EXPECT_EQ(Label("##sky_atm_rayleigh_scatter_0"), "0.005802");
    EXPECT_EQ(Label("##sky_atm_ozone_absorb_2"), "0.000085");
    ASSERT_NE(Find("##sky_atm_world_scale"), nullptr);
    EXPECT_FLOAT_EQ(Find("##sky_atm_world_scale")->ComputedSize[0], 120.f);
    EXPECT_FLOAT_EQ(Find("##sky_intensity_track")->ComputedSize[0], 216.f);
    EXPECT_FLOAT_EQ(Find("##sky_intensity_value")->ComputedSize[0], 48.f);
    ASSERT_NE(Find("##sky_mode_r"), nullptr);
    ASSERT_NE(Find("##sky_mode_r")->LastChild, nullptr);
    EXPECT_FLOAT_EQ(Find("##sky_mode_r")->LastChild->ComputedSize[0], 280.f);
    auto* scroll = ZUIStateGetOrInsert(&ctx.StateStore, Key("##stg_scroll"));
    ASSERT_GT(scroll->MaxScrollY, 0.f);
    scroll->ScrollYTarget   = scroll->MaxScrollY;
    ctx.Style.ReducedMotion = true;
    Frame();
    EXPECT_FLOAT_EQ(scroll->ScrollY, scroll->MaxScrollY);
}

TEST_F(ZUISettingsTest, HeaderHasCorrectHeightAndFullSurfaceMouseAndKeyboardToggle)
{
    Frame();
    auto* header = Find("##ch_Environment light");
    ASSERT_NE(header, nullptr);
    EXPECT_FLOAT_EQ(header->ComputedSize[1], 26.f);
    EXPECT_FLOAT_EQ(header->Padding[0], 8.f);
    EXPECT_FLOAT_EQ(header->FirstChild->ComputedSize[0], 16.f);
    EXPECT_FLOAT_EQ(header->FirstChild->NextSib->ComputedSize[0], 6.f);
    EXPECT_TRUE(header->Flags & ZUI_Clickable);
    ctx.MousePos[0] = header->ScreenMax[0] - 30.f;
    ctx.MousePos[1] = header->ScreenMin[1] + 13.f;
    Frame();
    ASSERT_EQ(ctx.HotKey, header->Key);
    Frame(); // Rebuild its hovered appearance, not only the hit-test state.
    EXPECT_FLOAT_EQ(Find("##ch_Environment light")->Colors[0][0], Tokens::BgSurface.R);
    EXPECT_FLOAT_EQ(Find("##ch_Environment light")->Colors[0][3], 1.f);
    ctx.MouseDown[0] = ctx.MousePressed[0] = true;
    Frame();
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    Frame();
    EXPECT_FALSE(Access::Section(component, 1));
    EXPECT_EQ(Find("##sky_intensity_r"), nullptr);
    Toggle("##ch_Environment light", true);
    EXPECT_TRUE(Access::Section(component, 1));
    EXPECT_FALSE(ctx.SpacePressed);
    EXPECT_TRUE(Find("##ch_Environment light")->Flags & ZUI_KeyboardFocused);
}

TEST_F(ZUISettingsTest, ResetSkyRestoresAllModesMarksDirtyAndKeepsApplicationCameraPreference)
{
    Frame();
    scene.Sky.EnvironmentIntensity              = 10.f;
    scene.Sky.Atmosphere.PlanetRadiusKilometers = 7000.f;
    scene.Sky.Sphere.ShowSunDisc                = false;
    editor->ConstrainCameraToAtmosphereGround   = true;
    const auto revision                         = scene.SkyRevision.value.load();
    Toggle("Reset##stg_reset");
    EXPECT_FLOAT_EQ(scene.Sky.EnvironmentIntensity, 1.f);
    EXPECT_FLOAT_EQ(scene.Sky.Atmosphere.PlanetRadiusKilometers, 6360.f);
    EXPECT_TRUE(scene.Sky.Sphere.ShowSunDisc);
    EXPECT_GT(scene.SkyRevision.value.load(), revision);
    EXPECT_TRUE(scene.IsDirty());
    EXPECT_TRUE(editor->ConstrainCameraToAtmosphereGround);
}

TEST_F(ZUISettingsTest, ThemeHasOnlyDarkCardAndResetAffectsOnlyCurrentPage)
{
    Access::Open(component, 3);
    Frame();
    ASSERT_NE(Find("##ch_Appearance"), nullptr);
    ASSERT_NE(Find("##ch_Motion"), nullptr);
    const auto* dark = Find("##thm_0");
    ASSERT_NE(dark, nullptr);
    EXPECT_FLOAT_EQ(dark->ComputedSize[0], 110.f);
    EXPECT_FLOAT_EQ(dark->ComputedSize[1], 80.f);
    EXPECT_FLOAT_EQ(dark->BorderThickness, 2.f);
    EXPECT_EQ(Find("##thm_1"), nullptr);
    ctx.Style.ReducedMotion        = true;
    configuration.DarkTheme        = false;
    scene.Sky.EnvironmentIntensity = 9.f;
    Toggle("Reset##stg_reset");
    EXPECT_FALSE(ctx.Style.ReducedMotion);
    EXPECT_TRUE(configuration.DarkTheme);
    EXPECT_FLOAT_EQ(scene.Sky.EnvironmentIntensity, 9.f);
    Access::Open(component, 2);
    Frame();
    const auto* reset = Find("Reset##stg_reset");
    ASSERT_NE(reset, nullptr);
    EXPECT_TRUE(reset->Flags & ZUI_Disabled);
    EXPECT_FALSE(reset->Flags & ZUI_Clickable);
    ctx.Style.ReducedMotion = true;
    Toggle("Reset##stg_reset");
    EXPECT_TRUE(ctx.Style.ReducedMotion);
}

TEST_F(ZUISettingsTest, DiagnosticsCoverEveryModeAndAssetAndLightCondition)
{
    Frame();
    EXPECT_NE(Text().find("Runtime bake state and per-viewport previews are intentionally not scene data."), std::string::npos);
    EXPECT_NE(Text().find("No primary celestial light is assigned; the sky has no direct sun source."), std::string::npos);
    scene.Sky.Mode = ZEngine::Rendering::Scenes::SkyMode::HDRI;
    Frame();
    EXPECT_NE(Text().find("No HDRI asset is assigned. The runtime will select its safe environment state."), std::string::npos);
    scene.Sky.EnvironmentMap        = uuids::uuid::from_string("27c0d523-66ab-4b61-a75e-bcd3e4cb2f89").value();
    scene.Sky.PrimaryCelestialLight = scene.Sky.EnvironmentMap;
    Frame();
    EXPECT_NE(Text().find("HDRI is referenced by stable asset UUID; generated cache paths are not scene data."), std::string::npos);
    EXPECT_EQ(Text().find("No primary celestial light"), std::string::npos);
    scene.Sky.Mode = ZEngine::Rendering::Scenes::SkyMode::SkySphere;
    Frame();
    EXPECT_NE(Text().find("Runtime bake state and per-viewport previews are intentionally not scene data."), std::string::npos);
}

TEST_F(ZUISettingsTest, LayoutUsesLabelColumnAndBothResetActionsRestorePanels)
{
    struct View : ZUIPanelView
    {
        void BuildContent(ZUIContext*, float[4]) override {}
    };
    View view   = {};
    view.Title  = "Outliner";
    auto* panel = panels.AddPanel(ZUIDockHashName("Outliner"));
    ASSERT_NE(panel, nullptr);
    panels.AddView(panel, &view);
    Access::Open(component, 4);
    Frame();
    auto* row = Find("##lp_r0");
    ASSERT_NE(row, nullptr);
    EXPECT_FLOAT_EQ(row->FirstChild->ComputedSize[0], 152.f);
    EXPECT_FLOAT_EQ(row->Gap, 12.f);
    EXPECT_EQ(Text().find("Panels"), std::string::npos);
    panels.SetPanelVisible("Outliner", false);
    EXPECT_FALSE(panels.IsPanelVisible("Outliner"));
    Toggle("Reset Layout##lp_rst");
    EXPECT_TRUE(panels.IsPanelVisible("Outliner"));
    panels.SetPanelVisible("Outliner", false);
    Toggle("Reset##stg_reset");
    EXPECT_TRUE(panels.IsPanelVisible("Outliner"));
}

TEST_F(ZUISettingsTest, AllSkyHeadersToggleWithoutLosingValuesAndSphereSunValuesStayEnabled)
{
    Access::OpenAll(component);
    Frame();
    const char* titles[] = {"Source", "Environment light", "Planet", "Molecular", "Aerosols", "Ozone", "Sun", "Ground", "Diagnostics"};
    for (const char* title : titles)
    {
        char key[80] = {};
        snprintf(key, sizeof(key), "##ch_%s", title);
        // FocusPass excludes fully scrolled-away controls. Present the target
        // at the scroll origin before delivering its keyboard activation.
        auto* header = Find(key);
        ASSERT_NE(header, nullptr);
        auto* scroll    = ZUIStateGetOrInsert(&ctx.StateStore, Key("##stg_scroll"));
        scroll->ScrollY = scroll->ScrollYTarget = std::max(0.f, header->ScreenMin[1] - Find("##stg_scroll")->ScreenMin[1] + scroll->ScrollY);
        ctx.Style.ReducedMotion                 = true;
        Frame();
        Toggle(key);
        Toggle(key, true);
    }
    EXPECT_FLOAT_EQ(scene.Sky.EnvironmentIntensity, 1.f);
    EXPECT_FLOAT_EQ(scene.Sky.Atmosphere.SunAngularRadiusRadians, .00465f);
    scene.Sky.Mode               = ZEngine::Rendering::Scenes::SkyMode::SkySphere;
    scene.Sky.Sphere.ShowSunDisc = false;
    Frame();
    Frame();
    ASSERT_NE(Find("##sky_sphere_radius_track"), nullptr);
    EXPECT_TRUE(Find("##sky_sphere_radius_track")->Flags & ZUI_Clickable);
    EXPECT_FALSE(Find("##sky_sphere_radius_track")->Flags & ZUI_Disabled);
    EXPECT_TRUE(Find("##sky_sphere_intensity_track")->Flags & ZUI_Clickable);
}
