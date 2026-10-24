#include <Tetragrama/Panels/FolderTreeCache.h>
#include <Tetragrama/Panels/ProjectViewPanel.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUILayout.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Memory;
using Tetragrama::Panels::FolderTreeCache;

namespace Tetragrama::Panels
{
    struct ProjectViewPanelTestAccess
    {
        static const char* Category(const char* name)
        {
            return ProjectViewPanel::TypeCategory(name);
        }
        static bool Passes(const char* name, bool directory, const char* search, const char* category)
        {
            ProjectViewPanel::Entry entry = {};
            ZEngine::Helpers::secure_strcpy(entry.name, sizeof(entry.name), name);
            entry.is_dir = directory;
            return ProjectViewPanel::PassesFilters(entry, search, category);
        }
        static void DrawAssetTiles(ProjectViewPanel& panel, ZEngine::UI::ZUIContext& ctx)
        {
            const char* names[] = {"Triangle.ZEMESH", "Surface.zematerial", "Texture.zetextures", "Default.zescene", "Lighting.frag", "Player.lua", "notes.txt", "Meshes"};
            panel.m_nentries    = 0;
            for (const char* name : names)
            {
                auto& entry = panel.m_entries[panel.m_nentries++];
                ZEngine::Helpers::secure_strcpy(entry.name, sizeof(entry.name), name);
                ZEngine::Helpers::secure_strcpy(entry.full_path, sizeof(entry.full_path), name);
                entry.is_dir = ZEngine::Helpers::secure_strcmp(name, "Meshes") == 0;
            }
            panel.DrawGrid(&ctx, 1000.f, 600.f);
        }
        static void PopulateGrid(ProjectViewPanel& panel, int count)
        {
            panel.m_nentries      = count;
            panel.m_listing_valid = true;
            for (int i = 0; i < count; ++i)
            {
                auto& entry = panel.m_entries[i];
                entry       = {};
                snprintf(entry.name, sizeof(entry.name), "Asset_%03d.txt", i);
                snprintf(entry.full_path, sizeof(entry.full_path), "/Assets/%s", entry.name);
            }
        }
        static void Search(ProjectViewPanel& panel, const char* query)
        {
            ZEngine::Helpers::secure_strcpy(panel.m_search, sizeof(panel.m_search), query);
        }
        static void ResetGridInput(ProjectViewPanel& panel)
        {
            panel.m_reset_grid_input = true;
        }
        static int DrawGrid(ProjectViewPanel& panel, ZEngine::UI::ZUIContext& ctx, float width, float height)
        {
            return panel.DrawGrid(&ctx, width, height);
        }
        static void Select(ProjectViewPanel& panel, const char* path)
        {
            ZEngine::Helpers::secure_strcpy(panel.m_selected_path, sizeof(panel.m_selected_path), path);
        }
        static const char* SelectedPath(const ProjectViewPanel& panel)
        {
            return panel.m_selected_path;
        }
        static void ClickMarker(ProjectViewPanel& panel, const char* path)
        {
            ZEngine::Helpers::secure_strcpy(panel.m_last_click_path, sizeof(panel.m_last_click_path), path);
            panel.m_last_click_time = 1.f;
        }
        static bool HasClickMarker(const ProjectViewPanel& panel)
        {
            return panel.m_last_click_path[0] || panel.m_last_click_time >= 0.f;
        }
        static void Directory(ProjectViewPanel& panel, const char* path)
        {
            panel.NavigateTo(VFSPath::Parse(path).Value());
        }
        static bool Navigate(ProjectViewPanel& panel, const char* path)
        {
            const auto parsed = VFSPath::Parse(path);
            return parsed.Succeeded() && panel.NavigateTo(parsed.Value());
        }
        static bool History(ProjectViewPanel& panel, int direction)
        {
            return panel.NavigateHistory(direction);
        }
        static uint32_t HistoryCount(const ProjectViewPanel& panel)
        {
            return panel.m_history_count;
        }
        static void Toolbar(ProjectViewPanel& panel, ZEngine::UI::ZUIContext& ctx, float width)
        {
            panel.DrawToolbar(&ctx, width);
        }
        static bool PendingScrollReset(const ProjectViewPanel& panel)
        {
            return panel.m_reset_grid_scroll;
        }
        static void Filter(ProjectViewPanel& panel, const char* category)
        {
            ZEngine::Helpers::secure_strcpy(panel.m_type_filter, sizeof(panel.m_type_filter), category);
        }
        static const char* Filter(const ProjectViewPanel& panel)
        {
            return panel.m_type_filter;
        }
        static const char* Search(const ProjectViewPanel& panel)
        {
            return panel.m_search;
        }
        static void RefreshListing(ProjectViewPanel& panel, IVFSContext* vfs)
        {
            panel.RefreshListing(vfs);
        }
        static void FilterUI(ProjectViewPanel& panel, ZEngine::UI::ZUIContext& ctx)
        {
            panel.DrawFilterControl(&ctx);
            panel.DrawFilters(&ctx);
        }
        static void Listing(ProjectViewPanel& panel, const ZEngine::Core::Containers::Array<VFSDirEntry>& entries)
        {
            panel.CacheListing(entries);
        }
        static int EntryCount(const ProjectViewPanel& panel)
        {
            return panel.m_nentries;
        }
        static bool Truncated(const ProjectViewPanel& panel)
        {
            return panel.m_truncated;
        }
        static void ThumbnailScale(ProjectViewPanel& panel, float scale)
        {
            panel.m_thumbnail_scale = scale;
        }
        static float ThumbnailScale(const ProjectViewPanel& panel)
        {
            return panel.m_thumbnail_scale;
        }
        static void Status(ProjectViewPanel& panel, ZEngine::UI::ZUIContext& ctx, int count, float width)
        {
            panel.DrawStatus(&ctx, count, width);
        }
        static void EntryDirectory(ProjectViewPanel& panel, int index)
        {
            panel.m_entries[index].is_dir = true;
        }
        static const char* EntryName(const ProjectViewPanel& panel, int index)
        {
            return panel.m_entries[index].name;
        }
        static const char* EntryPath(const ProjectViewPanel& panel, int index)
        {
            return panel.m_entries[index].full_path;
        }
        static bool Less(const char* left, const char* right)
        {
            ProjectViewPanel::Entry a = {};
            ProjectViewPanel::Entry b = {};
            ZEngine::Helpers::secure_strcpy(a.name, sizeof(a.name), left);
            ZEngine::Helpers::secure_strcpy(b.name, sizeof(b.name), right);
            return ProjectViewPanel::EntryLess(a, b);
        }
        static void Shortcuts(ProjectViewPanel& panel, ZEngine::UI::ZUIContext& ctx, const ZEngine::UI::ZUIBox* root)
        {
            panel.HandleShortcuts(&ctx, root);
        }
        static int ModalKind(const ProjectViewPanel& panel)
        {
            return static_cast<int>(panel.m_modal);
        }
        static const char* ModalTarget(const ProjectViewPanel& panel)
        {
            return panel.m_modal_target.CStr();
        }
        static bool ModalDirectory(const ProjectViewPanel& panel)
        {
            return panel.m_modal_is_dir;
        }
        static const char* ModalName(const ProjectViewPanel& panel)
        {
            return panel.m_modal_buf;
        }
        static FolderTreeCache& Cache(ProjectViewPanel& panel)
        {
            return panel.m_folder_tree;
        }
        static const char* CurrentDirectory(const ProjectViewPanel& panel)
        {
            return panel.m_current_dir.CStr();
        }
        static void Draw(ProjectViewPanel& panel, ZEngine::UI::ZUIContext& ctx, IVFSContext& vfs, const char* query)
        {
            snprintf(panel.m_folder_search, sizeof(panel.m_folder_search), "%s", query);
            panel.DrawSourcesTree(&ctx, &vfs);
        }
        static void OpenModal(ProjectViewPanel& panel, int kind)
        {
            panel.m_modal        = static_cast<ProjectViewPanel::Modal>(kind);
            panel.m_modal_opened = false;
        }
        static void DrawModal(ProjectViewPanel& panel, ZEngine::UI::ZUIContext& ctx, IVFSContext& vfs)
        {
            panel.DrawModals(&ctx, &vfs);
        }
        static void Operation(ProjectViewPanel& panel, int kind, const char* path, bool directory, const char* name = nullptr)
        {
            panel.OpenItemModal(static_cast<ProjectViewPanel::Modal>(kind), VFSPath::Parse(path).Value(), directory);
            if (name)
                ZEngine::Helpers::secure_strcpy(panel.m_modal_buf, sizeof(panel.m_modal_buf), name);
        }
        static bool Submit(ProjectViewPanel& panel, IVFSContext* vfs)
        {
            return panel.SubmitModal(vfs);
        }
        static const char* Error(const ProjectViewPanel& panel)
        {
            return panel.m_modal_error;
        }
        static const char* Visit(const ProjectViewPanel& panel, uint32_t index)
        {
            return panel.m_history[(panel.m_history_start + index) % ProjectViewPanel::kHistoryCapacity];
        }
        static void Mutated(ProjectViewPanel& panel, const char* source, const char* destination = nullptr)
        {
            const auto target = destination ? VFSPath::Parse(destination).Value() : VFSPath{};
            panel.UpdateNavigationAfterMutation(VFSPath::Parse(source).Value(), destination ? &target : nullptr);
        }
    };
} // namespace Tetragrama::Panels

namespace
{
    uint32_t CountBoxes(const ZEngine::UI::ZUIBox* root)
    {
        uint32_t count = 1;
        for (auto* child = root->FirstChild; child; child = child->NextSib)
            count += CountBoxes(child);
        return count;
    }

    const ZEngine::UI::ZUIBox* FindBox(const ZEngine::UI::ZUIBox* root, uint64_t key)
    {
        if (root->Key == key)
            return root;
        for (auto* child = root->FirstChild; child; child = child->NextSib)
            if (auto* found = FindBox(child, key))
                return found;
        return nullptr;
    }

    const ZEngine::UI::ZUIBox* FindBox(const ZEngine::UI::ZUIBox* root, const char* key)
    {
        return FindBox(root, ZEngine::UI::ZUIHashStr(key, static_cast<uint32_t>(ZEngine::Helpers::secure_strlen(key))));
    }

    std::string_view BoxLabel(const ZEngine::UI::ZUIBox* box)
    {
        return box && box->Label.Ptr ? std::string_view(box->Label.Ptr, box->Label.Len) : std::string_view{};
    }

    struct BrowserTestFonts
    {
        ZEngine::UI::ZUIGlyph     glyphs[224] = {};
        ZEngine::UI::ZUIFont      body        = {};
        ZEngine::UI::ZUIFont      small       = {};
        ZEngine::UI::ZUIFont      heading     = {};
        ZEngine::UI::ZUIFontAtlas atlas       = {};

        BrowserTestFonts()
        {
            for (auto& glyph : glyphs)
                glyph.AdvanceX = 4.f;
            body.Glyphs     = glyphs;
            body.GlyphCount = static_cast<uint32_t>(std::size(glyphs));
            body.LineHeight = 13.f;
            heading = small  = body;
            small.LineHeight = 12.f;
            atlas.Body       = &body;
            atlas.Small      = &small;
            atlas.Heading    = &heading;
        }
    };

    struct ListingVFS : VFSContext
    {
        std::vector<std::string>                                 Directories = {};
        uint32_t                                                 Calls       = 0;
        bool                                                     Fail        = false;

        VFSResult<ZEngine::Core::Containers::Array<VFSDirEntry>> List(const VFSPath& dir, ArenaAllocator* arena) override
        {
            ++Calls;
            if (Fail)
                return VFSResult<ZEngine::Core::Containers::Array<VFSDirEntry>>::Fail(VFSError::IOError);
            ZEngine::Core::Containers::Array<VFSDirEntry> result = {};
            result.init(arena, Directories.size() + 1);
            for (const auto& text : Directories)
            {
                const auto path = VFSPath::Parse(text.c_str());
                if (path.Succeeded() && path.Value().Parent() == dir)
                    result.push({{}, path.Value(), true});
            }
            return VFSResult<ZEngine::Core::Containers::Array<VFSDirEntry>>::Ok(std::move(result));
        }
    };
} // namespace

TEST(ProjectViewPanelTest, CookedAssetsHaveDistinctCaseInsensitiveCategories)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    EXPECT_STREQ(Access::Category("Triangle.zemesh"), "Models");
    EXPECT_STREQ(Access::Category("/Assets/Triangle.ZEMESH"), "Models");
    EXPECT_STREQ(Access::Category("Surface.zematerial"), "Materials");
    EXPECT_STREQ(Access::Category("Surface.ZeMaTeRiAl"), "Materials");
    EXPECT_STREQ(Access::Category("Atlas.zetextures"), "Textures");
    EXPECT_STREQ(Access::Category("Sky.zenvmap"), "Textures");
    EXPECT_STREQ(Access::Category("World.zescene"), "Scenes");
    EXPECT_STREQ(Access::Category("World.ZESCENE"), "Scenes");
}

TEST(ProjectViewPanelTest, SourceExtensionsMatchExactlyRatherThanByPrefix)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    for (const char* name : {"mesh.glb", "mesh.GLTF", "mesh.fbx", "mesh.OBJ", "mesh.dae", "mesh.usd", "mesh.usda", "mesh.usdc", "mesh.stl", "mesh.ply"})
        EXPECT_STREQ(Access::Category(name), "Models") << name;
    for (const char* name : {"source.cpp", "source.cc", "source.cxx", "source.c", "source.hpp", "source.hxx", "source.h", "source.cs", "source.js", "source.ts", "source.py", "source.lua", "source.rs"})
        EXPECT_STREQ(Access::Category(name), "Scripts") << name;
    for (const char* name : {"image.png", "image.psd", "image.pfm", "image.jpg", "image.jpeg", "image.bmp", "image.tga", "image.tif", "image.tiff", "image.dds", "image.hdr", "image.exr", "image.webp", "image.ktx", "image.ktx2"})
        EXPECT_STREQ(Access::Category(name), "Textures") << name;
    for (const char* name : {"shader.glsl", "shader.vert", "shader.frag", "shader.comp", "shader.hlsl", "shader.spv"})
        EXPECT_STREQ(Access::Category(name), "Shaders") << name;
    for (const char* name : {"", "no_extension", "file.", "file.zip", "file.json", "file.csv", "file.css", "file.zemesh2", "file.zescene.backup", "file.glslang", "file.component", "file.extremelylongextension"})
        EXPECT_STREQ(Access::Category(name), "Other") << name;
    EXPECT_STREQ(Access::Category(nullptr), "Other");
}

