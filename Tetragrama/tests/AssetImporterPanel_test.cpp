#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/AssetImporterPanel.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>

using namespace ZEngine::UI;

namespace Tetragrama::Panels
{
    struct AssetImporterPanelTestAccess
    {
        using Kind = AssetImporterPanel::ImporterKind;

        static bool SourceKind(const char* path, Kind& kind)
        {
            const auto parsed = ZEngine::Core::VFS::VFSPath::Parse(path);
            return parsed.Succeeded() && AssetImporterPanel::TryGetImporterKind(parsed.Value(), kind);
        }
        static const char* SourcePath(const AssetImporterPanel& panel)
        {
            return panel.m_path_buf;
        }
        static void InitializeScratch(AssetImporterPanel& panel, ZEngine::Core::Memory::ArenaAllocator& arena)
        {
            arena.CreateSubArena(ZKilo(64), &panel.m_local_arena, "ImporterTest/Configuration");
        }
        static bool Configure(AssetImporterPanel& panel, ZEngine::Importers::AssetCodec::ImportConfiguration& config, ZEngine::Core::VFS::IVFSContext* vfs, const char* custom_name = nullptr, float scale = 1.f)
        {
            panel.m_use_source_name = !custom_name;
            panel.m_scale           = scale;
            if (custom_name)
                ZEngine::Helpers::secure_strcpy(panel.m_instance_name, sizeof(panel.m_instance_name), custom_name);
            return panel.BuildImportConfiguration(config, vfs);
        }
        static const char* Error(const AssetImporterPanel& panel)
        {
            return panel.m_error;
        }
        static bool BrowsePending(const AssetImporterPanel& panel)
        {
            return panel.m_browse_pending;
        }
        static void ReuseSettings(AssetImporterPanel& panel)
        {
            panel.m_same_settings = true;
        }
        static bool Idle(const AssetImporterPanel& panel)
        {
            return panel.m_state.value.load() == AssetImporterPanel::ImporterState::Idle;
        }
        static int HistoryCount(const AssetImporterPanel& panel)
        {
            return panel.m_hist_count;
        }
        static void History(AssetImporterPanel& panel, const char* name, bool success)
        {
            panel.PushHistory(name, success, success ? "Imported" : "Unable to read the source file.");
        }
        static bool UsesSourceName(const AssetImporterPanel& panel)
        {
            return panel.m_use_source_name;
        }
        static void ShowProgress(AssetImporterPanel& panel, float progress, const char* status)
        {
            ZEngine::Helpers::secure_strcpy(panel.m_path_buf, sizeof(panel.m_path_buf), "/Assets/Ship.GLB");
            panel.m_state.value.store(AssetImporterPanel::ImporterState::Importing);
            panel.m_import_task.Panel = &panel;
            AssetImporterPanel::OnImportProgress(&panel.m_import_task, progress);
            AssetImporterPanel::OnImportLog(&panel.m_import_task, status);
        }
        static bool LastImportSucceeded(const AssetImporterPanel& panel)
        {
            return panel.m_hist_count && panel.m_history[panel.m_hist_count - 1].ok;
        }
        static void Import(AssetImporterPanel& panel, ZEngine::Importers::AssimpImporter& importer, ZEngine::Importers::AssetCodec::ImportConfiguration& config, ZEngine::Core::Memory::ArenaAllocator& arena)
        {
            if (!panel.m_import_task.PublishedPaths.capacity())
            {
                panel.m_import_result_slab.Init(&arena, ZMega(1));
                panel.m_import_task.PublishedPaths.init(&panel.m_import_result_slab, 8);
            }
            panel.m_assimp_importer                = &importer;
            auto& task                             = panel.m_import_task;
            task.Panel                             = &panel;
            task.Configuration                     = config;
            task.Configuration.ArtifactContext     = &task;
            task.Configuration.OnArtifactPublished = &AssetImporterPanel::OnImportArtifactPublished;
            task.Kind                              = AssetImporterPanel::ImporterKind::Assimp;
            task.Outcome                           = AssetImporterPanel::ImportTaskOutcome::Pending;
            task.Outputs.clear();
            task.Error.clear();
            task.Publication = config.VFS->BeginImportPublication();
            ZEngine::Helpers::secure_strcpy(task.SourcePath, sizeof(task.SourcePath), panel.m_path_buf);
            panel.m_state.value.store(AssetImporterPanel::ImporterState::Importing);
            AssetImporterPanel::RunImportTask(&task);
        }
    };
} // namespace Tetragrama::Panels

