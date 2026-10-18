#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Components/ZUI/ZUIRecentScenes.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
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
        static void Close(ZUIDockspaceComponent& component)
        {
            component.m_settings_open = false;
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

TEST_F(ZUISettingsTest, ShellUsesSceneTerminologyAndPreservesDirtySceneTitles)
{
    struct SceneView : ZUIPanelView
    {
        SceneView()
        {
            Title      = "Viewport";
            Chromeless = true;
        }
        void BuildContent(ZUIContext*, float[4]) override {}
    } view = {};
    Access::Close(component);
    auto* panel = panels.AddPanel(100);
    panels.AddView(panel, &view);
    configuration.ProjectName.init(&memory.MainArena, "Terminology");
    configuration.ActiveSceneName.init(&memory.MainArena, "Scene_A");
    scene.MarkDirty(true);
    Frame();
    EXPECT_NE(Find("##scene_tabs"), nullptr);
    EXPECT_EQ(Find("##level_tabs"), nullptr);
    EXPECT_NE(Text().find("Terminology - Scene Editor"), std::string::npos);
    EXPECT_NE(Text().find("Scene_A*"), std::string::npos);
    EXPECT_EQ(Text().find("Level Editor"), std::string::npos);
    scene.MarkDirty(false);
    Frame();
    EXPECT_NE(Text().find("Scene_A"), std::string::npos);
    EXPECT_EQ(Text().find("Scene_A*"), std::string::npos);
}

TEST_F(ZUISettingsTest, FileAndRecentMenusUseSceneTerminology)
{
    Access::Close(component);
    component.Initialize(nullptr); // No workspace or file-system interaction is needed for these labels.
    Toggle("File##menu_File");
    ctx.EnterPressed = false;
    Frame();
    EXPECT_NE(Text().find("New Scene"), std::string::npos);
    EXPECT_NE(Text().find("Open Scene"), std::string::npos);
    EXPECT_EQ(Text().find("New Level"), std::string::npos);
    EXPECT_EQ(Text().find("Open Level"), std::string::npos);
    Toggle("##smrow_Open Recent");
    ctx.EnterPressed = false;
    Frame();
    EXPECT_NE(Text().find("No recent scenes"), std::string::npos);
    EXPECT_NE(Text().find("Clear Recent Scenes"), std::string::npos);
    EXPECT_EQ(Text().find("recent levels"), std::string::npos);
    EXPECT_EQ(Text().find("Recent Levels"), std::string::npos);
}

namespace
{
    struct RecentHistoryFile
    {
        std::filesystem::path Path = std::filesystem::temp_directory_path() / ("zui-recent-scenes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
        ~RecentHistoryFile()
        {
            std::error_code error = {};
            std::filesystem::remove(Path, error);
        }
    };
} // namespace

TEST(RecentScenesTest, ExistingHistoryFormatRoundtripsWithoutLosingScenePaths)
{
    RecentHistoryFile file = {};
    {
        std::ofstream out(file.Path);
        ASSERT_TRUE(out.is_open());
        out << "1700000000\t/Project/Scenes/Scene A.zescene\n1699999999\t/Project/Scenes/Scene B.zescene\n";
    }
    Tetragrama::Components::RecentScenes recent = {};
    recent.Load(file.Path.string());
    ASSERT_EQ(recent.Count, 2);
    EXPECT_STREQ(recent.Items[0].Path, "/Project/Scenes/Scene A.zescene");
    EXPECT_EQ(recent.Items[0].Time, 1700000000);
    recent.Push("/Project/Scenes/Scene B.zescene");
    recent.Load(file.Path.string());
    ASSERT_EQ(recent.Count, 2);
    EXPECT_STREQ(recent.Items[0].Path, "/Project/Scenes/Scene B.zescene");
    EXPECT_STREQ(recent.Items[1].Path, "/Project/Scenes/Scene A.zescene");
}

TEST(RecentScenesTest, HistoryParsingIsBoundedAndRejectsMalformedRecords)
{
    using Tetragrama::Components::RecentScenes;
    RecentHistoryFile file    = {};
    const std::string longest = "/" + std::string(sizeof(RecentScenes::Entry::Path) - 2, 'a');
    {
        std::ofstream out(file.Path, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "not-a-record\n12oops\t/bad.zescene\n-1\t/negative.zescene\n9223372036854775808\t/overflow.zescene\n"
               "\t/no-time.zescene\n1700000000\t\n1700000000\t/"
            << std::string(10000, 'x')
            << "\n"
               "1700000000\t/One.zescene\r\n1700000001\t/One.zescene\n"
               "1700000000\t/invalid"
            << '\0'
            << "/hidden.zescene\n"
               "1700000000\t"
            << longest << "\n1700000000\t/Final.zescene";
    }
    RecentScenes recent = {};
    recent.Load(file.Path.string());
    ASSERT_EQ(recent.Count, 3);
    EXPECT_STREQ(recent.Items[0].Path, "/One.zescene");
    EXPECT_STREQ(recent.Items[1].Path, longest.c_str());
    EXPECT_STREQ(recent.Items[2].Path, "/Final.zescene");
}

TEST(RecentScenesTest, PushIsAliasSafeRejectsInvalidPathsAndStaysWithinCapacity)
{
    using Tetragrama::Components::RecentScenes;
    RecentScenes recent = {};
    for (int i = 0; i < RecentScenes::kMax + 2; ++i)
        recent.Push(("/Scene" + std::to_string(i) + ".zescene").c_str());
    ASSERT_EQ(recent.Count, RecentScenes::kMax);
    EXPECT_STREQ(recent.Items[0].Path, "/Scene9.zescene");
    EXPECT_STREQ(recent.Items[7].Path, "/Scene2.zescene");
    recent.Push(recent.Items[3].Path);
    EXPECT_EQ(recent.Count, RecentScenes::kMax);
    EXPECT_STREQ(recent.Items[0].Path, "/Scene6.zescene");
    EXPECT_STREQ(recent.Items[1].Path, "/Scene9.zescene");
    const char* invalid_paths[] = {nullptr, "", "/bad\tname.zescene", "/bad\nname.zescene", "/bad\rname.zescene"};
    for (const char* path : invalid_paths)
        recent.Push(path);
    recent.Push(std::string(sizeof(RecentScenes::Entry::Path), 'x').c_str());
    EXPECT_EQ(recent.Count, RecentScenes::kMax);
    EXPECT_STREQ(recent.Items[0].Path, "/Scene6.zescene");
    recent.Clear();
    EXPECT_EQ(recent.Count, 0);
    for (const auto& item : recent.Items)
    {
        EXPECT_EQ(item.Time, 0);
        for (char ch : item.Path)
            EXPECT_EQ(ch, '\0');
    }
}

TEST(RecentScenesTest, SceneStemsUseBoundedBuffersAndEitherPlatformsSeparators)
{
    using Tetragrama::Components::RecentScenes;
    struct Case
    {
        const char* Path = nullptr;
        const char* Stem = nullptr;
    };
    const Case cases[] = {
        {     "/Project/Scenes/My Scene.zescene",       "My Scene"},
        {"C:\\Project\\Scenes\\My Scene.zescene",       "My Scene"},
        {   "C:/Project/Scenes/My Scene.zescene",       "My Scene"},
        {               "/dir.with.dots/.hidden",        ".hidden"},
        {"/dir.with.dots/Name.with.dots.zescene", "Name.with.dots"},
        {                                "/dir/",               ""},
        {                                    ".",              "."},
        {                                   "..",             ".."},
        {                                     "",               ""},
        {                                nullptr,               ""}
    };
    for (const auto& item : cases)
    {
        char out[64] = {};
        RecentScenes::Stem(item.Path, out, sizeof(out));
        EXPECT_STREQ(out, item.Stem);
    }
    char tiny[4] = {};
    RecentScenes::Stem("/Scene.zescene", tiny, sizeof(tiny));
    EXPECT_STREQ(tiny, "Sce");
    RecentScenes::Stem("/Scene.zescene", tiny, 1);
    EXPECT_STREQ(tiny, "");
    RecentScenes::Stem("/Scene.zescene", nullptr, 0);
    char aliased[64] = "/Scenes/My Scene.zescene";
    RecentScenes::Stem(aliased, aliased, sizeof(aliased));
    EXPECT_STREQ(aliased, "My Scene");
}

TEST(RecentScenesTest, ExtremeTimestampsCannotOverflowAgeLabels)
{
    using Tetragrama::Components::RecentScenes;
    char out[32] = {};
    RecentScenes::Age(std::numeric_limits<int64_t>::min(), out, sizeof(out));
    EXPECT_STREQ(out, "Last week");
    RecentScenes::Age(std::numeric_limits<int64_t>::max(), out, sizeof(out));
    EXPECT_STREQ(out, "Just now");
    RecentScenes::Age(RecentScenes::Now(), out, 1);
    EXPECT_STREQ(out, "");
    RecentScenes::Age(0, nullptr, 0);
}

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