TEST(ProjectViewPanelTest, ImportedAssetsPassTheirTypeAndSearchFilters)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    EXPECT_TRUE(Access::Passes("ReviewedTriangle.ZEMESH", false, "triangle", "Models"));
    EXPECT_FALSE(Access::Passes("ReviewedTriangle.ZEMESH", false, "triangle", "Scenes"));
    EXPECT_FALSE(Access::Passes("ReviewedTriangle.ZEMESH", false, "missing", "Models"));
    EXPECT_TRUE(Access::Passes("Surface.zematerial", false, "", "Materials"));
    EXPECT_FALSE(Access::Passes("Surface.zematerial", false, "", "Shaders"));
    EXPECT_TRUE(Access::Passes("Atlas.zetextures", false, "", "Textures"));
    EXPECT_TRUE(Access::Passes("Meshes", true, "mesh", "Folders"));
    EXPECT_FALSE(Access::Passes("Meshes", true, "", "Models"));
    EXPECT_TRUE(Access::Passes("ReviewedTriangle.zemesh", false, "", "All"));
}

TEST(ProjectViewPanelTest, HistoryVisitsFoldersRatherThanParentsAndDropsTheForwardBranch)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    EXPECT_FALSE(Access::History(*panel, -1));
    EXPECT_TRUE(Access::Navigate(*panel, "/"));
    EXPECT_TRUE(Access::Navigate(*panel, "/Content/Models"));
    EXPECT_TRUE(Access::Navigate(*panel, "/Other"));
    EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Content/Models");
    EXPECT_TRUE(Access::History(*panel, 1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Other");
    EXPECT_FALSE(Access::History(*panel, 1));
    EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_TRUE(Access::Navigate(*panel, "/NewBranch"));
    EXPECT_FALSE(Access::History(*panel, 1));
    EXPECT_FALSE(Access::History(*panel, 2));
    EXPECT_EQ(Access::HistoryCount(*panel), 3u);
    EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/");
    EXPECT_FALSE(Access::History(*panel, -1));
}

TEST(ProjectViewPanelTest, HistoryIsBoundedAndReusesWrappedStorageAfterBranching)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    for (int i = 0; i < 100; ++i)
    {
        char path[32] = {};
        snprintf(path, sizeof(path), "/Folder_%d", i);
        ASSERT_TRUE(Access::Navigate(*panel, path));
    }
    EXPECT_EQ(Access::HistoryCount(*panel), 64u);
    for (int i = 0; i < 63; ++i)
        EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Folder_36");
    EXPECT_FALSE(Access::History(*panel, -1));
    EXPECT_TRUE(Access::Navigate(*panel, "/NewBranch"));
    EXPECT_EQ(Access::HistoryCount(*panel), 2u);
    EXPECT_FALSE(Access::History(*panel, 1));
    EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Folder_36");
    EXPECT_TRUE(Access::History(*panel, 1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/NewBranch");
}

TEST(ProjectViewPanelTest, NavigationClearsSelectionAndSameFolderDoesNotCreateAVisit)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ASSERT_TRUE(Access::Navigate(*panel, "/Content"));
    Access::Select(*panel, "/Content/Mesh.zemesh");
    EXPECT_FALSE(Access::Navigate(*panel, "/Content"));
    EXPECT_STREQ(Access::SelectedPath(*panel), "/Content/Mesh.zemesh");
    EXPECT_EQ(Access::HistoryCount(*panel), 1u);
    EXPECT_TRUE(Access::Navigate(*panel, "/Other"));
    EXPECT_STREQ(Access::SelectedPath(*panel), "");
    EXPECT_TRUE(Access::PendingScrollReset(*panel));
    Access::Select(*panel, "/Other/notes.txt");
    EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_STREQ(Access::SelectedPath(*panel), "");
}

TEST(ProjectViewPanelTest, NaturalOrderHandlesArbitrarilyLongNumbersAndDeterministicTies)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    EXPECT_TRUE(Access::Less("t_2.png", "T_10.png"));
    EXPECT_TRUE(Access::Less("a02B", "a2c"));
    EXPECT_TRUE(Access::Less("T_01", "T_1")); // Equal values: raw-name tie-break, not backend order.
    EXPECT_TRUE(Access::Less("T_1", "t_1"));
    const std::string short_number = "T_" + std::string(180, '9');
    const std::string long_number  = "T_" + std::string(181, '1');
    EXPECT_TRUE(Access::Less(short_number.c_str(), long_number.c_str()));
    EXPECT_FALSE(Access::Less(long_number.c_str(), short_number.c_str()));
    const std::string padded = "T_" + std::string(180, '0') + "9";
    EXPECT_TRUE(Access::Less(padded.c_str(), "T_10"));
    const char* names[] = {"", "0", "00", "0a", "00A", "1", "10", "a", "A", "a0", "a000", "a01", "A1", "a2", "a02b", "a2c", "a10"};
    for (const auto* a : names)
    {
        EXPECT_FALSE(Access::Less(a, a));
        for (const auto* b : names)
        {
            if (Access::Less(a, b))
                EXPECT_FALSE(Access::Less(b, a));
            for (const auto* c : names)
                if (Access::Less(a, b) && Access::Less(b, c))
                    EXPECT_TRUE(Access::Less(a, c));
        }
    }
}

class FolderTreeCacheTest : public testing::Test
{
protected:
    MemoryManager                    memory  = {};
    ArenaAllocator                   scratch = {};
    std::unique_ptr<FolderTreeCache> cache   = {};
    ListingVFS                       vfs     = {};

    void                             SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        memory.MainArena.CreateSubArena(ZMega(32), &scratch, "FolderTreeCacheTest");
        cache           = std::make_unique<FolderTreeCache>();
        vfs.Directories = {"/Assets", "/Assets/Models", "/Assets/Models/Spaceship", "/Other", "/.hidden"};
    }
    const FolderTreeCache::Node* Find(const char* path)
    {
        for (uint32_t i = 0; i < cache->Count; ++i)
            if (ZEngine::Helpers::secure_strcmp(cache->Nodes[i].Path, path) == 0)
                return &cache->Nodes[i];
        return nullptr;
    }
};

TEST_F(FolderTreeCacheTest, CachedListingSortsFoldersFirstNaturallyAndSkipsOnlyFilesForMetaSidecars)
{
    using Access                               = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto                               panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    const std::pair<const char*, bool> input[] = {
        {    "/T_10.png", false},
        {   "/Folder_10",  true},
        {     "/t_2.png", false},
        {     "/T_1.png", false},
        {    "/folder_2",  true},
        {    "/T_01.png", false},
        {  "/cache.meta",  true},
        {   "/Alpha.txt", false},
        {   "/alpha.txt", false},
        {     "/.hidden",  true},
        {"/T_2.png.MeTa", false}
    };
    ZEngine::Core::Containers::Array<VFSDirEntry> listing = {};
    listing.init(&scratch, std::size(input) + 1);
    listing.push({}); // Invalid paths must not enter the cache.
    for (const auto& [path, directory] : input)
        listing.push({{}, VFSPath::Parse(path).Value(), directory});
    const char* expected[] = {"cache.meta", "folder_2", "Folder_10", "Alpha.txt", "alpha.txt", "T_01.png", "T_1.png", "t_2.png", "T_10.png"};
    for (int order = 0; order < 2; ++order)
    {
        Access::Listing(*panel, listing);
        ASSERT_EQ(Access::EntryCount(*panel), std::size(expected));
        for (size_t i = 0; i < std::size(expected); ++i)
            EXPECT_STREQ(Access::EntryName(*panel, static_cast<int>(i)), expected[i]);
        std::reverse(listing.begin(), listing.end());
    }
}