namespace
{
    // Ready futures exercise the real browse coroutine without native GUI or a
    // scheduler polling thread. The only replaced dependency is the file picker.
    struct ImportFilePicker : ZEngine::Windows::CoreWindow
    {
        std::string Selection = {};
        int         Requests  = 0;
        uint32_t    GetHeight() const override
        {
            return 600;
        }
        uint32_t GetWidth() const override
        {
            return 800;
        }
        ZEngine::Core::Containers::StringView GetTitle() const override
        {
            return {};
        }
        void SetTitle(ZEngine::Core::Containers::StringView) override {}
        bool IsMinimized() const override
        {
            return false;
        }
        bool IsVSyncEnable() const override
        {
            return false;
        }
        void                                    SetVSync(bool) override {}
        void                                    SetCallbackFunction(const EventCallbackFn&) override {}
        const ZEngine::Windows::WindowProperty& GetWindowProperty() const override
        {
            return m_property;
        }
        bool CreateSurface(void*, void**) override
        {
            return false;
        }
        void* GetNativeWindow() const override
        {
            return nullptr;
        }
        void  PollEvent() override {}
        float GetTime() override
        {
            return 0.f;
        }
        float GetDeltaTime() override
        {
            return 0.f;
        }
        bool OnEvent(ZEngine::Core::CoreEvent&) override
        {
            return false;
        }
        bool OnWindowClosed(ZEngine::Windows::Events::WindowClosedEvent&) override
        {
            return false;
        }
        bool OnWindowResized(ZEngine::Windows::Events::WindowResizedEvent&) override
        {
            return false;
        }
        bool OnWindowMinimized(ZEngine::Windows::Events::WindowMinimizedEvent&) override
        {
            return false;
        }
        bool OnWindowMaximized(ZEngine::Windows::Events::WindowMaximizedEvent&) override
        {
            return false;
        }
        bool OnWindowRestored(ZEngine::Windows::Events::WindowRestoredEvent&) override
        {
            return false;
        }
        std::future<std::string> OpenFileDialogAsync(std::span<std::string_view>, std::string_view, std::string_view) override
        {
            ++Requests;
            std::promise<std::string> result = {};
            result.set_value(Selection);
            return result.get_future();
        }
    };
} // namespace

class AssetImporterPanelTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager   memory     = {};
    ZUIContext                             ctx        = {};
    Tetragrama::EditorConfiguration        config     = {};
    std::unique_ptr<Tetragrama::Editor>    editor     = {};
    Tetragrama::Layers::ZUILayer           layer      = {};
    Tetragrama::Panels::AssetImporterPanel importer   = {};
    ImportFilePicker                       picker     = {};
    ZUIGlyph                               glyphs[96] = {};
    ZUIFont                                font       = {};
    ZUIFontAtlas                           atlas      = {};

    void                                   SetUp() override
    {
        memory.Initialize(ZMega(256), {});
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(2), ZMega(1), 1024, 1024);
        for (auto& glyph : glyphs)
            glyph.AdvanceX = 5.5f;
        font.Glyphs     = glyphs;
        font.GlyphCount = 96;
        font.LineHeight = 14.f;
        atlas.Body = atlas.Small = atlas.Header = atlas.Heading = atlas.Tiny = &font;
        ctx.Atlas                                                            = &atlas;
        ZEngine::Core::MainThreadScheduler::Initialize(&memory.MainArena);
        editor                = std::make_unique<Tetragrama::Editor>();
        editor->Configuration = &config;
        editor->CurrentWindow = &picker;
        layer.CurrentApp      = editor.get();
        // Initialize requires the live engine's import budget; UI source selection
        // can be exercised without running an import or creating a window.
        importer.Initialize(&layer, nullptr);
        Tetragrama::Panels::AssetImporterPanelTestAccess::InitializeScratch(importer, memory.MainArena);
    }
    void TearDown() override
    {
        ZUIContextDestroy(&ctx);
        ZEngine::Core::MainThreadScheduler::Shutdown();
    }

    void Build(const char* drop = nullptr, ZUIDragPayloadKind kind = ZUIDragPayloadKind::NativePath)
    {
        ctx.ScreenW = 800;
        ctx.ScreenH = 600;
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        if (drop)
        {
            ctx.DragDropFired = true;
            ctx.DragTargetKey = ZUIHashStr("##imp_dz", 8);
            snprintf(ctx.DragPayload, sizeof(ctx.DragPayload), "%s", drop);
            ctx.DragPayloadLen  = (uint32_t) ZEngine::Helpers::secure_strlen(drop);
            ctx.DragPayloadKind = kind;
        }
        else
            ctx.DragDropFired = false;
        float rect[4] = {0, 0, 800, 600};
        importer.BuildContent(&ctx, rect);
    }

    ZUIBox* FindBox(const char* key)
    {
        const auto hash        = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        ZUIBox*    stack[1024] = {};
        uint32_t   count       = 0;
        if (ctx.Root)
            stack[count++] = ctx.Root;
        while (count)
        {
            auto* box = stack[--count];
            if (box->Key == hash)
                return box;
            for (auto* child = box->FirstChild; child; child = child->NextSib)
                stack[count++] = child;
        }
        return nullptr;
    }
    bool HasBox(const char* key)
    {
        return FindBox(key) != nullptr;
    }
    void Layout(float width = 420.f, float height = 610.f)
    {
        ctx.ScreenW       = width;
        ctx.ScreenH       = height;
        ctx.Root->Size[0] = ZPx(width);
        ctx.Root->Size[1] = ZPx(height);
        ZUIEndFrame(&ctx);
    }
};

