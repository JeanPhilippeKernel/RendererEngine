#include <Tetragrama/Panels/FolderTreeCache.h>
#include <Tetragrama/Panels/ProjectViewPanel.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUILayout.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
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
            const char* names[] = {"Triangle.ZEMESH", "Surface.zematerial", "Texture.zetextures", "Default.zescene", "Lighting.frag"};
            panel.m_nentries    = 0;
            for (const char* name : names)
            {
                auto& entry = panel.m_entries[panel.m_nentries++];
                ZEngine::Helpers::secure_strcpy(entry.name, sizeof(entry.name), name);
                ZEngine::Helpers::secure_strcpy(entry.full_path, sizeof(entry.full_path), name);
            }
            panel.DrawGrid(&ctx, 800.f);
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
    };
} // namespace Tetragrama::Panels

namespace
{
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
    const char*   kinds[] = {"Static Mesh", "Material", "Texture", "Level", "Shader"};
    const ZUIIcon icons[] = {ZUIIcon::Box, ZUIIcon::Workflow, ZUIIcon::Image, ZUIIcon::Map, ZUIIcon::Workflow};
    const auto*   tile    = row->FirstChild->NextSib; // Skip left padding.
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_NE(tile, nullptr);
        ASSERT_NE(tile->FirstChild, nullptr);
        ASSERT_NE(tile->FirstChild->FirstChild, nullptr);
        EXPECT_EQ(tile->FirstChild->FirstChild->IconId, static_cast<uint16_t>(icons[i]));
        const auto* kind = tile->LastChild->LastChild;
        ASSERT_NE(kind, nullptr);
        EXPECT_EQ(std::string_view(kind->Label.Ptr, kind->Label.Len), kinds[i]);
        tile = tile->NextSib->NextSib; // Skip the inter-tile gap.
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
        const ZUIBox* buttons = nullptr;
        for (auto* box = popup->FirstChild; box; box = box->NextSib)
            if (box->Key == ZUIHashStr("##pv_mbtns", 10))
                buttons = box;
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

TEST_F(FolderTreeCacheTest, LargeTreesVirtualizeRowsPreserveScrollHeightAndBoundPersistentState)
{
    using namespace ZEngine::UI;
    using Access = Tetragrama::Panels::ProjectViewPanelTestAccess;
    auto panel   = std::make_unique<Tetragrama::Panels::ProjectViewPanel>();
    vfs.Directories.clear();
    for (uint32_t i = 0; i < FolderTreeCache::Capacity - 1; ++i)
        vfs.Directories.push_back("/Folder" + std::to_string(i));
    Access::Cache(*panel).UpdateSearch(vfs, scratch, "Folder");

    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZMega(16), ZMega(2), 8192, 8192);
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
            auto* region    = ZUIBeginScrollRegion(&ctx, "##pv_src_scroll", ZFill(), ZFill());
            ZUISpacer(&ctx, 23.f); // The root row built by the full panel.
            Access::Draw(*panel, ctx, vfs, query);
            ZUIEndScrollRegion(&ctx);

            float    height = 0.f;
            uint32_t boxes  = 1;
            for (const auto* row = region->FirstChild; row; row = row->NextSib)
            {
                height += row->Size[1].Value;
                ++boxes;
                for (auto* child = row->FirstChild; child; child = child->NextSib)
                    ++boxes;
            }
            EXPECT_FLOAT_EQ(height, (float) FolderTreeCache::Capacity * 23.f);
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