TEST_F(FolderTreeCacheTest, FilterControlAndMenuMatchTheApprovedActiveStatesAndResetOnAll)
{
    using namespace ZEngine::UI;
    using Access     = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto       panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.ScreenW      = 560;
    ctx.ScreenH      = 286;
    const auto build = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(560.f), ZPx(286.f));
        Access::FilterUI(*panel, ctx);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    };
    const auto  key          = ZUIHashStr("##pv_filters", 12);
    const float rest[4]      = ZUI_TOKEN(Tokens::BgControl);
    const float selected[4]  = ZUI_TOKEN(Tokens::AccentSelection);
    const float border[4]    = ZUI_TOKEN(Tokens::AccentBlue);
    const float secondary[4] = ZUI_TOKEN(Tokens::TextSecondary);
    build();
    const auto* control = FindBox(ctx.Root, key);
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(std::string_view(control->FirstChild->NextSib->Label.Ptr, control->FirstChild->NextSib->Label.Len), "Filters");
    for (int c = 0; c < 4; ++c)
        EXPECT_FLOAT_EQ(control->Colors[0][c], rest[c]);
    const float menu_y = control->ScreenMax[1];
    ctx.FocusKey       = key;
    ctx.EnterPressed   = true;
    build();
    ASSERT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_FLOAT_EQ(ctx.PopupStack[0].PosY, menu_y);
    build();
    const auto* popup = ctx.PopupStack[0].Box;
    ASSERT_NE(popup, nullptr);
    EXPECT_FLOAT_EQ(popup->ComputedSize[0], 160.f);
    EXPECT_FLOAT_EQ(popup->ComputedSize[1], 244.f);
    const char* categories[] = {"All", "Folders", "Scenes", "Models", "Materials", "Textures", "Scripts", "Shaders", "Other"};
    auto*       item         = popup->FirstChild->NextSib;
    for (const auto* category : categories)
    {
        ASSERT_NE(item, nullptr);
        const auto* label = item->FirstChild->NextSib;
        EXPECT_EQ(std::string_view(label->Label.Ptr, label->Label.Len), category);
        EXPECT_EQ(bool(item->FirstChild->Flags & ZUI_DrawIcon), std::string_view(category) == "All");
        item = item->NextSib;
    }
    EXPECT_EQ(item, nullptr);
    ctx.FocusKey     = ZUIHashStr("##miex_Textures", 15);
    ctx.EnterPressed = true;
    build();
    EXPECT_STREQ(Access::Filter(*panel), "Textures");
    EXPECT_TRUE(Access::PendingScrollReset(*panel));
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    ctx.FocusKey = 0; // Inspect the active surface without a keyboard-focus ring.
    build();
    control = FindBox(ctx.Root, key);
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(std::string_view(control->FirstChild->NextSib->Label.Ptr, control->FirstChild->NextSib->Label.Len), "Textures");
    for (int c = 0; c < 4; ++c)
    {
        EXPECT_FLOAT_EQ(control->Colors[0][c], selected[c]);
        EXPECT_FLOAT_EQ(control->BorderColor[c], border[c]);
        EXPECT_FLOAT_EQ(control->FirstChild->TextColor[c], secondary[c]);
    }
    ctx.Disabled = true;
    build();
    control = FindBox(ctx.Root, key);
    ASSERT_NE(control, nullptr);
    EXPECT_TRUE(control->Flags & ZUI_Disabled);
    const float disabled[4] = ZUI_TOKEN(Tokens::TextDisabled);
    for (int c = 0; c < 4; ++c)
        EXPECT_FLOAT_EQ(control->FirstChild->TextColor[c], disabled[c]);
    ctx.Disabled     = false;
    ctx.FocusKey     = key;
    ctx.EnterPressed = true;
    build();
    build();
    popup = ctx.PopupStack[0].Box;
    ASSERT_NE(popup, nullptr);
    const auto* texture = FindBox(popup, ZUIHashStr("##miex_Textures", 15));
    ASSERT_NE(texture, nullptr);
    EXPECT_TRUE(texture->FirstChild->Flags & ZUI_DrawIcon);
    EXPECT_EQ(texture->FirstChild->IconId, static_cast<uint16_t>(ZUIIcon::Check));
    ctx.FocusKey     = ZUIHashStr("##miex_All", 10);
    ctx.EnterPressed = true;
    build();
    EXPECT_STREQ(Access::Filter(*panel), "All");
    build();
    control = FindBox(ctx.Root, key);
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(std::string_view(control->FirstChild->NextSib->Label.Ptr, control->FirstChild->NextSib->Label.Len), "Filters");
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, GridClearsSelectionWhenSearchCategoryOrRefreshHidesTheItem)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::PopulateGrid(*panel, 3);
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    const auto build = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(560.f), ZPx(286.f));
        const int count = Access::DrawGrid(*panel, ctx, 330.f, 190.f);
        ZUIEndColumn(&ctx);
        return count;
    };
    Access::Select(*panel, "/Assets/Asset_001.txt");
    Access::ClickMarker(*panel, "/Assets/Asset_001.txt");
    EXPECT_EQ(build(), 3);
    EXPECT_STREQ(Access::SelectedPath(*panel), "/Assets/Asset_001.txt");
    EXPECT_TRUE(Access::HasClickMarker(*panel));
    Access::Search(*panel, "Asset_002");
    EXPECT_EQ(build(), 1);
    EXPECT_STREQ(Access::SelectedPath(*panel), "");
    EXPECT_FALSE(Access::HasClickMarker(*panel));
    Access::Select(*panel, "/Assets/Asset_002.txt");
    Access::Filter(*panel, "Textures");
    EXPECT_EQ(build(), 0);
    EXPECT_STREQ(Access::SelectedPath(*panel), "");
    Access::Filter(*panel, "All");
    Access::Select(*panel, "/Assets/Asset_002.txt");
    EXPECT_EQ(build(), 1);
    EXPECT_STREQ(Access::SelectedPath(*panel), "/Assets/Asset_002.txt");
    Access::PopulateGrid(*panel, 0);
    EXPECT_EQ(build(), 0);
    EXPECT_STREQ(Access::SelectedPath(*panel), "");
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, EmptyAndFilteredGridStatesMatchTheReferenceAndStayCentered)
{
    using namespace ZEngine::UI;
    using Access           = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto             panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext       ctx   = {};
    BrowserTestFonts fonts = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas   = &fonts.atlas;
    ctx.ScreenW = 720;
    ctx.ScreenH = 161;
    struct Case
    {
        int         count    = 0;
        const char* query    = "";
        const char* category = "All";
        const char* title    = "";
        const char* hint     = "";
        bool        filtered = false;
    };
    const Case cases[] = {
        {0,       "",      "All",                                       "This folder is empty", "Use Add to create a file or a folder, or Import to bring in assets.", false},
        {3,   "conc",      "All",   "No items match " ZUI_GLYPH_LQUOTE "conc" ZUI_GLYPH_RQUOTE,                                 "The search covers this folder only.",  true},
        {3,       "", "Textures",                                       "No items in Textures",  "The search covers this folder only. Textures is the active filter.",  true},
        {3, "co##nc", "Textures", "No items match " ZUI_GLYPH_LQUOTE "co##nc" ZUI_GLYPH_RQUOTE,  "The search covers this folder only. Textures is the active filter.",  true}
    };
    const float secondary[4] = ZUI_TOKEN(Tokens::TextSecondary);
    const float disabled[4]  = ZUI_TOKEN(Tokens::TextDisabled);
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.title);
        Access::PopulateGrid(*panel, test.count);
        Access::Search(*panel, test.query);
        Access::Filter(*panel, test.category);
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(720.f), ZPx(161.f));
        EXPECT_EQ(Access::DrawGrid(*panel, ctx, 708.f, 161.f), 0);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        const auto* message = FindBox(ctx.Root, "##pv_empty");
        const auto* title   = FindBox(ctx.Root, "##pv_empty_title");
        const auto* hint    = FindBox(ctx.Root, "##pv_empty_hint");
        ASSERT_NE(message, nullptr);
        ASSERT_NE(title, nullptr);
        ASSERT_NE(hint, nullptr);
        EXPECT_EQ(BoxLabel(title), test.title);
        EXPECT_EQ(BoxLabel(hint), test.hint);
        EXPECT_EQ(title->FontSize, ZUIFontSize::Heading);
        EXPECT_EQ(hint->FontSize, ZUIFontSize::Small);
        const auto* icon = message->FirstChild;
        ASSERT_NE(icon, nullptr);
        EXPECT_EQ(icon->IconId, static_cast<uint16_t>(test.filtered ? ZUIIcon::Search : ZUIIcon::FolderOpen));
        EXPECT_TRUE(icon->Flags & ZUI_AssetIcon);
        EXPECT_FLOAT_EQ(icon->ComputedSize[0], 38.f);
        EXPECT_FLOAT_EQ(icon->ComputedSize[1], 38.f);
        EXPECT_FLOAT_EQ(message->Gap, 6.f);
        EXPECT_FLOAT_EQ(icon->ScreenMin[1] + message->LastChild->ScreenMax[1], message->ScreenMin[1] + 9.f + message->ScreenMax[1]);
        for (const auto* child = icon; child; child = child->NextSib)
            EXPECT_FLOAT_EQ(child->ScreenMin[0] + child->ScreenMax[0], message->ScreenMin[0] + message->ScreenMax[0]);
        for (int c = 0; c < 4; ++c)
        {
            EXPECT_FLOAT_EQ(title->TextColor[c], secondary[c]);
            EXPECT_FLOAT_EQ(hint->TextColor[c], secondary[c]);
            EXPECT_FLOAT_EQ(icon->TextColor[c], disabled[c]);
        }
        const auto* clear = FindBox(ctx.Root, "Clear filters##pv_clear_filters");
        EXPECT_EQ(clear != nullptr, test.filtered);
        if (clear)
        {
            EXPECT_FLOAT_EQ(clear->ComputedSize[0], 96.f);
            EXPECT_FLOAT_EQ(clear->ComputedSize[1], 26.f);
        }
        const auto* scroll = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("##pv_grid_scroll", 16));
        EXPECT_FLOAT_EQ(scroll->MaxScrollY, 0.f);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, ClearFiltersRestoresTheListingWithoutKeepingSelectionOrScroll)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::PopulateGrid(*panel, 3);
    Access::Search(*panel, "missing");
    Access::Filter(*panel, "Textures");
    Access::Select(*panel, "/Assets/Asset_001.txt");
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    const auto build = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(720.f), ZPx(161.f));
        const int count = Access::DrawGrid(*panel, ctx, 708.f, 161.f);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        return count;
    };
    auto* scroll    = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("##pv_grid_scroll", 16));
    scroll->ScrollY = scroll->ScrollYTarget = scroll->MaxScrollY = 100.f;
    constexpr char clear_key[]                                   = "Clear filters##pv_clear_filters";
    ctx.FocusKey                                                 = ZUIHashStr(clear_key, sizeof(clear_key) - 1);
    ctx.EnterPressed                                             = true;
    EXPECT_EQ(build(), 0);
    EXPECT_STREQ(Access::Search(*panel), "");
    EXPECT_STREQ(Access::Filter(*panel), "All");
    EXPECT_STREQ(Access::SelectedPath(*panel), "");
    EXPECT_TRUE(Access::PendingScrollReset(*panel));
    EXPECT_EQ(build(), 3);
    EXPECT_EQ(FindBox(ctx.Root, "##pv_empty"), nullptr);
    EXPECT_FLOAT_EQ(scroll->ScrollY, 0.f);
    EXPECT_FLOAT_EQ(scroll->ScrollYTarget, 0.f);
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, EmptyMessagesDoNotGrowPersistentStateAndUnknownListingsAreNotCalledEmpty)
{
    using namespace ZEngine::UI;
    using Access           = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto             panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext       ctx   = {};
    BrowserTestFonts fonts = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas        = &fonts.atlas;
    const auto build = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(350.f), ZPx(161.f));
        Access::DrawGrid(*panel, ctx, 338.f, 161.f);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    };
    build();
    EXPECT_EQ(FindBox(ctx.Root, "##pv_empty"), nullptr); // Default state is unknown, not empty.
    Access::PopulateGrid(*panel, 0);
    build();
    const auto persistent_bytes = ctx.PersistentArena.m_current_offset;
    for (int frame = 0; frame < 128; ++frame)
    {
        char query[256] = {};
        snprintf(query, sizeof(query), "long-query-##-%0200d", frame);
        Access::Search(*panel, query);
        build();
        const auto* title = FindBox(ctx.Root, "##pv_empty_title");
        ASSERT_NE(title, nullptr);
        EXPECT_LE(title->ComputedSize[0], 326.f);
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, persistent_bytes);
        EXPECT_LE(ctx.StateStore.Count, 4u);
        EXPECT_LT(CountBoxes(ctx.Root), 12u);
        EXPECT_LT(ctx.FrameArena.m_current_offset, ZKilo(64));
    }
    ASSERT_TRUE(Access::Navigate(*panel, "/Other"));
    build();
    EXPECT_EQ(FindBox(ctx.Root, "##pv_empty"), nullptr);
    Access::PopulateGrid(*panel, 0);
    build();
    EXPECT_NE(FindBox(ctx.Root, "##pv_empty"), nullptr);
    Access::RefreshListing(*panel, nullptr);
    build();
    EXPECT_EQ(FindBox(ctx.Root, "##pv_empty"), nullptr);
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, BrowserShortcutsUseTheSelectedTargetAndConsumeTheActivation)
{
    using namespace ZEngine::UI;
    using Access   = Tetragrama::Panels::ProjectViewPanelTestAccess;
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    for (bool rename : {true, false})
    {
        auto panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
        Access::PopulateGrid(*panel, 2);
        Access::Select(*panel, "/Assets/Asset_001.txt");
        if (!rename)
            Access::EntryDirectory(*panel, 1);
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        auto*      root    = ZUIBeginColumn(&ctx, "browser", ZPx(560.f), ZPx(286.f));
        const auto control = ZUIInvisibleButton(&ctx, "browser_control", ZPx(30.f), ZPx(22.f));
        ctx.FocusKey       = control.Key;
        ctx.F2Pressed      = rename;
        ctx.DeletePressed  = !rename;
        Access::Shortcuts(*panel, ctx, root);
        EXPECT_EQ(Access::ModalKind(*panel), rename ? 3 : 4);
        EXPECT_STREQ(Access::ModalTarget(*panel), "/Assets/Asset_001.txt");
        EXPECT_STREQ(Access::ModalName(*panel), "Asset_001.txt");
        EXPECT_EQ(Access::ModalDirectory(*panel), !rename);
        EXPECT_FALSE(ctx.F2Pressed);
        EXPECT_FALSE(ctx.DeletePressed);
        Access::DrawModal(*panel, ctx, vfs);
        EXPECT_EQ(ctx.PendingPopupKey, ZUIHashStr("##pv_modal", 10));
        ctx.PendingPopupKey = 0;
        ZUIEndColumn(&ctx);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, RenameShortcutSelectsTheNameOnceAndRestoresFocusWithoutReusingUndo)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::PopulateGrid(*panel, 2);
    Access::Select(*panel, "/Assets/Asset_001.txt");
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.ScreenW          = 800;
    ctx.ScreenH          = 600;
    const auto owner_key = ZUIHashStr("browser_control", 15);
    const auto field_key = ZUIHashStr("##pv_mi", 7);
    const auto build     = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        auto* root = ZUIBeginColumn(&ctx, "browser", ZPx(800.f), ZPx(600.f));
        ZUIInvisibleButton(&ctx, "browser_control", ZPx(30.f), ZPx(22.f));
        Access::Shortcuts(*panel, ctx, root);
        Access::DrawModal(*panel, ctx, vfs);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    };
    for (int session = 0; session < 2; ++session)
    {
        ctx.FocusKey  = owner_key;
        ctx.F2Pressed = true;
        build(); // Stage the popup; the name field does not exist yet.
        ASSERT_EQ(ctx.PopupStackSize, 1u);
        build(); // Build the field and initialize the selection only now.
        EXPECT_EQ(ctx.FocusKey, field_key);
        EXPECT_EQ(ctx.SavedFocusKey, owner_key);
        const auto* state = ZUIStateGetOrInsert(&ctx.StateStore, field_key);
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(state->SelectStart, 0);
        EXPECT_FLOAT_EQ(state->UserData, 13.f);
        build();
        EXPECT_EQ(state->SelectStart, 0);
        if (!session)
        {
            ctx.TextInput[0] = 'X';
            ctx.TextInputLen = 1;
            build();
            EXPECT_STREQ(Access::ModalName(*panel), "X");
            EXPECT_EQ(ctx.UndoTop, 1u);
        }
        else
        {
            ctx.CtrlZPressed = true;
            build();
            EXPECT_STREQ(Access::ModalName(*panel), "Asset_001.txt");
            EXPECT_EQ(ctx.UndoTop, 0u);
        }
        ctx.EscapePressed = true;
        build();
        EXPECT_EQ(Access::ModalKind(*panel), 0);
        EXPECT_EQ(ctx.PopupStackSize, 0u);
        EXPECT_EQ(ctx.FocusKey, owner_key);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, BrowserShortcutsCannotActOutsideTheBrowserOrThroughTextEditorsAndOverlays)
{
    using namespace ZEngine::UI;
    using Access   = Tetragrama::Panels::ProjectViewPanelTestAccess;
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    for (int guard = 0; guard < 16; ++guard)
    {
        SCOPED_TRACE(guard);
        auto panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
        Access::PopulateGrid(*panel, 2);
        Access::Select(*panel, "/Assets/Asset_001.txt");
        ctx.Disabled = ctx.CtrlDown = ctx.AltDown = ctx.ShiftDown = false;
        ctx.PopupStackSize                                        = 0;
        ctx.PendingPopupKey                                       = 0;
        ctx.DragSourceKey                                         = 0;
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        auto*      root    = ZUIBeginColumn(&ctx, "browser", ZPx(560.f), ZPx(286.f));
        const auto control = ZUIInvisibleButton(&ctx, "browser_control", ZPx(30.f), ZPx(22.f));
        ctx.FocusKey       = control.Key;
        ctx.F2Pressed = ctx.DeletePressed = true;
        switch (guard)
        {
            case 0:
                ctx.FocusKey = 0;
                break;
            case 1:
                ctx.FocusKey = ZUIHashStr("outliner", 8);
                break;
            case 2:
                ctx.TextInputActive = true;
                break;
            case 3:
                ctx.PopupStackSize = 1;
                break;
            case 4:
                ctx.PendingPopupKey = 1;
                break;
            case 5:
                ctx.ModalBox = root;
                break;
            case 6:
                ctx.Disabled = true;
                break;
            case 7:
                ctx.CtrlDown = true;
                break;
            case 8:
                ctx.AltDown = true;
                break;
            case 9:
                ctx.ShiftDown = true;
                break;
            case 10:
                Access::Select(*panel, "/Assets/Removed.txt");
                break;
            case 11:
                Access::Filter(*panel, "Textures");
                break;
            case 12:
                Access::OpenModal(*panel, 1);
                break;
            case 13:
                ctx.DragSourceKey = control.Key;
                break;
            case 14:
                root->LastChild->Flags = root->LastChild->Flags | ZUI_Disabled;
                break;
            case 15:
                ctx.F2Pressed = ctx.DeletePressed = false;
                break;
        }
        Access::Shortcuts(*panel, ctx, root);
        EXPECT_EQ(Access::ModalKind(*panel), guard == 12 ? 1 : 0);
        ZUIEndColumn(&ctx);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, CachedListingPreservesMaximumLengthPathsAndTerminatesReusedSlots)
{
    using Access            = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto              panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    const std::string filename(VFS_MAX_PATH - 2, 'a');
    const std::string full_path = "/" + filename;
    const auto        parsed    = VFSPath::Parse(full_path.c_str());
    ASSERT_TRUE(parsed.Succeeded());
    ZEngine::Core::Containers::Array<VFSDirEntry> listing = {};
    listing.init(&scratch, 1);
    listing.push({{}, parsed.Value(), true});
    Access::Listing(*panel, listing);
    ASSERT_EQ(Access::EntryCount(*panel), 1);
    EXPECT_STREQ(Access::EntryName(*panel, 0), filename.c_str());
    EXPECT_STREQ(Access::EntryPath(*panel, 0), full_path.c_str());
    Access::Select(*panel, full_path.c_str());
    EXPECT_STREQ(Access::SelectedPath(*panel), full_path.c_str());
    listing[0] = {{}, VFSPath::Parse("/b").Value(), false};
    Access::Listing(*panel, listing);
    ASSERT_EQ(Access::EntryCount(*panel), 1);
    EXPECT_STREQ(Access::EntryName(*panel, 0), "b");
    EXPECT_STREQ(Access::EntryPath(*panel, 0), "/b");
}

TEST_F(FolderTreeCacheTest, CachedListingStaysBoundedWhenTheDirectoryExceedsCapacity)
{
    using Access                                          = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto                                          panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZEngine::Core::Containers::Array<VFSDirEntry> listing = {};
    listing.init(&scratch, 600);
    for (int i = 0; i < 600; ++i)
    {
        char path[VFS_MAX_PATH] = {};
        snprintf(path, sizeof(path), "/Asset_%d.txt", i);
        listing.push({{}, VFSPath::Parse(path).Value(), false});
    }
    Access::Listing(*panel, listing);
    ASSERT_EQ(Access::EntryCount(*panel), 512);
    EXPECT_TRUE(Access::Truncated(*panel));
    EXPECT_STREQ(Access::EntryName(*panel, 0), "Asset_0.txt");
    EXPECT_STREQ(Access::EntryName(*panel, 511), "Asset_511.txt");
    ZEngine::UI::ZUIContext ctx   = {};
    BrowserTestFonts        fonts = {};
    ZEngine::UI::ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas = &fonts.atlas;
    ZEngine::UI::ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZEngine::UI::ZUIBeginColumn(&ctx, "root", ZEngine::UI::ZPx(560), ZEngine::UI::ZPx(26));
    Access::Status(*panel, ctx, 512, 560.f);
    ZEngine::UI::ZUIEndColumn(&ctx);
    const auto* count = FindBox(ctx.Root, "##pv_status_count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(BoxLabel(count), "512+ items");
    constexpr float orange[4] = ZUI_TOKEN(ZEngine::UI::Tokens::AccentOrange);
    for (int c = 0; c < 4; ++c)
        EXPECT_FLOAT_EQ(count->TextColor[c], orange[c]);
    ZEngine::UI::ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, ListingOverflowRequiresARealExtraEntryAndResetsOnNavigationOrRefresh)
{
    using Access                                          = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto                                          panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZEngine::Core::Containers::Array<VFSDirEntry> listing = {};
    listing.init(&scratch, 640);
    for (int i = 0; i < 512; ++i)
    {
        char path[VFS_MAX_PATH] = {};
        snprintf(path, sizeof(path), "/Asset_%d.txt", i);
        listing.push({{}, VFSPath::Parse(path).Value(), false});
    }
    for (int i = 0; i < 60; ++i)
    {
        char path[VFS_MAX_PATH] = {};
        snprintf(path, sizeof(path), "/.hidden_%d", i);
        listing.push({{}, VFSPath::Parse(path).Value(), false});
        snprintf(path, sizeof(path), "/Asset_%d.MeTa", i);
        listing.push({{}, VFSPath::Parse(path).Value(), false});
    }
    Access::Listing(*panel, listing);
    EXPECT_EQ(Access::EntryCount(*panel), 512);
    EXPECT_FALSE(Access::Truncated(*panel));
    listing.push({{}, VFSPath::Parse("/folder.meta").Value(), true}); // A directory is not a sidecar.
    Access::Listing(*panel, listing);
    EXPECT_TRUE(Access::Truncated(*panel));
    Access::Navigate(*panel, "/Other");
    EXPECT_FALSE(Access::Truncated(*panel));
    Access::Listing(*panel, listing);
    EXPECT_TRUE(Access::Truncated(*panel));
    Access::RefreshListing(*panel, nullptr);
    EXPECT_FALSE(Access::Truncated(*panel));
    listing.clear();
    Access::Listing(*panel, listing);
    EXPECT_EQ(Access::EntryCount(*panel), 0);
    EXPECT_FALSE(Access::Truncated(*panel));
}

TEST_F(FolderTreeCacheTest, StatusUsesSmallTextEllipsizesLongPathsAndKeepsTheSharedSliderRigid)
{
    using namespace ZEngine::UI;
    using Access           = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto             panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    BrowserTestFonts fonts = {};
    ZUIContext       ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas                      = &fonts.atlas;
    const std::string path         = "/Assets/" + std::string(230, 'a') + ".txt";
    constexpr float   secondary[4] = ZUI_TOKEN(Tokens::TextSecondary);
    for (float width : {1216.f, 560.f})
        for (bool selected : {false, true})
        {
            Access::Select(*panel, selected ? path.c_str() : "");
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            ZUIBeginColumn(&ctx, "root", ZPx(width), ZPx(26.f));
            Access::Status(*panel, ctx, 8, width);
            ZUIEndColumn(&ctx);
            ZUILayoutSolve(&ctx);
            const auto* status  = FindBox(ctx.Root, "##pv_status");
            const auto* count   = FindBox(ctx.Root, "##pv_status_count");
            const auto* caption = FindBox(ctx.Root, "##pv_status_label");
            const auto* slider  = FindBox(ctx.Root, "##pv_st_slider");
            ASSERT_NE(status, nullptr);
            ASSERT_NE(count, nullptr);
            ASSERT_NE(caption, nullptr);
            ASSERT_NE(slider, nullptr);
            EXPECT_FLOAT_EQ(status->ComputedSize[1], 25.f);
            EXPECT_FLOAT_EQ(status->Padding[0], 12.f);
            EXPECT_FLOAT_EQ(status->Gap, 10.f);
            EXPECT_EQ(BoxLabel(count), selected ? "8 items (1 selected)" : "8 items");
            for (const auto* label : {count, caption})
            {
                EXPECT_EQ(label->FontSize, ZUIFontSize::Small);
                for (int c = 0; c < 4; ++c)
                    EXPECT_FLOAT_EQ(label->TextColor[c], secondary[c]);
            }
            EXPECT_TRUE(slider->Flags & ZUI_Clickable);
            EXPECT_FLOAT_EQ(slider->ComputedSize[0], 56.f);
            EXPECT_FLOAT_EQ(slider->ComputedSize[1], Metrics::HeightControlSm);
            EXPECT_NEAR(slider->ScreenMax[0], width - 12.f, .001f);
            EXPECT_NEAR(slider->ScreenMin[1] + slider->ComputedSize[1] * .5f, 12.5f, .001f);
            const auto* label = FindBox(ctx.Root, "##pv_status_path");
            EXPECT_EQ(label != nullptr, selected);
            if (label)
            {
                EXPECT_EQ(label->FontSize, ZUIFontSize::Small);
                EXPECT_LE(label->ScreenMax[0], caption->ScreenMin[0] - 12.f - 20.f);
                if (width == 560.f)
                    EXPECT_NE(BoxLabel(label), path);
            }
        }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, ThumbnailSliderEditsAndClampsTheLiveTileScale)
{
    using namespace ZEngine::UI;
    using Access     = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto       panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    EXPECT_FLOAT_EQ(Access::ThumbnailScale(*panel), 1.f);
    ctx.FocusKey     = ZUIHashStr("##pv_st_slider", 14);
    const auto build = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(560), ZPx(26));
        Access::Status(*panel, ctx, 1, 560.f);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    };
    ctx.ArrowRightPressed = true;
    build();
    EXPECT_NEAR(Access::ThumbnailScale(*panel), 1.0075f, .00001f);
    Access::ThumbnailScale(*panel, 1.5f);
    build();
    EXPECT_FLOAT_EQ(Access::ThumbnailScale(*panel), 1.5f);
    ctx.ArrowRightPressed = false;
    ctx.ArrowLeftPressed  = true;
    Access::ThumbnailScale(*panel, .75f);
    build();
    EXPECT_FLOAT_EQ(Access::ThumbnailScale(*panel), .75f);
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, ThumbnailExtremesScaleTheWholeTileReflowAndKeepVirtualizationBounded)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::PopulateGrid(*panel, 512);
    BrowserTestFonts fonts = {};
    ZUIContext       ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas                   = &fonts.atlas;
    const auto persistent_bytes = ctx.PersistentArena.m_current_offset;
    for (float scale : {.75f, 1.f, 1.5f, .75f})
    {
        Access::ThumbnailScale(*panel, scale);
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(339), ZPx(190));
        EXPECT_EQ(Access::DrawGrid(*panel, ctx, 327.f, 190.f), 512);
        ZUIEndColumn(&ctx);
        ZUILayoutSolve(&ctx);
        const auto* tile = FindBox(ctx.Root, "##pvc_0");
        ASSERT_NE(tile, nullptr);
        EXPECT_FLOAT_EQ(tile->ComputedSize[0], 110.f * scale);
        EXPECT_FLOAT_EQ(tile->ComputedSize[1], 128.f * scale);
        EXPECT_FLOAT_EQ(tile->FirstChild->ComputedSize[1], 79.f * scale);
        EXPECT_FLOAT_EQ(tile->FirstChild->FirstChild->ComputedSize[0], 38.f * scale);
        const auto* metadata = FindBox(tile, "##pvmd_0");
        ASSERT_NE(metadata, nullptr);
        EXPECT_FLOAT_EQ(metadata->ComputedSize[1], 46.f * scale);
        EXPECT_FLOAT_EQ(metadata->FirstChild->TextScale, scale);
        EXPECT_FLOAT_EQ(metadata->LastChild->TextScale, scale);
        EXPECT_FLOAT_EQ(metadata->LastChild->ComputedSize[1], 12.f * scale);
        const int   columns = static_cast<int>(327.f / (110.f * scale + 10.f));
        const auto* row     = FindBox(ctx.Root, "##pvrow_0");
        ASSERT_NE(row, nullptr);
        int tiles = 0;
        for (const auto* child = row->FirstChild; child; child = child->NextSib)
            if (child->Flags & ZUI_Clickable)
                ++tiles;
        EXPECT_EQ(tiles, columns);
        EXPECT_EQ(FindBox(ctx.Root, "##pvc_511"), nullptr);
        EXPECT_LT(ctx.FrameArena.m_current_offset, ZKilo(128));
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, persistent_bytes);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, AssetAndFolderNamesTreatDoubleHashesAsLiteralText)
{
    using namespace ZEngine::UI;
    using Access                                          = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto                                          panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZEngine::Core::Containers::Array<VFSDirEntry> listing = {};
    listing.init(&scratch, 1);
    listing.push({{}, VFSPath::Parse("/part##one.txt").Value(), false});
    Access::Listing(*panel, listing);
    BrowserTestFonts fonts = {};
    ZUIContext       ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas = &fonts.atlas;
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(300));
    Access::DrawGrid(*panel, ctx, 388.f, 300.f);
    ZUIEndColumn(&ctx);
    const auto* metadata = FindBox(ctx.Root, "##pvmd_0");
    ASSERT_NE(metadata, nullptr);
    EXPECT_EQ(BoxLabel(metadata->FirstChild), "part##one.txt");

    ListingVFS vfs  = {};
    vfs.Directories = {"/folder##one"};
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBeginColumn(&ctx, "root", ZPx(210), ZPx(300));
    Access::Draw(*panel, ctx, vfs, "");
    ZUIEndColumn(&ctx);
    const auto* row = FindBox(ctx.Root, "/folder##one");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(BoxLabel(row->LastChild), "folder##one");
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, CookedAssetTilesShowCorrectKindsAndIcons)
{
    using namespace ZEngine::UI;
    using Access     = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto       panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(600));
    Access::DrawAssetTiles(*panel, ctx);
    ZUIEndColumn(&ctx);
    const auto* row = ctx.Root->FirstChild->FirstChild->NextSib; // Scroll region: spacer, then tile row.
    ASSERT_NE(row, nullptr);
    const char*   kinds[] = {"Static Mesh", "Material", "Texture", "Scene", "Shader", "Script", "File", "Folder"};
    const ZUIIcon icons[] = {ZUIIcon::Box, ZUIIcon::Workflow, ZUIIcon::Image, ZUIIcon::Engine, ZUIIcon::Workflow, ZUIIcon::Code, ZUIIcon::Square, ZUIIcon::Folder};
    const auto*   tile    = row->FirstChild->NextSib; // Skip left padding.
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_NE(tile, nullptr);
        ASSERT_NE(tile->FirstChild, nullptr);
        ASSERT_NE(tile->FirstChild->FirstChild, nullptr);
        EXPECT_EQ(tile->FirstChild->FirstChild->IconId, static_cast<uint16_t>(icons[i]));
        EXPECT_TRUE(tile->FirstChild->FirstChild->Flags & ZUI_AssetIcon);
        const auto* kind = tile->LastChild->PrevSib->LastChild;
        ASSERT_NE(kind, nullptr);
        EXPECT_EQ(std::string_view(kind->Label.Ptr, kind->Label.Len), kinds[i]);
        tile = tile->NextSib->NextSib; // Skip the inter-tile gap.
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, SceneFileArtworkMatchesExactExtensionsAtEveryThumbnailScale)
{
    using namespace ZEngine::UI;
    using Access                                          = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto                                          panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZEngine::Core::Containers::Array<VFSDirEntry> listing = {};
    listing.init(&scratch, 4);
    listing.push({{}, VFSPath::Parse("/World.zescene").Value(), false});
    listing.push({{}, VFSPath::Parse("/World.ZeScEnE").Value(), false});
    listing.push({{}, VFSPath::Parse("/World.zescene.backup").Value(), false});
    listing.push({{}, VFSPath::Parse("/Folder.zescene").Value(), true});
    Access::Listing(*panel, listing);
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    for (float scale : {.75f, 1.f, 1.5f})
    {
        Access::ThumbnailScale(*panel, scale);
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(1000.f), ZPx(600.f));
        EXPECT_EQ(Access::DrawGrid(*panel, ctx, 988.f, 600.f), 4);
        ZUIEndColumn(&ctx);
        for (int i = 0; i < 4; ++i)
        {
            char key[32] = {};
            snprintf(key, sizeof(key), "##pvc_%d", i);
            const auto* tile = FindBox(ctx.Root, key);
            ASSERT_NE(tile, nullptr);
            ASSERT_NE(tile->FirstChild, nullptr);
            const auto* icon = tile->FirstChild->FirstChild;
            ASSERT_NE(icon, nullptr);
            const bool directory = ZEngine::Helpers::secure_strcmp(Access::EntryName(*panel, i), "Folder.zescene") == 0;
            const bool scene     = ZEngine::Helpers::secure_strcmp(Access::Category(Access::EntryName(*panel, i)), "Scenes") == 0;
            EXPECT_EQ(icon->IconId, static_cast<uint16_t>(directory ? ZUIIcon::Folder : scene ? ZUIIcon::Engine : ZUIIcon::Square));
            EXPECT_TRUE(icon->Flags & ZUI_AssetIcon);
            EXPECT_FLOAT_EQ(icon->Size[0].Value, 38.f * scale);
            EXPECT_FLOAT_EQ(icon->Size[1].Value, 38.f * scale);
        }
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, ToolbarKeepsControlsRigidAndCollapsesOnlyMiddleBreadcrumbs)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::Navigate(*panel, "/");
    Access::Navigate(*panel, "/Content/Environments/City/Blockout/Walls");
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ZUIGlyph glyphs[224] = {};
    for (auto& glyph : glyphs)
        glyph.AdvanceX = 7.f;
    ZUIFont font                = {};
    font.Glyphs                 = glyphs;
    font.GlyphCount             = 224;
    font.LineHeight             = 13.f;
    ZUIFontAtlas atlas          = {};
    atlas.Body                  = &font;
    ctx.Atlas                   = &atlas;
    constexpr float disabled[4] = ZUI_TOKEN(Tokens::TextDisabled);
    for (float width : {1216.f, 560.f})
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(width), ZPx(286.f));
        Access::Toolbar(*panel, ctx, width);
        ZUIEndColumn(&ctx);
        ZUILayoutSolve(&ctx);
        const auto* bar = FindBox(ctx.Root, ZUIHashStr("##pv_toolbar", 12));
        ASSERT_NE(bar, nullptr);
        EXPECT_FLOAT_EQ(bar->ComputedSize[1], 36.f);
        EXPECT_FLOAT_EQ(bar->Padding[0], 10.f);
        EXPECT_FLOAT_EQ(bar->Gap, 8.f);
        const auto* save = FindBox(bar, ZUIHashStr("##pv_saveall", 12));
        ASSERT_NE(save, nullptr);
        EXPECT_TRUE(save->Flags & ZUI_Disabled);
        for (int channel = 0; channel < 4; ++channel)
            EXPECT_FLOAT_EQ(save->LastChild->TextColor[channel], disabled[channel]);
        for (auto* control = bar->FirstChild; control && control != save->NextSib; control = control->NextSib)
        {
            EXPECT_FLOAT_EQ(control->ComputedSize[1], 26.f);
            EXPECT_FLOAT_EQ(control->ScreenMin[1], 5.f);
            EXPECT_FLOAT_EQ(control->Size[0].Strictness, 1.f);
        }
        EXPECT_EQ(FindBox(bar, ZUIHashStr("##pv_settings", 13)), nullptr);
        const auto* current = FindBox(bar, ZUIHashStr("##pv_bc_5", 9));
        ASSERT_NE(current, nullptr);
        EXPECT_EQ(std::string_view(current->Label.Ptr, current->Label.Len), "Walls");
        EXPECT_FALSE(current->Flags & ZUI_Clickable);
        EXPECT_LE(current->ScreenMax[0], width - 10.f);
        EXPECT_NE(FindBox(bar, ZUIHashStr("##pv_bc_0", 9)), nullptr);
        EXPECT_EQ(FindBox(bar, ZUIHashStr("##pv_bc_more", 12)) != nullptr, width == 560.f);
        EXPECT_EQ(FindBox(bar, ZUIHashStr("##pv_bc_1", 9)) != nullptr, width == 1216.f);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, BreadcrumbAncestorClickNavigatesAndHistoryArrowStatesReflectTheCursor)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::Navigate(*panel, "/");
    Access::Navigate(*panel, "/Content/Blockout");
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.HotKey = ctx.ActiveKey = ZUIHashStr("##pv_bc_1", 9);
    ctx.MouseReleased[0]       = true;
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBeginColumn(&ctx, "root", ZPx(1216.f), ZPx(286.f));
    Access::Toolbar(*panel, ctx, 1216.f);
    ZUIEndColumn(&ctx);
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Content");
    EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Content/Blockout");
    ctx.MouseReleased[0] = false;
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBeginColumn(&ctx, "root", ZPx(1216.f), ZPx(286.f));
    Access::Toolbar(*panel, ctx, 1216.f);
    ZUIEndColumn(&ctx);
    const auto* back    = FindBox(ctx.Root, ZUIHashStr("##pv_back", 9));
    const auto* forward = FindBox(ctx.Root, ZUIHashStr("##pv_forward", 12));
    ASSERT_NE(back, nullptr);
    ASSERT_NE(forward, nullptr);
    EXPECT_TRUE(back->Flags & ZUI_Clickable);
    EXPECT_TRUE(forward->Flags & ZUI_Clickable);
    EXPECT_TRUE(Access::History(*panel, -1));
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBeginColumn(&ctx, "root", ZPx(1216.f), ZPx(286.f));
    Access::Toolbar(*panel, ctx, 1216.f);
    ZUIEndColumn(&ctx);
    back = FindBox(ctx.Root, ZUIHashStr("##pv_back", 9));
    ASSERT_NE(back, nullptr);
    EXPECT_TRUE(back->Flags & ZUI_Disabled);
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, CollapsedBreadcrumbMenuOpensAndNavigatesToTheChosenAncestor)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::Navigate(*panel, "/Content/Environment/City/Walls");
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ZUIGlyph glyphs[96] = {};
    for (auto& glyph : glyphs)
        glyph.AdvanceX = 7.f;
    ZUIFont font       = {};
    font.Glyphs        = glyphs;
    font.GlyphCount    = 96;
    font.LineHeight    = 13.f;
    ZUIFontAtlas atlas = {};
    atlas.Body         = &font;
    ctx.Atlas          = &atlas;
    ctx.ScreenW        = 560;
    ctx.ScreenH        = 286;
    const auto build   = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(560.f), ZPx(286.f));
        Access::Toolbar(*panel, ctx, 560.f);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    };
    ctx.FocusKey     = ZUIHashStr("##pv_bc_more", 12);
    ctx.EnterPressed = true;
    build();
    ASSERT_EQ(ctx.PopupStackSize, 1u);
    build();
    ASSERT_NE(ctx.PopupStack[0].Box, nullptr);
    const char item[] = "##pv_bcm_1";
    const auto target = ZUIHashStr(item, sizeof(item) - 1);
    EXPECT_NE(FindBox(ctx.PopupStack[0].Box, target), nullptr);
    ctx.FocusKey     = target;
    ctx.EnterPressed = true;
    build();
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Content");
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, BreadcrumbMenuStorageIsBoundedAcrossVisitsAndFolderNamesAreLiteral)
{
    using namespace ZEngine::UI;
    using Access     = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto       panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ZUIGlyph glyphs[96] = {};
    for (auto& glyph : glyphs)
        glyph.AdvanceX = 7.f;
    ZUIFont font         = {};
    font.Glyphs          = glyphs;
    font.GlyphCount      = 96;
    font.LineHeight      = 13.f;
    ZUIFontAtlas atlas   = {};
    atlas.Body           = &font;
    ctx.Atlas            = &atlas;
    ctx.ScreenW          = 560;
    ctx.ScreenH          = 286;
    uint32_t state_count = 0;
    for (int visit = 0; visit < 100; ++visit)
    {
        char path[VFS_MAX_PATH] = {};
        snprintf(path, sizeof(path), "/Folder##_%03d/Environment/City/Walls", visit);
        ASSERT_TRUE(Access::Navigate(*panel, path));
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(560.f), ZPx(286.f));
        ZUIOpenPopup(&ctx, "##pv_bc_menu", 0.f, 40.f);
        Access::Toolbar(*panel, ctx, 560.f);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        if (visit == 1)
            state_count = ctx.StateStore.Count;
        if (visit > 1)
            EXPECT_EQ(ctx.StateStore.Count, state_count);
    }
    const auto* item = FindBox(ctx.Root, ZUIHashStr("##pv_bcm_1", 10));
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(std::string_view(item->Label.Ptr, item->Label.Len), "Folder##_099");
    EXPECT_EQ(Access::HistoryCount(*panel), 64u);
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, AssetTileStatesMatchComponentMasterAndRetainTheFullNameTooltip)
{
    using namespace ZEngine::UI;
    using Access     = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto       panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ZUIGlyph glyphs[224] = {};
    for (auto& glyph : glyphs)
        glyph.AdvanceX = 7.f;
    ZUIFont font                  = {};
    font.Glyphs                   = glyphs;
    font.GlyphCount               = 224;
    font.LineHeight               = 13.f;
    ZUIFont small                 = font;
    small.LineHeight              = 12.f;
    ZUIFontAtlas atlas            = {};
    atlas.Body                    = &font;
    atlas.Small                   = &small;
    ctx.Atlas                     = &atlas;
    constexpr float colors[3][4]  = {ZUI_TOKEN(Tokens::BgSurface), ZUI_TOKEN(Tokens::BgHover), ZUI_TOKEN(Tokens::AccentSelection)};
    constexpr float borders[3][4] = {ZUI_TOKEN(Tokens::BorderSubtle), ZUI_TOKEN(Tokens::BorderStrong), ZUI_TOKEN(Tokens::BorderSelected)};
    constexpr float caption[4]    = ZUI_TOKEN(Tokens::TextSelected);
    const char*     names[]       = {"Triangle.ZEMESH", "Surface.zematerial", "Texture.zetextures", "Default.zescene", "Lighting.frag", "Player.lua", "notes.txt", "Meshes"};
    for (int state = 0; state < 3; ++state)
        for (int kind = 0; kind < 8; ++kind)
        {
            char tile_key[32] = {};
            snprintf(tile_key, sizeof(tile_key), "##pvc_%d", kind);
            const auto key       = ZUIHashStr(tile_key, static_cast<uint32_t>(ZEngine::Helpers::secure_strlen(tile_key)));
            ctx.HotKey           = state ? key : 0;
            ctx.TooltipHoverKey  = key;
            ctx.TooltipHoverTime = 0.4f;
            Access::Select(*panel, state == 2 ? names[kind] : "");
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            ZUIBeginColumn(&ctx, "root", ZPx(1000), ZPx(600));
            Access::DrawAssetTiles(*panel, ctx);
            ZUIEndColumn(&ctx);
            const auto* tile = FindBox(ctx.Root, tile_key);
            ASSERT_NE(tile, nullptr);
            EXPECT_FLOAT_EQ(tile->Size[0].Value, 110.f);
            EXPECT_FLOAT_EQ(tile->Size[1].Value, 128.f);
            EXPECT_FLOAT_EQ(tile->CornerRadii[0], Metrics::Radius3);
            const auto* thumbnail = tile->FirstChild;
            EXPECT_FLOAT_EQ(thumbnail->Size[1].Value, 79.f);
            EXPECT_FLOAT_EQ(thumbnail->FirstChild->Size[0].Value, 38.f);
            const auto* metadata = tile->LastChild->PrevSib;
            EXPECT_FLOAT_EQ(metadata->Size[1].Value, 46.f);
            EXPECT_FLOAT_EQ(metadata->Padding[0], 5.f);
            EXPECT_FLOAT_EQ(metadata->Padding[1], 8.f);
            EXPECT_FLOAT_EQ(metadata->Padding[3], 8.f);
            EXPECT_FLOAT_EQ(metadata->Gap, 4.f);
            EXPECT_EQ(metadata->FirstChild->FontSize, ZUIFontSize::Body);
            EXPECT_EQ(metadata->LastChild->FontSize, ZUIFontSize::Small);
            EXPECT_LE(metadata->FirstChild->Size[0].Value, 100.f);
            EXPECT_EQ(tile->LastChild->Flags, ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
            EXPECT_FLOAT_EQ(tile->LastChild->BorderThickness, 1.f);
            for (int channel = 0; channel < 4; ++channel)
            {
                EXPECT_FLOAT_EQ(tile->Colors[0][channel], colors[state][channel]);
                EXPECT_FLOAT_EQ(tile->LastChild->BorderColor[channel], borders[state][channel]);
                if (state == 2)
                    EXPECT_FLOAT_EQ(metadata->LastChild->TextColor[channel], caption[channel]);
            }
            if (state)
            {
                ASSERT_NE(ctx.ForegroundRoot, nullptr);
                ASSERT_NE(ctx.ForegroundRoot->FirstChild, nullptr);
                const auto& label = ctx.ForegroundRoot->FirstChild->Label;
                EXPECT_EQ(std::string_view(label.Ptr, label.Len), names[kind]);
            }
        }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, DefaultAndNarrowPanelsKeepAuthoredTreeWidthSearchInsetAndBodyHeight)
{
    using namespace ZEngine::UI;
    BrowserTestFonts fonts = {};
    ZUIContext       ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas = &fonts.atlas;
    for (float width : {1216.f, 560.f})
    {
        auto panel  = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
        // The dock owns the 30 px tab strip; BuildContent owns the other 256 px.
        ctx.ScreenW = width;
        ctx.ScreenH = 256.f;
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(width), ZPx(256.f));
        float rect[4] = {0.f, 0.f, width, 256.f};
        panel->BuildContent(&ctx, rect);
        ZUIEndColumn(&ctx);
        ZUILayoutSolve(&ctx);
        const auto* body   = FindBox(ctx.Root, "##pv_body");
        const auto* right  = FindBox(ctx.Root, "##pv_right");
        const auto* search = FindBox(ctx.Root, "##pv_search_row");
        const auto* filter = FindBox(ctx.Root, "##pv_filters");
        const auto* grid   = FindBox(ctx.Root, "##pv_grid_scroll");
        ASSERT_NE(body, nullptr);
        ASSERT_NE(right, nullptr);
        ASSERT_NE(search, nullptr);
        ASSERT_NE(filter, nullptr);
        ASSERT_NE(grid, nullptr);
        EXPECT_FLOAT_EQ(body->ComputedSize[1], 194.f);
        EXPECT_FLOAT_EQ(right->ScreenMin[0], 210.f);
        EXPECT_FLOAT_EQ(right->ComputedSize[0], width - 210.f);
        EXPECT_FLOAT_EQ(filter->ScreenMin[0], 222.f);
        EXPECT_FLOAT_EQ(filter->ScreenMin[1] - body->ScreenMin[1], 7.f);
        EXPECT_FLOAT_EQ(filter->ComputedSize[1], 26.f);
        EXPECT_FLOAT_EQ(search->ComputedSize[1], 33.f);
        EXPECT_FLOAT_EQ(grid->ScreenMin[1] - body->ScreenMin[1], 33.f);
        EXPECT_FLOAT_EQ(grid->ComputedSize[1], 161.f);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, FolderRowsMatchDefaultHoverAndSelectedComponentStates)
{
    using namespace ZEngine::UI;
    using Access     = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto       panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    constexpr float colors[3][4] = {
        {0.f, 0.f, 0.f, 0.f},
        ZUI_TOKEN(Tokens::BgHover), ZUI_TOKEN(Tokens::AccentSelection)
    };
    for (int state = 0; state < 3; ++state)
    {
        Access::Directory(*panel, state == 2 ? "/Assets" : "/Other");
        ctx.HotKey = state == 1 ? ZUIHashStr("/Assets", 7) : 0;
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(210), ZPx(600));
        Access::Draw(*panel, ctx, vfs, "");
        ZUIEndColumn(&ctx);
        const auto* row = ctx.Root->FirstChild;
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->Key, ZUIHashStr("/Assets", 7));
        EXPECT_FLOAT_EQ(row->Size[1].Value, 23.f);
        EXPECT_FLOAT_EQ(row->Padding[0], 9.f);
        EXPECT_FLOAT_EQ(row->Gap, 7.f);
        EXPECT_FLOAT_EQ(row->FirstChild->Size[0].Value, 9.f);
        const auto* icon = row->FirstChild->NextSib;
        EXPECT_FLOAT_EQ(icon->Size[0].Value, 13.f);
        EXPECT_EQ(icon->IconId, static_cast<uint16_t>(state == 2 ? ZUIIcon::FolderOpen : ZUIIcon::Folder));
        EXPECT_EQ(row->LastChild->FontSize, ZUIFontSize::Body);
        for (int channel = 0; channel < 4; ++channel)
            EXPECT_FLOAT_EQ(row->Colors[0][channel], colors[state][channel]);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, LargeAssetGridsBuildOnlyVisibleRowsAndPreserveFullScrollHeight)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::PopulateGrid(*panel, 512);
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(16), ZMega(2), 8192, 8192);
    ctx.Style.ReducedMotion = true;
    ctx.ScreenW             = 1000;
    ctx.ScreenH             = 300;
    auto* scroll            = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("##pv_grid_scroll", 16));
    for (int columns : {1, 2, 8})
    {
        const float content_height = 25.f + ((512 + columns - 1) / columns) * 138.f;
        for (float fraction : {0.f, .5f, 1.f})
        {
            scroll->MaxScrollY = content_height - 300.f;
            scroll->ScrollY = scroll->ScrollYTarget = scroll->MaxScrollY * fraction;
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            ZUIBeginColumn(&ctx, "root", ZPx(columns * 120.f + 12.f), ZPx(300.f));
            EXPECT_EQ(Access::DrawGrid(*panel, ctx, columns * 120.f, 300.f), 512);
            ZUIEndColumn(&ctx);
            ZUIEndFrame(&ctx);
            EXPECT_FLOAT_EQ(scroll->MaxScrollY, content_height - 300.f);
            EXPECT_LT(CountBoxes(ctx.Root), static_cast<uint32_t>(columns * 60 + 30));
            EXPECT_LT(ctx.FrameArena.m_current_offset, ZMega(1));
            if (fraction == 1.f)
                EXPECT_NE(FindBox(ctx.Root, ZUIHashStr("##pvc_511", 9)), nullptr);
        }
    }
    EXPECT_LE(ctx.StateStore.Count, 512u + 16u);
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, AssetIdentitySurvivesFilteringAndColumnReflow)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::PopulateGrid(*panel, 30);
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    const auto key = ZUIHashStr("##pvc_7", 7);
    for (int frame = 0; frame < 3; ++frame)
    {
        Access::Search(*panel, frame == 2 ? "Asset_007" : "");
        ctx.HotKey = ctx.FocusKey = key;
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(1000.f), ZPx(300.f));
        EXPECT_EQ(Access::DrawGrid(*panel, ctx, frame ? 240.f : 960.f, 300.f), frame == 2 ? 1 : 30);
        ZUIEndColumn(&ctx);
        const auto* tile = FindBox(ctx.Root, key);
        ASSERT_NE(tile, nullptr);
        const auto& name = tile->LastChild->PrevSib->FirstChild->Label;
        EXPECT_EQ(std::string_view(name.Ptr, name.Len), "Asset_007.txt");
        EXPECT_EQ(ctx.FocusKey, key);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, AssetGridClampsScrollAfterFilteringAndHandlesExtremeDimensions)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::PopulateGrid(*panel, 512);
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(16), ZMega(2), 8192, 8192);
    ctx.Style.ReducedMotion = true;
    auto* scroll            = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("##pv_grid_scroll", 16));
    scroll->MaxScrollY = scroll->ScrollY = scroll->ScrollYTarget = 50000.f;
    Access::Search(*panel, "Asset_007");
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBeginColumn(&ctx, "root", ZPx(400.f), ZPx(300.f));
    EXPECT_EQ(Access::DrawGrid(*panel, ctx, 240.f, 300.f), 1);
    EXPECT_FLOAT_EQ(scroll->ScrollY, 0.f);
    EXPECT_FLOAT_EQ(scroll->ScrollYTarget, 0.f);
    EXPECT_NE(FindBox(ctx.Root, ZUIHashStr("##pvc_7", 7)), nullptr);
    ZUIEndColumn(&ctx);

    for (float dimension : {-1.f, 0.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()})
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(400.f), ZPx(300.f));
        EXPECT_EQ(Access::DrawGrid(*panel, ctx, dimension, dimension), 1);
        ZUIEndColumn(&ctx);
        EXPECT_TRUE(std::isfinite(scroll->MaxScrollY));
        EXPECT_LT(CountBoxes(ctx.Root), 30u);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, RefreshRetiresGridInputWithoutClearingOtherPanelState)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::PopulateGrid(*panel, 1);
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    const auto grid  = ZUIHashStr("##pvc_0", 7);
    const auto other = ZUIHashStr("other_panel", 11);
    for (bool grid_input : {true, false})
    {
        const auto key   = grid_input ? grid : other;
        const auto popup = grid_input ? ZUIHashStr("##pvgctx_0", 10) : other;
        ctx.HotKey = ctx.ActiveKey = ctx.FocusKey = ctx.SavedFocusKey = ctx.TooltipHoverKey = ctx.DragSourceKey = key;
        ctx.TooltipHoverTime                                                                                    = 1.f;
        ctx.DragPayloadLen                                                                                      = 4;
        ctx.PopupStackSize                                                                                      = 1;
        ctx.PopupStack[0].Key = ctx.PendingPopupKey = popup;
        ctx.MouseReleased[0]                        = true;
        Access::ResetGridInput(*panel);
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(400.f), ZPx(300.f));
        Access::DrawGrid(*panel, ctx, 240.f, 300.f);
        ZUIEndColumn(&ctx);
        EXPECT_STREQ(Access::SelectedPath(*panel), "");
        EXPECT_EQ(ctx.HotKey, grid_input ? 0 : other);
        EXPECT_EQ(ctx.ActiveKey, grid_input ? 0 : other);
        EXPECT_EQ(ctx.FocusKey, grid_input ? 0 : other);
        EXPECT_EQ(ctx.SavedFocusKey, grid_input ? 0 : other);
        EXPECT_EQ(ctx.TooltipHoverKey, grid_input ? 0 : other);
        EXPECT_EQ(ctx.DragSourceKey, grid_input ? 0 : other);
        EXPECT_EQ(ctx.PopupStackSize, grid_input ? 0u : 1u);
        EXPECT_EQ(ctx.PendingPopupKey, grid_input ? 0 : other);
        EXPECT_EQ(ctx.DragPayloadLen, grid_input ? 0u : 4u);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, FileDialogButtonRowsFitTheFigmaControlHeight)
{
    using namespace ZEngine::UI;
    using Access     = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto       panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.ScreenW = 800;
    ctx.ScreenH = 600;
    for (int kind : {1, 2, 3, 4})
    {
        Access::OpenModal(*panel, kind);
        for (int frame = 0; frame < 2; ++frame)
        {
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(600));
            Access::DrawModal(*panel, ctx, vfs);
            ZUIEndColumn(&ctx);
            ZUIEndFrame(&ctx);
        }
        ASSERT_GT(ctx.PopupStackSize, 0u);
        const auto* popup = ctx.PopupStack[ctx.PopupStackSize - 1].Box;
        ASSERT_NE(popup, nullptr);
        const auto* buttons = FindBox(popup, "##pv_mbtns");
        ASSERT_NE(buttons, nullptr);
        EXPECT_GE(buttons->ComputedSize[1], 26.f);
        for (const auto* button = buttons->FirstChild; button; button = button->NextSib)
            if (button->Flags & ZUI_DrawText)
            {
                EXPECT_FLOAT_EQ(button->ComputedSize[1], 26.f);
                EXPECT_GE(button->ScreenMin[1], buttons->ScreenMin[1]);
                EXPECT_LE(button->ScreenMax[1], buttons->ScreenMax[1]);
            }
        ZUIClosePopup(&ctx);
    }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, TreeAndTileShareFolderMenuLayoutAndCreateInsideTheClickedFolder)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    for (bool tree : {true, false})
    {
        SCOPED_TRACE(tree);
        auto panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
        Access::Navigate(*panel, "/");
        ZEngine::Core::Containers::Array<VFSDirEntry> entries = {};
        entries.init(&scratch, 1);
        entries.push({{}, VFSPath::Parse("/Assets").Value(), true});
        Access::Listing(*panel, entries);
        ZUIContext       ctx   = {};
        BrowserTestFonts fonts = {};
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
        ctx.Atlas        = &fonts.atlas;
        ctx.ScreenW      = 800;
        ctx.ScreenH      = 600;
        const auto build = [&]() {
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(600));
            if (tree)
                Access::Draw(*panel, ctx, vfs, "");
            else
                Access::DrawGrid(*panel, ctx, 600.f, 400.f);
            ZUIEndColumn(&ctx);
            ZUIEndFrame(&ctx);
        };
        build(); // Consume the navigation reset before opening a tile popup.
        char popup_key[64] = {};
        if (tree)
            snprintf(popup_key, sizeof(popup_key), "##stctx_%llu", static_cast<unsigned long long>(ZUIHashStr("/Assets", 7)));
        else
            ZEngine::Helpers::secure_strcpy(popup_key, sizeof(popup_key), "##pvgctx_0");
        ZUIOpenPopup(&ctx, popup_key, 0.f, 40.f);
        build();
        build();
        const auto* popup = FindBox(ctx.Root, popup_key);
        ASSERT_NE(popup, nullptr);
        EXPECT_FLOAT_EQ(popup->ComputedSize[0], 200.f);
        EXPECT_FLOAT_EQ(popup->ComputedSize[1], 123.f);
        const char* keys[]     = {"##miex_Open", "##miex_New Folder", "##miex_Rename", "##miex_Delete"};
        float       label_x    = -1.f;
        float       previous_y = -1.f;
        for (const auto* key : keys)
        {
            const auto* item = FindBox(popup, key);
            ASSERT_NE(item, nullptr);
            const auto* label = item->FirstChild->NextSib;
            ASSERT_NE(label, nullptr);
            EXPECT_EQ(label->Flags & ZUI_DrawText, ZUI_DrawText);
            if (label_x < 0.f)
                label_x = label->ScreenMin[0];
            EXPECT_FLOAT_EQ(label->ScreenMin[0], label_x);
            EXPECT_GT(item->ScreenMin[1], previous_y);
            previous_y = item->ScreenMin[1];
        }
        ctx.FocusKey     = ZUIHashStr(keys[1], static_cast<uint32_t>(ZEngine::Helpers::secure_strlen(keys[1])));
        ctx.EnterPressed = true;
        build();
        EXPECT_EQ(Access::ModalKind(*panel), 2);
        EXPECT_STREQ(Access::ModalTarget(*panel), "/Assets");
        EXPECT_STREQ(Access::ModalName(*panel), "NewFolder");
        ZUIContextDestroy(&ctx);
    }
}