TEST_F(AssetImporterPanelTest, KeyboardDropZoneAndBrowseButtonSelectFilesAndRecoverFromCancel)
{
    using Access      = Tetragrama::Panels::AssetImporterPanelTestAccess;
    ctx.KeyboardFocus = true;
    ctx.FocusKey      = ZUIHashStr("##imp_dz", 8);
    ctx.EnterPressed  = true;
    Build();
    Build(); // A second activation before draining cannot open a second dialog.
    EXPECT_TRUE(Access::BrowsePending(importer));
    ZEngine::Core::MainThreadScheduler::Drain();
    EXPECT_EQ(picker.Requests, 1);
    EXPECT_FALSE(Access::BrowsePending(importer));
    EXPECT_TRUE(Access::Idle(importer));

    picker.Selection        = "/Assets/Ship.GLB";
    constexpr char button[] = "Import File##imp_browse";
    ctx.FocusKey            = ZUIHashStr(button, sizeof(button) - 1);
    ctx.SpacePressed        = true;
    Build();
    ZEngine::Core::MainThreadScheduler::Drain();
    EXPECT_EQ(picker.Requests, 2);
    EXPECT_FALSE(Access::BrowsePending(importer));
    ctx.EnterPressed = ctx.SpacePressed = false;
    Build();
    EXPECT_TRUE(HasBox("##imp_opt_hdr"));
    EXPECT_STREQ(Access::SourcePath(importer), "/Assets/Ship.GLB");
}

TEST_F(AssetImporterPanelTest, AutomaticImportKeepsOptionsAvailableIfItCannotStart)
{
    using Access = Tetragrama::Panels::AssetImporterPanelTestAccess;
    Access::ReuseSettings(importer);
    Build("/Assets/Ship.GLB"); // No live engine/import budget: submission cannot start.
    Build();
    EXPECT_TRUE(HasBox("##imp_opt_hdr"));
    EXPECT_FALSE(Access::Idle(importer));
}