class ContentBrowserFileTest : public FolderTreeCacheTest
{
protected:
    struct RacingContext : VFSContext
    {
        VFSPath                HiddenDestination = {};
        VFSResult<VFSFileStat> Stat(const VFSPath& path) override
        {
            return path == HiddenDestination ? VFSResult<VFSFileStat>::Fail(VFSError::NotFound) : VFSContext::Stat(path);
        }
    };
    std::filesystem::path root       = {};
    VFSDiskBackend        disk       = {};
    RacingContext         filesystem = {};

    void                  SetUp() override
    {
        FolderTreeCacheTest::SetUp();
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        root              = std::filesystem::temp_directory_path() / ("zui-browser-crud-" + std::to_string(unique));
        ASSERT_TRUE(std::filesystem::create_directory(root));
        ASSERT_TRUE(std::filesystem::create_directory(root / "Assets"));
        disk.Initialize(root.string().c_str(), VFSBackendCaps::Read | VFSBackendCaps::Write | VFSBackendCaps::List, &memory.MainArena);
        filesystem.Initialize(&memory.MainArena, 8);
        ASSERT_TRUE(filesystem.Mount(&disk, VFSPath::Root(), 0).Succeeded());
    }
    void TearDown() override
    {
        filesystem.Shutdown();
        std::error_code error = {};
        if (!root.empty())
            std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error);
    }
    void Write(const char* path, const char* text)
    {
        auto opened = filesystem.Open(VFSPath::Parse(path).Value(), VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Truncate);
        ASSERT_TRUE(opened.Succeeded());
        const auto length = ZEngine::Helpers::secure_strlen(text);
        EXPECT_TRUE(opened.Value()->Write({reinterpret_cast<const uint8_t*>(text), length}, 0).Succeeded());
        filesystem.Close(opened.Value());
    }
    void ExpectContents(const char* path, const char* expected)
    {
        auto opened = filesystem.Open(VFSPath::Parse(path).Value(), VFSOpenFlags::Read);
        ASSERT_TRUE(opened.Succeeded());
        uint8_t    buffer[64] = {};
        const auto read       = opened.Value()->Read({buffer, sizeof(buffer)}, 0);
        ASSERT_TRUE(read.Succeeded());
        const auto length = ZEngine::Helpers::secure_strlen(expected);
        EXPECT_EQ(read.Value(), length);
        EXPECT_EQ(ZEngine::Helpers::secure_memcmp(buffer, sizeof(buffer), expected, length, length), 0);
        filesystem.Close(opened.Value());
    }
};

TEST_F(ContentBrowserFileTest, CreateAndRenameRejectConflictsIncludingAConcurrentCreation)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Write("/Assets/keep.txt", "keep these bytes");
    Write("/Assets/source.txt", "source bytes");
    for (bool racing : {false, true})
    {
        filesystem.HiddenDestination = racing ? VFSPath::Parse("/Assets/keep.txt").Value() : VFSPath{};
        Access::Operation(*panel, 1, "/Assets", false, "keep.txt");
        EXPECT_FALSE(Access::Submit(*panel, &filesystem));
        EXPECT_STREQ(Access::Error(*panel), "A file or folder with this name already exists");
        Access::Operation(*panel, 3, "/Assets/source.txt", false, "keep.txt");
        EXPECT_FALSE(Access::Submit(*panel, &filesystem));
        EXPECT_STREQ(Access::Error(*panel), "A file or folder with this name already exists");
        ExpectContents("/Assets/keep.txt", "keep these bytes");
        ExpectContents("/Assets/source.txt", "source bytes");
    }
}

TEST_F(ContentBrowserFileTest, FolderCreationRejectsAConcurrentDestinationWithoutChangingItsContents)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ASSERT_TRUE(std::filesystem::create_directory(root / "Assets" / "Existing"));
    Write("/Assets/Existing/keep.txt", "keep these bytes");
    // Simulate a directory being created after the dialog's name check.
    filesystem.HiddenDestination = VFSPath::Parse("/Assets/Existing").Value();
    Access::Operation(*panel, 2, "/Assets", true, "Existing");
    EXPECT_FALSE(Access::Submit(*panel, &filesystem));
    EXPECT_STREQ(Access::Error(*panel), "A file or folder with this name already exists");
    ExpectContents("/Assets/Existing/keep.txt", "keep these bytes");
}