TEST_F(AssetImporterPanelTest, ImportNamesUseTheStemAndCustomNamesConsistently)
{
    using namespace ZEngine::Core::VFS;
    using Access   = Tetragrama::Panels::AssetImporterPanelTestAccess;
    VFSContext vfs = {};
    config.WorkingSpacePath.init(&memory.MainArena, "/Project");
    config.MeshPath.init(&memory.MainArena, "/Assets/Meshes");
    config.MaterialPath.init(&memory.MainArena, "/Assets/Materials");
    config.TexturePath.init(&memory.MainArena, "/Assets/Textures");
    Build("/Assets/Ship.GLB");
    ZEngine::Importers::AssetCodec::ImportConfiguration result = {};
    ASSERT_TRUE(Access::Configure(importer, result, &vfs));
    EXPECT_STREQ(result.AssetName.c_str(), "Ship");
    EXPECT_STREQ(result.OutputAssetFile.c_str(), "Ship.zemesh");
    ASSERT_TRUE(Access::Configure(importer, result, &vfs, "My Ship", 2.f));
    EXPECT_STREQ(result.AssetName.c_str(), "My Ship");
    EXPECT_STREQ(result.OutputAssetFile.c_str(), "My Ship.zemesh");
    EXPECT_FLOAT_EQ(result.Options.UniformScale, 2.f);
    for (const char* name : {"", "../Ship", "dir/Ship", "dir\\Ship", "Ship:", ".", "..", "Ship.", "Ship "})
    {
        EXPECT_FALSE(Access::Configure(importer, result, &vfs, name)) << name;
        EXPECT_NE(Access::Error(importer)[0], '\0');
    }
    const std::string too_long(249, 'a');
    EXPECT_FALSE(Access::Configure(importer, result, &vfs, too_long.c_str()));
    for (float scale : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        EXPECT_FALSE(Access::Configure(importer, result, &vfs, "Ship", scale));
    EXPECT_TRUE(Access::Configure(importer, result, &vfs, "Ship"));
    EXPECT_STREQ(Access::Error(importer), "");
}

TEST_F(AssetImporterPanelTest, OptionCheckboxIncludesItsVisibleLabelInTheClickTarget)
{
    Build("/Assets/Ship.GLB");
    Build();
    const auto* source_name = FindBox("Use source name##imp_usn");
    ASSERT_NE(source_name, nullptr);
    ASSERT_NE(source_name->Parent, nullptr);
    const auto* checkbox = source_name->Parent->Parent;
    ASSERT_NE(checkbox, nullptr);
    EXPECT_TRUE(checkbox->Flags & ZUI_Clickable);
    EXPECT_EQ(checkbox->FirstChild, source_name->Parent);
    ctx.FocusKey     = checkbox->Key;
    ctx.SpacePressed = true;
    EXPECT_TRUE(Tetragrama::Panels::AssetImporterPanelTestAccess::UsesSourceName(importer));
    Build();
    EXPECT_FALSE(Tetragrama::Panels::AssetImporterPanelTestAccess::UsesSourceName(importer));
}

TEST_F(AssetImporterPanelTest, IdleMatchesReferenceMetricsAndShowsHonestEmptyHistory)
{
    Build();
    Layout();
    const auto* drop    = FindBox("##imp_dz");
    const auto* browse  = FindBox("Import File##imp_browse");
    const auto* history = FindBox("##ch_Recent Imports");
    ASSERT_NE(drop, nullptr);
    ASSERT_NE(browse, nullptr);
    ASSERT_NE(history, nullptr);
    EXPECT_FLOAT_EQ(drop->ScreenMin[0], 12.f);
    EXPECT_FLOAT_EQ(drop->ScreenMin[1], 12.f);
    EXPECT_FLOAT_EQ(drop->ComputedSize[0], 396.f);
    EXPECT_FLOAT_EQ(drop->ComputedSize[1], 140.f);
    EXPECT_TRUE(drop->Flags & ZUI_DashedBorder);
    EXPECT_FLOAT_EQ(drop->CornerRadii[0], 6.f);
    EXPECT_FLOAT_EQ(browse->ScreenMin[1], 160.f);
    EXPECT_FLOAT_EQ(browse->ComputedSize[1], 26.f);
    EXPECT_FLOAT_EQ(history->ScreenMin[1], 198.f);
    EXPECT_FLOAT_EQ(history->ComputedSize[1], 28.f);
    EXPECT_EQ(history->LastChild->FontSize, ZUIFontSize::Header);
    EXPECT_TRUE(HasBox("No recent imports"));
    EXPECT_TRUE(FindBox("##imp_history")->Flags & ZUI_DrawBackground);
    EXPECT_FALSE(ctx.Root->Flags & ZUI_Scrollable);
    EXPECT_STREQ(importer.Title, "Asset Importer");
    EXPECT_TRUE(importer.MatchesTitle("Importer"));
}

TEST_F(AssetImporterPanelTest, HistoryIsNewestFirstRightAlignedAndScrollsWithoutMovingBrowse)
{
    using Access = Tetragrama::Panels::AssetImporterPanelTestAccess;
    for (int i = 0; i < 20; ++i)
        Access::History(importer, "Very long imported filename for history.fbx", true);
    Access::History(importer, "Failed.obj", false);
    Build();
    Layout(420.f, 300.f);
    EXPECT_EQ(Access::HistoryCount(importer), 16);
    const auto* row = FindBox("##imp_history_0");
    ASSERT_NE(row, nullptr);
    EXPECT_FLOAT_EQ(row->ComputedSize[1], 26.f);
    EXPECT_EQ(row->LastChild->FontSize, ZUIFontSize::Tiny);
    EXPECT_FLOAT_EQ(row->LastChild->ScreenMax[0], 408.f);
    EXPECT_EQ(std::string(row->LastChild->Label.Ptr, row->LastChild->Label.Len), "Failed");
    const auto* history = FindBox("##imp_history");
    ASSERT_NE(history, nullptr);
    const auto* state = ZUIStateGetOrInsert(&ctx.StateStore, history->Key);
    EXPECT_GT(state->MaxScrollY, 0.f);
    EXPECT_FLOAT_EQ(FindBox("Import File##imp_browse")->ScreenMin[1], 160.f);
}

TEST_F(AssetImporterPanelTest, OptionsMatchReferenceColumnsSectionsAndPinnedFooter)
{
    Build("/Assets/Ship.GLB");
    Build();
    Layout();
    const auto* header = FindBox("##imp_opt_hdr");
    const auto* mesh   = FindBox("##ch_Mesh");
    const auto* name   = FindBox("##imp_name_dis");
    const auto* footer = FindBox("##imp_footer_col");
    ASSERT_NE(header, nullptr);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(name, nullptr);
    ASSERT_NE(footer, nullptr);
    EXPECT_FLOAT_EQ(header->ComputedSize[1], 36.f);
    EXPECT_EQ(header->CrossAlign, ZUIAlign::Center);
    EXPECT_FLOAT_EQ(mesh->ScreenMin[1], 36.f);
    EXPECT_FLOAT_EQ(mesh->ComputedSize[1], 28.f);
    EXPECT_FLOAT_EQ(name->ScreenMin[0], 152.f);
    EXPECT_FLOAT_EQ(name->ComputedSize[0], 240.f);
    EXPECT_FLOAT_EQ(name->ComputedSize[1], 22.f);
    const auto* axis = FindBox("##combo_btn_##imp_axis");
    ASSERT_NE(axis, nullptr);
    EXPECT_FLOAT_EQ(axis->ComputedSize[0], 200.f);
    EXPECT_FLOAT_EQ(footer->ScreenMax[1], 610.f);
    EXPECT_FLOAT_EQ(footer->ComputedSize[1], 70.f);
    for (const char* key : {"Cancel##imp_cancel", "Preview...##imp_prev", "Import All##imp_do"})
    {
        const auto* button = FindBox(key);
        ASSERT_NE(button, nullptr);
        EXPECT_FLOAT_EQ(button->ComputedSize[0], 84.f);
        EXPECT_FLOAT_EQ(button->ComputedSize[1], 26.f);
    }
    EXPECT_TRUE(FindBox("Preview...##imp_prev")->Flags & ZUI_Disabled);
    EXPECT_FALSE(HasBox("##imp_filter_row"));
    EXPECT_FALSE(HasBox("##ch_LOD"));
}

TEST_F(AssetImporterPanelTest, SectionDisclosuresUseSquareCenteredLucideChevrons)
{
    const auto check = [&](const char* label) {
        char key[128] = {};
        snprintf(key, sizeof(key), "##ch_%s", label);
        const auto* header = FindBox(key);
        ASSERT_NE(header, nullptr);
        const auto* arrow = header->FirstChild;
        ASSERT_NE(arrow, nullptr);
        EXPECT_TRUE(arrow->Flags & ZUI_DrawIcon);
        EXPECT_FALSE(arrow->Flags & ZUI_DrawTriArrow);
        EXPECT_EQ(arrow->IconId, (uint16_t) ZUIIcon::ChevronDown);
        EXPECT_FLOAT_EQ(arrow->ComputedSize[0], 10.f);
        EXPECT_FLOAT_EQ(arrow->ComputedSize[1], 10.f);
        EXPECT_FLOAT_EQ(arrow->ScreenMin[0] - header->ScreenMin[0], 12.f);
        EXPECT_FLOAT_EQ(arrow->ScreenMin[1] + arrow->ScreenMax[1], header->ScreenMin[1] + header->ScreenMax[1]);
    };
    Build();
    Layout();
    check("Recent Imports");
    Build("/Assets/Ship.GLB");
    Build();
    Layout();
    for (const char* label : {"Mesh", "Materials", "Animation"})
        check(label);
}

TEST_F(AssetImporterPanelTest, CloseIconRemainsCenteredAndUnclippedBesideLongFilenames)
{
    Build("/Assets/A very long source filename that exceeds even a wide importer panel without overlapping its close button.GLB");
    for (float width : {420.f, 280.f, 140.f})
    {
        Build();
        Layout(width, 250.f);
        const auto* header = FindBox("##imp_opt_hdr");
        const auto* name   = FindBox("##imp_hname");
        const auto* close  = FindBox("##imp_close");
        ASSERT_NE(header, nullptr);
        ASSERT_NE(name, nullptr);
        ASSERT_NE(close, nullptr);
        EXPECT_TRUE(name->Flags & ZUI_ClipChildren);
        EXPECT_LE(name->ScreenMax[0], close->ScreenMin[0] - 8.f);
        EXPECT_FLOAT_EQ(close->ComputedSize[0], 25.f);
        EXPECT_FLOAT_EQ(close->ComputedSize[1], 25.f);
        EXPECT_FLOAT_EQ(close->ScreenMax[0], width - 8.f);
        const auto* icon = close->FirstChild;
        ASSERT_NE(icon, nullptr);
        EXPECT_TRUE(icon->Flags & ZUI_DrawIcon);
        EXPECT_EQ(icon->IconId, (uint16_t) ZUIIcon::X);
        EXPECT_FLOAT_EQ(icon->ComputedSize[0], 13.f);
        EXPECT_FLOAT_EQ(icon->ComputedSize[1], 13.f);
        for (int axis = 0; axis < 2; ++axis)
        {
            EXPECT_FLOAT_EQ(icon->ScreenMin[axis] + icon->ScreenMax[axis], close->ScreenMin[axis] + close->ScreenMax[axis]);
            EXPECT_GE(icon->ScreenMin[axis], close->ScreenMin[axis]);
            EXPECT_LE(icon->ScreenMax[axis], close->ScreenMax[axis]);
            EXPECT_GE(close->ScreenMin[axis], header->ScreenMin[axis]);
            EXPECT_LE(close->ScreenMax[axis], header->ScreenMax[axis]);
        }
    }
}

TEST_F(AssetImporterPanelTest, UnsupportedPreviewExplainsItsDisabledStateOnHover)
{
    Build("/Assets/Ship.GLB");
    Build();
    Layout();
    const auto* help = FindBox("##imp_preview_help");
    ASSERT_NE(help, nullptr);
    EXPECT_TRUE(help->Flags & ZUI_NoKeyboardFocus);
    ctx.HotKey = ctx.TooltipHoverKey = help->Key;
    ctx.TooltipHoverTime             = 1.f;
    ctx.MousePos[0]                  = (help->ScreenMin[0] + help->ScreenMax[0]) * 0.5f;
    ctx.MousePos[1]                  = (help->ScreenMin[1] + help->ScreenMax[1]) * 0.5f;
    Build();
    ASSERT_NE(ctx.ForegroundRoot, nullptr);
    ASSERT_NE(ctx.ForegroundRoot->FirstChild, nullptr);
    const auto& label = ctx.ForegroundRoot->FirstChild->Label;
    EXPECT_EQ(std::string(label.Ptr, label.Len), "Import preview is not supported yet.");
    const auto* preview = FindBox("Preview...##imp_prev");
    ASSERT_NE(preview, nullptr);
    EXPECT_TRUE(preview->Flags & ZUI_Disabled);
    EXPECT_FALSE(preview->Flags & ZUI_Clickable);
}

TEST_F(AssetImporterPanelTest, ShortNarrowOptionsKeepControlsWithinThePanelAndFooterVisible)
{
    Build("/Assets/Ship.GLB");
    Build();
    Layout(280.f, 250.f);
    const auto* footer = FindBox("##imp_footer_col");
    const auto* body   = FindBox("##imp_opt_scroll");
    ASSERT_NE(footer, nullptr);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(footer->ScreenMax[1], 250.f);
    EXPECT_LE(body->ScreenMax[1], footer->ScreenMin[1]);
    EXPECT_GT(ZUIStateGetOrInsert(&ctx.StateStore, body->Key)->MaxScrollY, 0.f);
    for (const char* key : {"##imp_name_dis", "##combo_btn_##imp_axis", "Cancel##imp_cancel", "Import All##imp_do"})
    {
        const auto* box = FindBox(key);
        ASSERT_NE(box, nullptr);
        EXPECT_GE(box->ComputedSize[0], 0.f);
        EXPECT_GE(box->ScreenMin[0], 0.f);
        EXPECT_LE(box->ScreenMax[0], 280.f);
    }
}

TEST_F(AssetImporterPanelTest, CloseClearsPreflightErrorsAndDoesNotBuildStaleOptions)
{
    using Access = Tetragrama::Panels::AssetImporterPanelTestAccess;
    Build("/Assets/Ship.GLB");
    ZEngine::Importers::AssetCodec::ImportConfiguration configuration = {};
    EXPECT_FALSE(Access::Configure(importer, configuration, nullptr));
    ASSERT_NE(Access::Error(importer)[0], '\0');
    ctx.FocusKey     = ZUIHashStr("##imp_close", 11);
    ctx.EnterPressed = true;
    Build();
    EXPECT_TRUE(Access::Idle(importer));
    EXPECT_STREQ(Access::Error(importer), "");
    EXPECT_STREQ(Access::SourcePath(importer), "");
    EXPECT_FALSE(HasBox("##imp_footer_col"));
}

TEST_F(AssetImporterPanelTest, ProgressUsesFullFilenameSixPixelTrackAndBoundedWorkerStatus)
{
    using Access = Tetragrama::Panels::AssetImporterPanelTestAccess;
    Access::ShowProgress(importer, 0.62f, "Reading geometry...");
    Build();
    Layout();
    EXPECT_TRUE(HasBox("Importing Ship.GLB"));
    EXPECT_TRUE(HasBox("Reading geometry... 62%"));
    const auto* track = FindBox("##imp_prog");
    ASSERT_NE(track, nullptr);
    EXPECT_FLOAT_EQ(track->ScreenMin[0], 24.f);
    EXPECT_FLOAT_EQ(track->ComputedSize[0], 372.f);
    EXPECT_FLOAT_EQ(track->ComputedSize[1], 6.f);
    EXPECT_FLOAT_EQ(track->CornerRadii[0], 3.f);
    EXPECT_FLOAT_EQ(track->FirstChild->ComputedSize[0], 372.f * 0.62f);
    EXPECT_FALSE(track->Flags & ZUI_DrawBorder);
    EXPECT_TRUE(FindBox("Cancel##imp_cancel_running")->Flags & ZUI_Disabled);
    EXPECT_FALSE(HasBox("##imp_log_scroll"));
    Access::ShowProgress(importer, std::numeric_limits<float>::quiet_NaN(), "Reading geometry...");
    Build();
    Layout();
    EXPECT_FLOAT_EQ(FindBox("##imp_prog")->FirstChild->ComputedSize[0], 0.f);
    EXPECT_TRUE(HasBox("Reading geometry... 0%"));
}

TEST_F(AssetImporterPanelTest, RealObjImportCompletesOnMainThreadAndRecoversFromFailure)
{
    using namespace ZEngine::Core::VFS;
    using Access = Tetragrama::Panels::AssetImporterPanelTestAccess;
    struct Workspace
    {
        VFSMemoryBackend      Backend   = {};
        VFSContext            VFS       = {};
        std::filesystem::path Directory = {};
        ~Workspace()
        {
            VFS.Shutdown();
            std::error_code error = {};
            if (!Directory.empty())
                std::filesystem::remove_all(Directory, error);
        }
    } workspace = {};
    workspace.Backend.Initialize(&memory.MainArena);
    workspace.VFS.Initialize(&memory.MainArena, 4);
    ASSERT_TRUE(workspace.VFS.Mount(&workspace.Backend, VFSPath::Root(), 0).Succeeded());
    workspace.Directory = std::filesystem::temp_directory_path() / ("editor-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directory(workspace.Directory));
    const auto source      = (workspace.Directory / "Triangle.OBJ").string();
    const auto destination = VFSPath::Parse("/Assets/Meshes/Custom Triangle.zemesh").Value();
    config.WorkingSpacePath.init(&memory.MainArena, workspace.Directory.string().c_str());
    config.MeshPath.init(&memory.MainArena, "/Assets/Meshes");
    config.MaterialPath.init(&memory.MainArena, "/Assets/Materials");
    config.TexturePath.init(&memory.MainArena, "/Assets/Textures");
    Build(source.c_str());
    ZEngine::Importers::AssetCodec::ImportConfiguration import_config = {};
    ASSERT_TRUE(Access::Configure(importer, import_config, &workspace.VFS, "Custom Triangle"));
    import_config.Options.ImportMaterials             = false;
    import_config.Options.ImportTextures              = false;
    ZEngine::Importers::AssimpImporter model_importer = {};
    model_importer.Initialize(&memory.MainArena);

    // Missing source: the real worker reports failure and the main-thread
    // completion returns the panel to Idle instead of leaving it stuck.
    Access::Import(importer, model_importer, import_config, memory.MainArena);
    EXPECT_FALSE(Access::Idle(importer));
    EXPECT_EQ(Access::HistoryCount(importer), 0);
    ZEngine::Core::MainThreadScheduler::Drain();
    ASSERT_TRUE(Access::Idle(importer));
    EXPECT_FALSE(Access::LastImportSucceeded(importer));
    const auto missing = workspace.VFS.Exists(destination);
    ASSERT_TRUE(missing.Succeeded());
    EXPECT_FALSE(missing.Value());

    {
        std::ofstream file(source);
        file << "o Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        ASSERT_TRUE(file.good());
    }
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        Access::Import(importer, model_importer, import_config, memory.MainArena);
        EXPECT_FALSE(Access::Idle(importer));
        ZEngine::Core::MainThreadScheduler::Drain();
        ASSERT_TRUE(Access::Idle(importer));
        EXPECT_TRUE(Access::LastImportSucceeded(importer));
        const auto exists = workspace.VFS.Exists(destination);
        ASSERT_TRUE(exists.Succeeded());
        EXPECT_TRUE(exists.Value());
        EXPECT_EQ(Access::HistoryCount(importer), attempt + 2);
    }
}

TEST_F(AssetImporterPanelTest, DropZoneSelectsSupportedSourceAndShowsOptions)
{
    Build();
    ASSERT_TRUE(HasBox("##imp_dz"));
    Build("/Assets/Ship.GLB");
    Build();
    EXPECT_TRUE(HasBox("##imp_opt_hdr"));
    EXPECT_FALSE(HasBox("##imp_dz"));
    EXPECT_STREQ(Tetragrama::Panels::AssetImporterPanelTestAccess::SourcePath(importer), "/Assets/Ship.GLB");
}

TEST_F(AssetImporterPanelTest, UnrelatedPayloadsLeaveImporterIdle)
{
    Build("actor:42");
    Build();
    EXPECT_TRUE(HasBox("##imp_dz"));
    Build("/Assets/Texture.png");
    Build();
    EXPECT_TRUE(HasBox("##imp_dz"));
}

TEST_F(AssetImporterPanelTest, ProjectDropResolvesNativePathWithoutChangingCase)
{
    config.WorkingSpacePath.init(&memory.MainArena, "/Projects/Game");
    Build("/Assets/Models/Ship.GLB", ZUIDragPayloadKind::VFSPath);
    Build();
    EXPECT_TRUE(HasBox("##imp_opt_hdr"));
#ifdef _WIN32
    EXPECT_STREQ(Tetragrama::Panels::AssetImporterPanelTestAccess::SourcePath(importer), "/Projects/Game\\Assets\\Models\\Ship.GLB");
#else
    EXPECT_STREQ(Tetragrama::Panels::AssetImporterPanelTestAccess::SourcePath(importer), "/Projects/Game/Assets/Models/Ship.GLB");
#endif
}

TEST_F(AssetImporterPanelTest, OpaqueOrUnresolvableFileDropsRemainIdle)
{
    Build("/Assets/Ship.glb", ZUIDragPayloadKind::Opaque);
    Build();
    EXPECT_TRUE(HasBox("##imp_dz"));
    Build("/Assets/Ship.glb", ZUIDragPayloadKind::VFSPath);
    Build();
    EXPECT_TRUE(HasBox("##imp_dz"));
}

TEST(EditorFileDropTest, NativePathsPreserveTheirNamespaceAndSeparators)
{
    char native[128] = {};
    for (const char* path : {"C:\\Projects\\Game\\Ship.GLB", "c:/Projects/Game/Ship.GLB", "\\\\server\\share\\Ship.glb", "/Projects/Game/Ship.glb"})
    {
        ASSERT_TRUE(Tetragrama::Panels::ResolveDroppedFile(path, ZUIDragPayloadKind::NativePath, nullptr, nullptr, native, sizeof(native)));
        EXPECT_STREQ(native, path);
    }
}

TEST(EditorFileDropTest, EngineMountAndProjectMountResolveSeparately)
{
    char native[128] = {};
    ASSERT_TRUE(Tetragrama::Panels::ResolveDroppedFile("/ZodiacEngine/Models/Ship.GLB", ZUIDragPayloadKind::VFSPath, "/Project", "/EngineAssets", native, sizeof(native)));
#ifdef _WIN32
    EXPECT_STREQ(native, "/EngineAssets\\Models\\Ship.GLB");
#else
    EXPECT_STREQ(native, "/EngineAssets/Models/Ship.GLB");
#endif
    // Mount names match whole components, not arbitrary prefixes.
    ASSERT_TRUE(Tetragrama::Panels::ResolveDroppedFile("/ZodiacEngineExtra/Ship.glb", ZUIDragPayloadKind::VFSPath, "/Project", "/EngineAssets", native, sizeof(native)));
#ifdef _WIN32
    EXPECT_STREQ(native, "/Project\\ZodiacEngineExtra\\Ship.glb");
#else
    EXPECT_STREQ(native, "/Project/ZodiacEngineExtra/Ship.glb");
#endif
}

TEST(EditorFileDropTest, InvalidAndOversizedPathsFailWithoutPartialOutput)
{
    char native[32] = {};
    for (const char* path : {"/", "/ZodiacEngine", "/../../Ship.glb", "relative/Ship.glb", ""})
    {
        EXPECT_FALSE(Tetragrama::Panels::ResolveDroppedFile(path, ZUIDragPayloadKind::VFSPath, "/Project", "/Engine", native, sizeof(native)));
        EXPECT_STREQ(native, "");
    }
    EXPECT_FALSE(Tetragrama::Panels::ResolveDroppedFile("/Ship.glb", ZUIDragPayloadKind::VFSPath, nullptr, nullptr, native, sizeof(native)));
    EXPECT_FALSE(Tetragrama::Panels::ResolveDroppedFile("/ZodiacEngine/Ship.glb", ZUIDragPayloadKind::VFSPath, "/Project", nullptr, native, sizeof(native)));
    EXPECT_FALSE(Tetragrama::Panels::ResolveDroppedFile("/Assets/Models/LongFileName.glb", ZUIDragPayloadKind::VFSPath, "/Project", nullptr, native, sizeof(native)));
    EXPECT_STREQ(native, "");
    char exact[sizeof("/Ship.glb")] = {};
    EXPECT_TRUE(Tetragrama::Panels::ResolveDroppedFile("/Ship.glb", ZUIDragPayloadKind::NativePath, nullptr, nullptr, exact, sizeof(exact)));
    EXPECT_FALSE(Tetragrama::Panels::ResolveDroppedFile("/Ship.glb", ZUIDragPayloadKind::NativePath, nullptr, nullptr, exact, sizeof(exact) - 1));
    EXPECT_STREQ(exact, "");
}

TEST(AssetImporterSourceTest, ValidationAndDispatchShareCaseInsensitiveFormatSelection)
{
    using Access      = Tetragrama::Panels::AssetImporterPanelTestAccess;
    Access::Kind kind = {};
    for (const char* path : {"/Ship.glb", "/Ship.GLB", "/Ship.GlTf"})
    {
        ASSERT_TRUE(Access::SourceKind(path, kind));
        EXPECT_EQ(kind, Access::Kind::Gltf);
    }
    ASSERT_TRUE(Access::SourceKind("/Ship.FbX", kind));
    EXPECT_EQ(kind, Access::Kind::Fbx);
    ASSERT_TRUE(Access::SourceKind("/Ship.OBJ", kind));
    EXPECT_EQ(kind, Access::Kind::Assimp);
    for (const char* path : {"/Ship", "/Ship.gltf.backup", "/Ship.png", "/Ship.unsupported"})
        EXPECT_FALSE(Access::SourceKind(path, kind));
}