TEST_F(ContentBrowserFileTest, OperationsValidateSinglePortableNamesAndDoNotEscapeTheTargetFolder)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Write("/Assets/source.txt", "source");
    for (int kind : {1, 2, 3})
        for (const char* name : {"", ".", "..", "../outside", "folder/child", "folder\\child", "/absolute", "C:drive", "bad:name", "bad*name", "bad?name", "bad|name", "bad\"name", "bad<name", "bad>name", "bad\nname", "trailing.", "trailing ", "   ", "CON", "nul.txt", "COM1.log", "lpt9"})
        {
            SCOPED_TRACE(name);
            Access::Operation(*panel, kind, kind == 3 ? "/Assets/source.txt" : "/Assets", kind == 2, name);
            EXPECT_FALSE(Access::Submit(*panel, &filesystem));
            EXPECT_NE(Access::Error(*panel)[0], '\0');
        }
    EXPECT_FALSE(std::filesystem::exists(root / "outside"));
    ExpectContents("/Assets/source.txt", "source");
    Access::Operation(*panel, 1, "/Assets", false, "COM10.txt");
    EXPECT_TRUE(Access::Submit(*panel, &filesystem));
    Access::Operation(*panel, 1, "/Assets", false, "unavailable.txt");
    EXPECT_FALSE(Access::Submit(*panel, nullptr));
    Access::Operation(*panel, 4, "/", true);
    EXPECT_FALSE(Access::Submit(*panel, &filesystem));
    EXPECT_TRUE(std::filesystem::exists(root / "Assets"));
}

TEST_F(ContentBrowserFileTest, SuccessfulCrudReturnsPooledFilesAndReconcilesAncestorNavigation)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    Access::Navigate(*panel, "/Assets");
    Access::Operation(*panel, 2, "/Assets", true, "Original");
    ASSERT_TRUE(Access::Submit(*panel, &filesystem));
    Access::Operation(*panel, 2, "/Assets/Original", true, "Child");
    ASSERT_TRUE(Access::Submit(*panel, &filesystem));
    Write("/Assets/Original/Child/keep.txt", "keep");
    Access::Navigate(*panel, "/Assets/Original");
    Access::Navigate(*panel, "/Assets/Original/Child");
    Access::Select(*panel, "/Assets/Original/Child/keep.txt");
    Access::ClickMarker(*panel, "/Assets/Original/Child/keep.txt");
    Access::Operation(*panel, 3, "/Assets/Original", true, "Renamed");
    ASSERT_TRUE(Access::Submit(*panel, &filesystem));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Assets/Renamed/Child");
    EXPECT_STREQ(Access::SelectedPath(*panel), "");
    EXPECT_FALSE(Access::HasClickMarker(*panel));
    EXPECT_TRUE(Access::History(*panel, -1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Assets/Renamed");
    EXPECT_TRUE(Access::History(*panel, 1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Assets/Renamed/Child");
    ExpectContents("/Assets/Renamed/Child/keep.txt", "keep");
    Access::Operation(*panel, 4, "/Assets/Renamed", true);
    ASSERT_TRUE(Access::Submit(*panel, &filesystem));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Assets");
    EXPECT_FALSE(std::filesystem::exists(root / "Assets" / "Renamed"));
    for (uint32_t i = 0; i < Access::HistoryCount(*panel); ++i)
        EXPECT_FALSE(VFSPath::Parse("/Assets/Renamed").Value().IsPrefixOf(VFSPath::Parse(Access::Visit(*panel, i)).Value()));

    // More successful creates than the 256-slot pool: closing only the native
    // handle, rather than returning the file to its owner, used to exhaust it.
    for (int i = 0; i < 300; ++i)
    {
        char name[32] = {};
        snprintf(name, sizeof(name), "created-%03d.txt", i);
        Access::Operation(*panel, 1, "/Assets", false, name);
        ASSERT_TRUE(Access::Submit(*panel, &filesystem));
    }
}

TEST_F(ContentBrowserFileTest, DialogRecipeFocusErrorsCancelAndEnterMatchTheApprovedBehavior)
{
    using namespace ZEngine::UI;
    using Access           = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto             panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext       ctx   = {};
    BrowserTestFonts fonts = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas        = &fonts.atlas;
    ctx.ScreenW      = 800;
    ctx.ScreenH      = 600;
    const auto owner = ZUIHashStr("browser_control", 15);
    const auto build = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(600));
        ZUIInvisibleButton(&ctx, "browser_control", ZPx(30), ZPx(22));
        Access::DrawModal(*panel, ctx, filesystem);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    };
    for (int kind : {1, 2, 3, 4})
    {
        SCOPED_TRACE(kind);
        Write("/Assets/source.txt", "source");
        Access::Operation(*panel, kind, kind <= 2 ? "/Assets" : "/Assets/source.txt", kind == 2);
        ctx.FocusKey = owner;
        build();
        build();
        build();
        const auto* popup   = FindBox(ctx.Root, "##pv_modal");
        const auto* title   = FindBox(ctx.Root, "##pv_modal_title");
        const auto* buttons = FindBox(ctx.Root, "##pv_mbtns");
        ASSERT_NE(popup, nullptr);
        ASSERT_NE(title, nullptr);
        ASSERT_NE(buttons, nullptr);
        EXPECT_FLOAT_EQ(popup->ComputedSize[0], 288.f);
        EXPECT_FLOAT_EQ(popup->ComputedSize[1], kind == 4 ? 106.f : 115.f);
        EXPECT_FLOAT_EQ(popup->ScreenMin[0] + popup->ScreenMax[0], 800.f);
        EXPECT_FLOAT_EQ(popup->ScreenMin[1] + popup->ScreenMax[1], 600.f);
        EXPECT_FLOAT_EQ(popup->Padding[0], 17.f);
        EXPECT_FLOAT_EQ(popup->BorderThickness, 1.f);
        EXPECT_FLOAT_EQ(popup->CornerRadii[0], 6.f);
        EXPECT_EQ(popup->ShadowKind, ZUIShadowKind::Modal);
        EXPECT_EQ(title->FontSize, ZUIFontSize::Heading);
        EXPECT_FLOAT_EQ(buttons->Gap, 8.f);
        EXPECT_EQ(BoxLabel(buttons->FirstChild), "Cancel");
        EXPECT_EQ(BoxLabel(buttons->LastChild), kind == 4 ? "Delete" : kind == 3 ? "Rename" : "Create");
        EXPECT_FLOAT_EQ(buttons->LastChild->ScreenMax[0], popup->ScreenMax[0] - 17.f);
        EXPECT_FLOAT_EQ(buttons->FirstChild->ComputedSize[0], 84.f);
        EXPECT_FLOAT_EQ(buttons->LastChild->ComputedSize[0], 84.f);
        EXPECT_EQ(ctx.SavedFocusKey, owner);
        if (kind == 4)
        {
            EXPECT_EQ(BoxLabel(title), "Delete " ZUI_GLYPH_LQUOTE "source.txt" ZUI_GLYPH_RQUOTE "?");
            EXPECT_EQ(ctx.FocusKey, ZUIHashStr("Delete##pvd", 11));
        }
        else
        {
            const auto* field = FindBox(ctx.Root, "##pv_mi");
            ASSERT_NE(field, nullptr);
            EXPECT_FLOAT_EQ(field->ComputedSize[0], 254.f);
            EXPECT_FLOAT_EQ(field->ComputedSize[1], 22.f);
            EXPECT_EQ(ctx.FocusKey, field->Key);
            EXPECT_EQ(ZUIStateGetOrInsert(&ctx.StateStore, field->Key)->SelectStart, 0);
        }
        ctx.EscapePressed = true;
        build();
        EXPECT_EQ(Access::ModalKind(*panel), 0);
        EXPECT_EQ(ctx.FocusKey, owner);
    }

    Access::Operation(*panel, 3, "/Assets/source.txt", false, "folder/child");
    build();
    build();
    ctx.EnterPressed = true;
    build();
    build();
    const auto* error = FindBox(ctx.Root, "##pv_modal_error");
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(BoxLabel(error), "Name must not contain path separators");
    EXPECT_TRUE(FindBox(ctx.Root, "##pv_mi")->Flags & ZUI_CustomFocusBorder);
    const auto* popup = FindBox(ctx.Root, "##pv_modal");
    EXPECT_FLOAT_EQ(popup->ComputedSize[1], 137.f);
    constexpr float red[4] = ZUI_TOKEN(Tokens::AccentRed);
    for (int c = 0; c < 4; ++c)
    {
        EXPECT_FLOAT_EQ(error->TextColor[c], red[c]);
        EXPECT_FLOAT_EQ(FindBox(ctx.Root, "##pv_mi")->BorderColor[c], red[c]);
    }
    ctx.CtrlAPressed = true;
    ctx.TextInput[0] = 'X';
    ctx.TextInputLen = 1;
    build();
    EXPECT_EQ(FindBox(ctx.Root, "##pv_modal_error"), nullptr);
    EXPECT_FALSE(FindBox(ctx.Root, "##pv_mi")->Flags & ZUI_CustomFocusBorder);
    ctx.EnterPressed = true;
    build();
    EXPECT_EQ(Access::ModalKind(*panel), 0);
    ExpectContents("/Assets/X", "source");
    ZUIContextDestroy(&ctx);
}

TEST(ProjectViewPanelTest, WrappedHistoryRebasesAndPrunesOnlyTheMutatedSubtree)
{
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    for (int i = 0; i < 96; ++i)
    {
        char path[VFS_MAX_PATH] = {};
        snprintf(path, sizeof(path), "/%s/Visit_%03d", i % 2 ? "FolderSibling" : "Folder", i);
        ASSERT_TRUE(Access::Navigate(*panel, path));
    }
    ASSERT_EQ(Access::HistoryCount(*panel), 64u);
    Access::Mutated(*panel, "/Folder", "/Renamed");
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/FolderSibling/Visit_095");
    for (uint32_t i = 0; i < 64; ++i)
    {
        char expected[VFS_MAX_PATH] = {};
        snprintf(expected, sizeof(expected), "/%s/Visit_%03u", i % 2 ? "FolderSibling" : "Renamed", i + 32);
        EXPECT_STREQ(Access::Visit(*panel, i), expected);
    }
    Access::Mutated(*panel, "/Renamed");
    EXPECT_EQ(Access::HistoryCount(*panel), 32u);
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/FolderSibling/Visit_095");
    for (uint32_t i = 0; i < 32; ++i)
    {
        char expected[VFS_MAX_PATH] = {};
        snprintf(expected, sizeof(expected), "/FolderSibling/Visit_%03u", 33 + i * 2);
        EXPECT_STREQ(Access::Visit(*panel, i), expected);
    }
    ASSERT_TRUE(Access::History(*panel, -1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/FolderSibling/Visit_093");
    ASSERT_TRUE(Access::History(*panel, 1));
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/FolderSibling/Visit_095");
}

TEST_F(ContentBrowserFileTest, EmptyNamesDisableSubmissionAndKeyboardCancelNeverDeletes)
{
    using namespace ZEngine::UI;
    using Access           = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto             panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext       ctx   = {};
    BrowserTestFonts fonts = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas        = &fonts.atlas;
    ctx.ScreenW      = 800;
    ctx.ScreenH      = 600;
    const auto build = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(600));
        Access::DrawModal(*panel, ctx, filesystem);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    };
    for (int kind : {1, 2, 3})
    {
        Write("/Assets/source.txt", "keep");
        Access::Operation(*panel, kind, kind == 3 ? "/Assets/source.txt" : "/Assets", kind == 2, "");
        build();
        build();
        const auto* button = FindBox(ctx.Root, kind == 3 ? "Rename##pvr" : "Create##pvc");
        ASSERT_NE(button, nullptr);
        EXPECT_TRUE(button->Flags & ZUI_Disabled);
        ctx.EnterPressed = true;
        build();
        EXPECT_EQ(Access::ModalKind(*panel), kind);
        EXPECT_STREQ(Access::Error(*panel), "");
        ctx.EscapePressed = true;
        build();
    }
    Access::Operation(*panel, 4, "/Assets/source.txt", false);
    build();
    build();
    build();
    constexpr char cancel_key[] = "Cancel##pv_modal_cancel";
    ctx.FocusKey                = ZUIHashStr(cancel_key, sizeof(cancel_key) - 1);
    ctx.EnterPressed            = true;
    build();
    EXPECT_EQ(Access::ModalKind(*panel), 0);
    ExpectContents("/Assets/source.txt", "keep");
    Access::Operation(*panel, 4, "/Assets/source.txt", false);
    build();
    build();
    build();
    ctx.EnterPressed = true;
    build();
    EXPECT_EQ(Access::ModalKind(*panel), 0);
    EXPECT_FALSE(std::filesystem::exists(root / "Assets" / "source.txt"));
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, SearchFindsCollapsedDescendantsAndRetainsAncestorsCaseInsensitively)
{
    cache->UpdateSearch(vfs, scratch, "SPACESHIP");
    ASSERT_NE(Find("/Assets/Models/Spaceship"), nullptr);
    EXPECT_TRUE(Find("/Assets/Models/Spaceship")->Visible);
    EXPECT_TRUE(Find("/Assets/Models")->Visible);
    EXPECT_TRUE(Find("/Assets")->Visible);
    EXPECT_FALSE(Find("/Other")->Visible);
    EXPECT_EQ(Find("/.hidden"), nullptr);
    EXPECT_FALSE(cache->Searching());
}

TEST_F(FolderTreeCacheTest, QueryChangesAndIdleFramesReuseListings)
{
    const auto scratch_offset = scratch.m_current_offset;
    cache->UpdateSearch(vfs, scratch, "Spaceship");
    const auto calls = vfs.Calls;
    for (int i = 0; i < 100; ++i)
        cache->UpdateSearch(vfs, scratch, "Spaceship");
    cache->UpdateSearch(vfs, scratch, "other");
    EXPECT_FALSE(Find("/Assets")->Visible);
    EXPECT_TRUE(Find("/Other")->Visible);
    cache->UpdateSearch(vfs, scratch, "");
    EXPECT_TRUE(Find("/Assets")->Visible);
    EXPECT_EQ(vfs.Calls, calls);
    EXPECT_EQ(scratch.m_current_offset, scratch_offset);
}

TEST_F(FolderTreeCacheTest, DiscoveryIsBudgetedAndIndicesStayStableAcrossFrames)
{
    cache->UpdateSearch(vfs, scratch, "Spaceship", 1);
    EXPECT_EQ(vfs.Calls, 1u);
    EXPECT_TRUE(cache->Searching());
    const auto* assets = Find("/Assets");
    ASSERT_NE(assets, nullptr);
    for (int frame = 0; frame < 10 && cache->Searching(); ++frame)
    {
        const auto calls = vfs.Calls;
        cache->UpdateSearch(vfs, scratch, "Spaceship", 1);
        EXPECT_LE(vfs.Calls - calls, 1u);
        EXPECT_EQ(Find("/Assets"), assets);
    }
    EXPECT_FALSE(cache->Searching());
    ASSERT_NE(Find("/Assets/Models/Spaceship"), nullptr);
}

TEST_F(FolderTreeCacheTest, RefreshInvalidatesCachedListingsAndFilters)
{
    cache->UpdateSearch(vfs, scratch, "Spaceship");
    vfs.Directories = {"/Other"};
    cache->Invalidate();
    cache->UpdateSearch(vfs, scratch, "Spaceship");
    EXPECT_EQ(Find("/Assets"), nullptr);
    ASSERT_NE(Find("/Other"), nullptr);
    EXPECT_FALSE(Find("/Other")->Visible);
}

TEST_F(FolderTreeCacheTest, FailedListingsAreNotRetriedEveryFrame)
{
    vfs.Fail = true;
    cache->UpdateSearch(vfs, scratch, "Spaceship");
    EXPECT_TRUE(cache->Incomplete);
    cache->UpdateSearch(vfs, scratch, "Spaceship");
    EXPECT_EQ(vfs.Calls, 1u);
    vfs.Fail = false;
    cache->Invalidate();
    cache->UpdateSearch(vfs, scratch, "Spaceship");
    EXPECT_FALSE(cache->Incomplete);
    EXPECT_NE(Find("/Assets/Models/Spaceship"), nullptr);
}

TEST_F(FolderTreeCacheTest, CapacityIsBoundedAndOverflowIsReported)
{
    vfs.Directories.clear();
    for (uint32_t i = 0; i < FolderTreeCache::Capacity + 8; ++i)
        vfs.Directories.push_back("/Folder" + std::to_string(i));
    cache->UpdateSearch(vfs, scratch, "Folder", 1);
    EXPECT_EQ(cache->Count, FolderTreeCache::Capacity);
    EXPECT_TRUE(cache->Incomplete);
    EXPECT_EQ(vfs.Calls, 1u);
    cache->UpdateSearch(vfs, scratch, "Folder");
    EXPECT_FALSE(cache->Searching());
    EXPECT_EQ(vfs.Calls, 1u);
}

TEST_F(FolderTreeCacheTest, EmptyAndOversizedQueriesAreSafeAndReuseListings)
{
    cache->UpdateSearch(vfs, scratch, nullptr);
    EXPECT_FALSE(cache->Searching());
    EXPECT_TRUE(Find("/Assets")->Visible);
    const std::string query(1024, 'x');
    cache->UpdateSearch(vfs, scratch, query.c_str());
    const auto calls = vfs.Calls;
    cache->UpdateSearch(vfs, scratch, query.c_str());
    EXPECT_EQ(vfs.Calls, calls);
    EXPECT_FALSE(Find("/Assets")->Visible);
}

TEST_F(FolderTreeCacheTest, FolderSearchDistinguishesProgressNoMatchesAndIncompleteResults)
{
    using namespace ZEngine::UI;
    using Access           = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto             panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext       ctx   = {};
    BrowserTestFonts fonts = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    ctx.Atlas   = &fonts.atlas;
    ctx.ScreenW = 210;
    ctx.ScreenH = 194;
    vfs.Directories.clear();
    for (int i = 0; i < 64; ++i)
        vfs.Directories.push_back("/Folder" + std::to_string(i));
    const auto build = [&]() {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginScrollRegion(&ctx, "##pv_src_scroll", ZPx(210.f), ZPx(194.f));
        Access::Draw(*panel, ctx, vfs, "zzz");
        ZUIEndScrollRegion(&ctx);
        ZUIEndFrame(&ctx);
    };
    build();
    ASSERT_TRUE(Access::Cache(*panel).Searching());
    const auto* note = FindBox(ctx.Root, "##pv_src_searching");
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(BoxLabel(note->FirstChild), "Searching folders...");
    EXPECT_EQ(FindBox(ctx.Root, "##pv_src_no_match"), nullptr);
    EXPECT_FLOAT_EQ(note->Padding[0], 9.f);
    EXPECT_FLOAT_EQ(note->Padding[1], 8.f);
    EXPECT_EQ(note->FirstChild->FontSize, ZUIFontSize::Small);
    const float secondary[4] = ZUI_TOKEN(Tokens::TextSecondary);
    const float warning[4]   = ZUI_TOKEN(Tokens::AccentOrange);
    for (int frame = 0; frame < 10 && Access::Cache(*panel).Searching(); ++frame)
        build();
    EXPECT_FALSE(Access::Cache(*panel).Searching());
    EXPECT_EQ(FindBox(ctx.Root, "##pv_src_searching"), nullptr);
    note = FindBox(ctx.Root, "##pv_src_no_match");
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(BoxLabel(note->FirstChild), "No folders match " ZUI_GLYPH_LQUOTE "zzz" ZUI_GLYPH_RQUOTE ".");
    EXPECT_FLOAT_EQ(note->FirstChild->ScreenMin[0], 9.f);
    EXPECT_FLOAT_EQ(note->FirstChild->ScreenMin[1], 8.f);
    for (int c = 0; c < 4; ++c)
        EXPECT_FLOAT_EQ(note->FirstChild->TextColor[c], secondary[c]);
    const auto persistent_bytes = ctx.PersistentArena.m_current_offset;
    const auto state_count      = ctx.StateStore.Count;
    const auto listing_calls    = vfs.Calls;
    for (int frame = 0; frame < 128; ++frame)
    {
        char query[128] = {};
        snprintf(query, sizeof(query), "z##z-%04d", frame);
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginScrollRegion(&ctx, "##pv_src_scroll", ZPx(210.f), ZPx(194.f));
        Access::Draw(*panel, ctx, vfs, query);
        ZUIEndScrollRegion(&ctx);
        ZUIEndFrame(&ctx);
        note = FindBox(ctx.Root, "##pv_src_no_match");
        ASSERT_NE(note, nullptr);
        char expected[sizeof(query) + 32] = {};
        snprintf(expected, sizeof(expected), "No folders match " ZUI_GLYPH_LQUOTE "%s" ZUI_GLYPH_RQUOTE ".", query);
        EXPECT_EQ(BoxLabel(note->FirstChild), expected);
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, persistent_bytes);
        EXPECT_EQ(ctx.StateStore.Count, state_count);
        EXPECT_LT(CountBoxes(ctx.Root), 12u);
        EXPECT_LT(ctx.FrameArena.m_current_offset, ZKilo(64));
    }
    EXPECT_EQ(vfs.Calls, listing_calls); // Query changes reuse the settled folder index.
    Access::Cache(*panel).Invalidate();
    vfs.Fail = true;
    build();
    EXPECT_TRUE(Access::Cache(*panel).Incomplete);
    EXPECT_EQ(FindBox(ctx.Root, "##pv_src_no_match"), nullptr);
    note = FindBox(ctx.Root, "##pv_src_incomplete");
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(BoxLabel(note->FirstChild), "Folder index incomplete; refresh to retry.");
    EXPECT_EQ(note->FirstChild->FontSize, ZUIFontSize::Small);
    for (int c = 0; c < 4; ++c)
        EXPECT_FLOAT_EQ(note->FirstChild->TextColor[c], warning[c]);
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, LargeTreesVirtualizeRowsPreserveScrollHeightAndBoundPersistentState)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    vfs.Directories.clear();
    for (uint32_t i = 0; i < FolderTreeCache::Capacity - 1; ++i)
        vfs.Directories.push_back("/Folder" + std::to_string(i));
    Access::Cache(*panel).UpdateSearch(vfs, scratch, "Folder");

    ZUIContext       ctx   = {};
    BrowserTestFonts fonts = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(16), ZMega(2), 8192, 8192);
    ctx.Atlas          = &fonts.atlas;
    ctx.ScreenW        = 800;
    ctx.ScreenH        = 600;
    auto* scroll       = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("##pv_src_scroll", 15));
    scroll->ScreenMinY = 0.f;
    scroll->ScreenMaxY = 600.f;
    // Visit every row, both normally and during search. Recycling the viewport
    // must not accumulate separate expansion/row/chevron states per directory.
    for (const char* query : {"", "Folder"})
        for (uint32_t first = 0; first < FolderTreeCache::Capacity; first += 20)
        {
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            scroll->ScrollY = (float) first * 23.f;
            auto* region    = ZUIBeginScrollRegion(&ctx, "##pv_src_scroll", ZPx(210.f), ZPx(600.f));
            if (!query[0])
                ZUISpacer(&ctx, 23.f); // The root row is hidden during search.
            Access::Draw(*panel, ctx, vfs, query);
            ZUIEndScrollRegion(&ctx);
            ZUILayoutSolve(&ctx);

            float    height = 0.f;
            uint32_t boxes  = 1;
            for (const auto* row = region->FirstChild; row; row = row->NextSib)
            {
                if (!(row->Flags & ZUI_FloatY))
                    height += row->ComputedSize[1];
                ++boxes;
                for (auto* child = row->FirstChild; child; child = child->NextSib)
                    ++boxes;
            }
            EXPECT_FLOAT_EQ(height, (float) (FolderTreeCache::Capacity - (query[0] ? 1 : 0)) * 23.f + 20.f);
            EXPECT_LT(boxes, 200u);
            EXPECT_LE(ctx.StateStore.Count, FolderTreeCache::Capacity + 16);
        }
    ZUIContextDestroy(&ctx);
}

TEST_F(FolderTreeCacheTest, DisclosureClickOnlyExpandsAndSearchPreservesExpansionState)
{
    using namespace ZEngine::UI;
    using Access     = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto       panel = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    ZUIContext ctx   = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(8), ZMega(2), 1024, 1024);
    auto build = [&](const char* query = "") {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginScrollRegion(&ctx, "##pv_src_scroll", ZPx(210.f), ZPx(600.f));
        ZUISpacer(&ctx, 23.f);
        Access::Draw(*panel, ctx, vfs, query);
        ZUIEndScrollRegion(&ctx);
        ZUILayoutSolve(&ctx);
    };
    build();
    const auto key = ZUIHashStr("/Assets", 7);
    auto*      row = ZUIStateGetOrInsert(&ctx.StateStore, key);
    EXPECT_FLOAT_EQ(row->UserData, 0.f);
    ctx.HotKey = ctx.ActiveKey = key;
    ctx.MouseReleased[0]       = true;
    ctx.MousePos[0]            = row->ScreenMinX + 13.f; // Inside the 9 px disclosure icon.
    build();
    EXPECT_FLOAT_EQ(row->UserData, 1.f);
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "");
    ctx.MousePos[0] = row->ScreenMinX + 60.f; // The folder label navigates instead.
    build();
    EXPECT_STREQ(Access::CurrentDirectory(*panel), "/Assets");
    EXPECT_FLOAT_EQ(row->UserData, 1.f);
    ctx.MouseReleased[0] = false;
    build("Spaceship");
    build();
    EXPECT_FLOAT_EQ(row->UserData, 1.f);
    ZUIContextDestroy(&ctx);
}
