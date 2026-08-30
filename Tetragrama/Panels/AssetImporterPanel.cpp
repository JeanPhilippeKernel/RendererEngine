#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Panels/AssetImporterPanel.h>
#include <ZEngine/Core/Coroutine.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>
#include <string>

using namespace ZEngine::UI;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Helpers;
using namespace ZEngine::Importers;

namespace Tetragrama::Panels
{
    static constexpr float kBg[4]     = {0.09f, 0.09f, 0.095f, 1.f};
    static constexpr float kWhite[4]  = {0.80f, 0.80f, 0.80f, 1.0f};
    static constexpr float kGreen[4]  = {0.30f, 1.00f, 0.40f, 1.0f};
    static constexpr float kRed[4]    = {1.00f, 0.30f, 0.30f, 1.0f};

    // ── Initialize ────────────────────────────────────────────────────────────

    void AssetImporterPanel::Initialize(Tetragrama::Layers::ZUILayer* layer)
    {
        m_layer = layer;
        if (!layer)
            return;

        // Scratch arena for ImportConfiguration strings — carved from layer arena
        // ZKilo(64): ~8 path strings × ≤512 bytes each — a few KB is all we need
        layer->LocalArena.CreateSubArena(ZKilo(64), &m_local_arena);

        // Importer arenas carved from the engine's ImportPipeline budget so all
        // import memory — engine importers and editor importers — is budget-tracked.
        auto* import_arena = &ZEngine::Engine::GetContext()->ImportPipelineArena;
        import_arena->CreateSubArena(ZMega(64),  &m_gltf_importer_arena);
        import_arena->CreateSubArena(ZMega(128), &m_assimp_importer_arena);

        m_gltf_importer   = ZPushStructCtor(import_arena, ZEngine::Importers::GltfImporter);
        m_fbx_importer    = ZPushStructCtor(import_arena, ZEngine::Importers::FbxImporter);
        m_assimp_importer = ZPushStructCtor(import_arena, ZEngine::Importers::AssimpImporter);

        m_gltf_importer->Initialize(&m_gltf_importer_arena);
        m_fbx_importer->Initialize(import_arena);
        m_assimp_importer->Initialize(&m_assimp_importer_arena);
    }

    // ── BuildContent entry ────────────────────────────────────────────────────

    void AssetImporterPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        (void) rect;

        auto* app = m_layer ? reinterpret_cast<EditorPtr>(m_layer->CurrentApp) : nullptr;
        if (!app)
        {
            EmptyPanelBg(ctx, "##imp_bg", kBg, nullptr);
            return;
        }

        // Consume PendingImportPath written by viewport drag-drop or project right-click.
        // Goes directly to importing (like develop) — no Options shown for drag-drop.
        if (app->Configuration && app->Configuration->PendingImportPath[0] != '\0' &&
            m_state.value.load(std::memory_order_acquire) == ImporterState::Idle)
        {
            secure_strncpy(m_path_buf, sizeof(m_path_buf),
                           app->Configuration->PendingImportPath,
                           sizeof(m_path_buf) - 1);
            secure_strncpy(m_instance_name, sizeof(m_instance_name),
                           app->Configuration->PendingImportName,
                           sizeof(m_instance_name) - 1);
            app->Configuration->PendingImportPath[0] = '\0';
            app->Configuration->PendingImportName[0] = '\0';
            m_add_to_scene = true;
            StartImport();
        }

        // Consume TriggerScan posted by background thread (actor creation on main thread)
        TriggerScan();

        ZUIBox* bg   = ZUIBeginColumn(ctx, "##imp_bg", ZFill(), ZFill());
        bg->Flags    = bg->Flags | ZUI_DrawBackground | ZUI_Scrollable;
        ZUIBoxSetColorArr(bg, kBg);
        bg->EdgeSoftness = 0.f;

        switch (m_state.value.load(std::memory_order_acquire))
        {
            case ImporterState::Idle:      BuildIdle(ctx);      break;
            case ImporterState::Options:   BuildOptions(ctx);   break;
            case ImporterState::Importing: BuildImporting(ctx); break;
        }

        ZUIEndColumn(ctx);
    }

    // ── BuildIdle ─────────────────────────────────────────────────────────────

    void AssetImporterPanel::BuildIdle(ZUIContext* ctx)
    {
        float fh = ZUIGetFrameHeight(ctx);
        ZUISpacer(ctx, 10.f);

        // "+ Import File" button
        {
            ZUIBeginRow(ctx, "##imp_idle_r", ZFill(), ZPx(fh + 6.f));
            ZUISpacer(ctx, 8.f);
            ZUISignal btn = ZUIButton(ctx, "+ Import File##imp_browse");
            ZUIEndRow(ctx);
            if (btn.Flags & ZUI_SignalClicked)
                BrowseFile();
        }

        ZUISpacer(ctx, 6.f);
        {
            ZUIBeginRow(ctx, "##imp_drop_hint", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "or drag a 3D file onto the viewport", ctx->Theme.TextDim);
            ZUIEndRow(ctx);
        }

        // Recent imports
        if (m_hist_count > 0)
        {
            ZUISpacer(ctx, 12.f);
            ZUISeparator(ctx);
            ZUISpacer(ctx, 6.f);
            {
                ZUIBeginRow(ctx, "##imp_hist_hdr", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "Recent Imports", ctx->Theme.TextDim);
                ZUIEndRow(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // Newest-first (ring buffer: head-1 = newest)
            static const char* kRowKeys[kHistMax] = {
                "##hr0","##hr1","##hr2","##hr3","##hr4","##hr5","##hr6","##hr7",
                "##hr8","##hr9","##hra","##hrb","##hrc","##hrd","##hre","##hrf",
            };
            for (int i = 0; i < m_hist_count; ++i)
            {
                int idx = (m_hist_count - 1 - i) % kHistMax;
                const HistEntry& e = m_history[idx];
                char buf[320];
                if (e.ok)
                    snprintf(buf, sizeof(buf), "[OK]  %s", e.name);
                else
                    snprintf(buf, sizeof(buf), "[ERR] %s  (%s)", e.name, e.message);
                ZUIBeginRow(ctx, kRowKeys[i], ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, buf, e.ok ? kGreen : kRed);
                ZUIEndRow(ctx);
                ZUISpacer(ctx, 2.f);
            }
        }
    }

    // ── BuildOptions ──────────────────────────────────────────────────────────

    void AssetImporterPanel::BuildOptions(ZUIContext* ctx)
    {
        float fh = ZUIGetFrameHeight(ctx);
        ZUISpacer(ctx, 8.f);

        // Header: filename stem + X close button
        {
            char stem_buf[256] = {};
            auto pr = VFSPath::Parse(m_path_buf);
            if (pr.Succeeded())
            {
                auto s = pr.Value().Stem();
                snprintf(stem_buf, sizeof(stem_buf), "%.*s", (int)s.Length, s.Data);
            }
            ZUIBeginRow(ctx, "##imp_opt_hdr", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, stem_buf, ctx->Theme.TextDefault);

            ZUIBox* fill  = ZUIPushBox(ctx, "##imp_hfill", 11, ZUI_None);
            fill->Size[0] = ZFill(); fill->Size[1] = ZPx(1.f);
            ZUIPopBox(ctx);

            ZUIBox* xb     = ZUIPushBox(ctx, "##imp_close", 11, ZUI_DrawBackground | ZUI_Clickable | ZUI_DrawText);
            xb->Size[0]    = ZPx(fh); xb->Size[1] = ZPx(fh);
            xb->Label      = ZUIPushStr(&ctx->FrameArena, "x", 1);
            xb->TextAlign  = ZUITextAlign::Center;
            bool xhov      = (ctx->HotKey == xb->Key);
            ZUIBoxSetColor(xb, xhov ? 0.82f : 0.f, xhov ? 0.15f : 0.f, xhov ? 0.15f : 0.f, xhov ? 1.f : 0.f);
            xb->TextColor[0] = ctx->Theme.TextDim[0]; xb->TextColor[1] = ctx->Theme.TextDim[1];
            xb->TextColor[2] = ctx->Theme.TextDim[2]; xb->TextColor[3] = 1.f;
            ZUISignal xsig = ZUISignalFromBox(ctx, xb);
            ZUIPopBox(ctx);
            ZUIEndRow(ctx);

            if (xsig.Flags & ZUI_SignalClicked)
            {
                m_path_buf[0] = '\0';
                m_state.value.store(ImporterState::Idle, std::memory_order_release);
            }
        }

        ZUISeparator(ctx);
        ZUISpacer(ctx, 6.f);

        // ── Filter pill row (UE5-style horizontal filter) ─────────────────────
        {
            static const char* kPills[]    = {"General","Mesh","Material","Animation","LOD","All"};
            static const char* kPillKeys[] = {
                "##pf_gen","##pf_mesh","##pf_mat","##pf_anim","##pf_lod","##pf_all"
            };
            ZUIBeginRow(ctx, "##imp_filter_row", ZFill(), ZPx(fh + 6.f));
            ZUISpacer(ctx, 8.f);
            for (int pi = 0; pi < 6; ++pi)
            {
                bool disabled = (pi == 3 || pi == 4);
                if (disabled) ZUIBeginDisabled(ctx);

                bool    act  = (m_options_filter == pi);
                uint32_t pln = (uint32_t)strlen(kPills[pi]);
                ZUIBox* pb   = ZUIPushBox(ctx, kPillKeys[pi], (uint32_t)strlen(kPillKeys[pi]),
                                          ZUI_DrawBackground | ZUI_Clickable | ZUI_DrawText);
                pb->Size[0]    = ZText();
                pb->Size[1]    = ZPx(fh + 4.f);
                pb->Label      = ZUIPushStr(&ctx->FrameArena, kPills[pi], pln);
                pb->TextAlign  = ZUITextAlign::Center;
                pb->Padding[0] = 10.f;
                pb->Padding[2] = 10.f;
                ZUIBoxSetCornerRadius(pb, 3.f);
                if (act)
                {
                    ZUIBoxSetColorArr(pb, ctx->Theme.TabActiveBg);
                    pb->TextColor[0] = pb->TextColor[1] = pb->TextColor[2] = pb->TextColor[3] = 1.f;
                }
                else
                {
                    bool hov = (ctx->HotKey == pb->Key);
                    ZUIBoxSetColor(pb, 1.f, 1.f, 1.f, hov ? 0.07f : 0.f);
                    pb->TextColor[0] = ctx->Theme.TextDim[0]; pb->TextColor[1] = ctx->Theme.TextDim[1];
                    pb->TextColor[2] = ctx->Theme.TextDim[2]; pb->TextColor[3] = 1.f;
                }
                ZUISignal psig = ZUISignalFromBox(ctx, pb);
                ZUIPopBox(ctx);
                ZUISpacer(ctx, 4.f);
                if (!disabled && (psig.Flags & ZUI_SignalClicked))
                    m_options_filter = pi;

                if (disabled) ZUIEndDisabled(ctx);
            }
            ZUIEndRow(ctx);
        }
        ZUISeparator(ctx);

        // ── Filtered settings (scroll region) ─────────────────────────────────
        ZUIBeginScrollRegion(ctx, "##imp_opt_scroll", ZFill(), ZFill());
        {
            // Section header helper
            static constexpr float kLblW = 140.f;
            auto sec_hdr = [&](const char* key, const char* label) {
                ZUISpacer(ctx, 10.f);
                ZUIBeginRow(ctx, key, ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, label, ctx->Theme.TextDim);
                ZUIEndRow(ctx);
                ZUISeparator(ctx);
                ZUISpacer(ctx, 6.f);
            };
            // 2-column row helper (label left, widget fills right)
#define IMP_ROW_BEGIN(key) ZUIBeginRow(ctx, key, ZFill(), ZPx(fh + 4.f)); ZUISpacer(ctx, 8.f); ZUIBeginColumn(ctx, key"_l", ZPx(kLblW), ZFill());
#define IMP_ROW_MID(label) ZUILabel(ctx, label, ctx->Theme.TextDefault); ZUIEndColumn(ctx);
#define IMP_ROW_END ZUISpacer(ctx, 8.f); ZUIEndRow(ctx); ZUISpacer(ctx, 3.f);
            // Checkbox row (checkbox left, label right)
#define IMP_CB_ROW_BEGIN(key) ZUIBeginRow(ctx, key, ZFill(), ZPx(fh + 4.f)); ZUISpacer(ctx, 8.f);
#define IMP_CB_ROW_END(label) ZUISpacer(ctx, 8.f); ZUILabel(ctx, label, ctx->Theme.TextDefault); ZUIEndRow(ctx); ZUISpacer(ctx, 3.f);

            bool show_gen  = (m_options_filter == 0 || m_options_filter == 5);
            bool show_mesh = (m_options_filter == 1 || m_options_filter == 5);
            bool show_mat  = (m_options_filter == 2 || m_options_filter == 5);
            bool show_anim = (m_options_filter == 3 || m_options_filter == 5);
            bool show_lod  = (m_options_filter == 4 || m_options_filter == 5);

            // ── General / Common ──────────────────────────────────────────────
            if (show_gen)
            {
                sec_hdr("##imp_s_gen", "Common");

                IMP_CB_ROW_BEGIN("##imp_usn_r")
                ZUICheckbox(ctx, "##imp_usn", &m_use_source_name);
                IMP_CB_ROW_END("Use Source Name for Asset")

                IMP_ROW_BEGIN("##imp_aname_r")
                IMP_ROW_MID("Asset Name")
                {
                    char fn_buf[256] = {};
                    auto pr = VFSPath::Parse(m_path_buf);
                    if (pr.Succeeded()) { auto s = pr.Value().Stem(); snprintf(fn_buf, sizeof(fn_buf), "%.*s", (int)s.Length, s.Data); }
                    if (m_use_source_name) { ZUIBeginDisabled(ctx); ZUITextField(ctx, "##imp_name_dis", fn_buf, sizeof(fn_buf), 0.f); ZUIEndDisabled(ctx); }
                    else ZUITextField(ctx, "##imp_name", m_instance_name, sizeof(m_instance_name), 0.f);
                }
                IMP_ROW_END

                IMP_ROW_BEGIN("##imp_scale_r")
                IMP_ROW_MID("Offset Uniform Scale")
                ZUIDragFloat(ctx, "##imp_scale", &m_scale, 0.01f, 80.f);
                IMP_ROW_END

                IMP_ROW_BEGIN("##imp_axis_r")
                IMP_ROW_MID("Axis Up")
                {
                    static const char* kAxes[] = {"Y-Up", "Z-Up"};
                    if (ZUIBeginCombo(ctx, "##imp_axis", kAxes[m_axis_index]))
                    {
                        for (int i = 0; i < 2; ++i)
                            if (ZUIComboItem(ctx, kAxes[i], m_axis_index == i)) m_axis_index = i;
                        ZUIEndCombo(ctx);
                    }
                }
                IMP_ROW_END
            }

            // ── Mesh ──────────────────────────────────────────────────────────
            if (show_mesh)
            {
                sec_hdr("##imp_s_mesh", "Common Meshes");

                IMP_ROW_BEGIN("##imp_nrm_r")
                IMP_ROW_MID("Normals")
                {
                    static const char* kNrm[] = {"Off","Flat","Smooth"};
                    if (ZUIBeginCombo(ctx, "##imp_normals", kNrm[m_normals_mode]))
                    {
                        for (int i = 0; i < 3; ++i)
                            if (ZUIComboItem(ctx, kNrm[i], m_normals_mode == i)) m_normals_mode = i;
                        ZUIEndCombo(ctx);
                    }
                }
                IMP_ROW_END

                IMP_CB_ROW_BEGIN("##imp_mv_r")
                ZUICheckbox(ctx, "##imp_mv", &m_merge_vertices);
                IMP_CB_ROW_END("Merge Identical Vertices")

                IMP_CB_ROW_BEGIN("##imp_fuv_r")
                ZUICheckbox(ctx, "##imp_fuv", &m_flip_uvs);
                IMP_CB_ROW_END("Flip UVs")

                {
                    static bool s_keep_sections = false;
                    ZUIBeginDisabled(ctx);
                    IMP_CB_ROW_BEGIN("##imp_ks_r")
                    ZUICheckbox(ctx, "##imp_ks", &s_keep_sections);
                    IMP_CB_ROW_END("Keep Sections Separate")
                    ZUIEndDisabled(ctx);
                }
            }

            // ── Material ──────────────────────────────────────────────────────
            if (show_mat)
            {
                sec_hdr("##imp_s_mat", "Materials");

                IMP_CB_ROW_BEGIN("##imp_imat_r")
                ZUICheckbox(ctx, "##imp_imat", &m_import_materials);
                IMP_CB_ROW_END("Import Materials")

                IMP_CB_ROW_BEGIN("##imp_itex_r")
                ZUICheckbox(ctx, "##imp_itex", &m_import_textures);
                IMP_CB_ROW_END("Import Textures")
            }

            // ── Animation (disabled) ──────────────────────────────────────────
            if (show_anim)
            {
                ZUIBeginDisabled(ctx);
                sec_hdr("##imp_s_anim", "Common Skeletal Meshes and Animations");
                static bool s_import_anims = true, s_only_anims = false, s_bone_tracks = true;
                IMP_CB_ROW_BEGIN("##imp_ia_r")  ZUICheckbox(ctx, "##imp_ia",  &s_import_anims);  IMP_CB_ROW_END("Import Animations")
                IMP_CB_ROW_BEGIN("##imp_ioa_r") ZUICheckbox(ctx, "##imp_ioa", &s_only_anims);    IMP_CB_ROW_END("Import Only Animations")
                IMP_CB_ROW_BEGIN("##imp_ibt_r") ZUICheckbox(ctx, "##imp_ibt", &s_bone_tracks);   IMP_CB_ROW_END("Import Bone Tracks")
                ZUISpacer(ctx, 4.f);
                ZUIBeginRow(ctx, "##imp_anim_hint", ZFill(), ZPx(fh)); ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "Animation import requires skeletal mesh support.", ctx->Theme.TextDim);
                ZUIEndRow(ctx);
                ZUIEndDisabled(ctx);
            }

            // ── LOD (disabled) ────────────────────────────────────────────────
            if (show_lod)
            {
                ZUIBeginDisabled(ctx);
                sec_hdr("##imp_s_lod", "LOD");
                static bool s_import_lods = false;
                static int  s_max_lods    = 4;
                IMP_CB_ROW_BEGIN("##imp_il_r") ZUICheckbox(ctx, "##imp_il", &s_import_lods); IMP_CB_ROW_END("Import LODs")
                IMP_ROW_BEGIN("##imp_lod_n_r") IMP_ROW_MID("Max LOD Count")
                ZUISliderFloat(ctx, "##imp_lod_n", (float*)&s_max_lods, 1.f, 8.f);
                IMP_ROW_END
                ZUISpacer(ctx, 4.f);
                ZUIBeginRow(ctx, "##imp_lod_hint", ZFill(), ZPx(fh)); ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "LOD support requires virtual geometry streaming.", ctx->Theme.TextDim);
                ZUIEndRow(ctx);
                ZUIEndDisabled(ctx);
            }

#undef IMP_ROW_BEGIN
#undef IMP_ROW_MID
#undef IMP_ROW_END
#undef IMP_CB_ROW_BEGIN
#undef IMP_CB_ROW_END
        }
        ZUIEndScrollRegion(ctx);

        ZUISeparator(ctx);
        ZUISpacer(ctx, 6.f);
        ZUICheckbox(ctx, "Use same settings for subsequent files##imp_ss", &m_same_settings);
        ZUISpacer(ctx, 8.f);

        // Footer buttons
        {
            ZUIBeginRow(ctx, "##imp_footer", ZFill(), ZPx(fh + 4.f));
            ZUISpacer(ctx, 8.f);
            ZUISignal imp_btn = ZUIButton(ctx, "Import All##imp_do");
            ZUISpacer(ctx, 6.f);
            ZUIBeginDisabled(ctx);
            ZUIButton(ctx, "Preview...##imp_prev");
            ZUIEndDisabled(ctx);
            ZUISpacer(ctx, 6.f);
            ZUISignal cancel = ZUIButton(ctx, "Cancel##imp_cancel");
            ZUIEndRow(ctx);

            if (imp_btn.Flags & ZUI_SignalClicked)
                StartImport();
            if (cancel.Flags & ZUI_SignalClicked)
            {
                m_path_buf[0] = '\0';
                m_state.value.store(ImporterState::Idle, std::memory_order_release);
            }
        }
        ZUISpacer(ctx, 8.f);
    }

    // ── BuildImporting ────────────────────────────────────────────────────────

    void AssetImporterPanel::BuildImporting(ZUIContext* ctx)
    {
        float fh = ZUIGetFrameHeight(ctx);
        ZUISpacer(ctx, 8.f);

        // "Importing <filename>"
        {
            char stem_buf[256] = {};
            auto pr = VFSPath::Parse(m_path_buf);
            if (pr.Succeeded()) { auto s = pr.Value().Stem(); snprintf(stem_buf, sizeof(stem_buf), "Importing %.*s", (int)s.Length, s.Data); }
            else secure_strncpy(stem_buf, sizeof(stem_buf), "Importing...", 12);
            ZUIBeginRow(ctx, "##imp_ing_hdr", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, stem_buf, ctx->Theme.TextDefault);
            ZUIEndRow(ctx);
        }
        ZUISpacer(ctx, 6.f);

        // Progress bar
        {
            ZUIBeginRow(ctx, "##imp_prog_r", ZFill(), ZPx(18.f));
            ZUISpacer(ctx, 8.f);
            ZUIProgressBar(ctx, "##imp_prog", m_progress.value.load(std::memory_order_relaxed));
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);
        }

        ZUISpacer(ctx, 6.f);
        ZUISeparator(ctx);
        ZUISpacer(ctx, 4.f);

        // Log scroll region
        ZUIBeginScrollRegion(ctx, "##imp_log_scroll", ZFill(), ZFill());
        {
            std::lock_guard<std::mutex> lock(m_log_mutex);
            int start = (m_log_count < kLogMax)
                        ? 0
                        : m_log_head; // oldest first
            for (int i = 0; i < m_log_count; ++i)
            {
                int idx = (start + i) % kLogMax;
                char line[270];
                snprintf(line, sizeof(line), "> %s", m_log[idx].text);
                ZUILabel(ctx, line, m_log[idx].color);
            }
        }
        ZUIEndScrollRegion(ctx);
    }

    // ── File browse ───────────────────────────────────────────────────────────

    std::future<void> AssetImporterPanel::BrowseFileAsync()
    {
        if (!m_layer || !m_layer->CurrentApp)
            co_return;
        auto                          window  = m_layer->CurrentApp->CurrentWindow;
        std::vector<std::string_view> filters = {".glb", ".gltf", ".fbx", ".obj"};
        std::string                   picked  = co_await window->OpenFileDialogAsync(filters);
        if (!picked.empty())
        {
            secure_strncpy(m_path_buf, sizeof(m_path_buf), picked.c_str(), sizeof(m_path_buf) - 1);
            m_instance_name[0] = '\0';
            m_add_to_scene     = false;
            if (m_same_settings)
                StartImport();
            else
                m_state.value.store(ImporterState::Options, std::memory_order_release);
        }
    }

    void AssetImporterPanel::BrowseFile()
    {
        ZEngine::Core::MainThreadScheduler::Post(this, [](void* ctx) {
            reinterpret_cast<AssetImporterPanel*>(ctx)->BrowseFileAsync();
        });
    }

    // ── StartImport ───────────────────────────────────────────────────────────

    void AssetImporterPanel::StartImport()
    {
        if (!m_gltf_importer || !m_fbx_importer || !m_assimp_importer)
            return;

        auto* app = m_layer ? reinterpret_cast<EditorPtr>(m_layer->CurrentApp) : nullptr;
        if (!app || !app->Configuration)
            return;

        auto vfs_result = VFSPath::Parse(m_path_buf);
        if (vfs_result.Failed())
            return;

        auto& vfs_value  = vfs_result.Value();
        auto  asset_name = vfs_value.Stem();
        auto  parent_dir = vfs_value.Parent();

        char asset_file_buf[256];
        snprintf(asset_file_buf, sizeof(asset_file_buf), "%.*s.zemesh",
                 (int)asset_name.Length, asset_name.Data);

        m_local_arena.Clear();

        auto* config = ZPushStruct(&m_local_arena, ZEngine::Importers::AssetCodec::ImportConfiguration);
        const auto& cfg = *app->Configuration;
        config->OutputWorkingSpacePath.init(&m_local_arena, cfg.WorkingSpacePath.c_str());
        config->OutputTextureFilesPath.init(&m_local_arena, cfg.TexturePath.c_str());
        config->OutputAssetsPath.init(&m_local_arena, cfg.MeshPath.c_str());
        config->OutputMaterialPath.init(&m_local_arena, cfg.MaterialPath.c_str());
        if (!m_use_source_name && m_instance_name[0] != '\0')
            config->AssetName.init(&m_local_arena, m_instance_name);
        else
            config->AssetName.init(&m_local_arena, asset_name.Data);
        config->OutputAssetFile.init(&m_local_arena, asset_file_buf);
        config->InputBaseAssetFilePath.init(&m_local_arena, parent_dir.CStr());
        config->VFS           = reinterpret_cast<ZEngine::Core::VFS::IVFSContext*>(ZEngine::Engine::GetContext()->VFS);
        config->Options.UniformScale    = m_scale;
        config->Options.AxisUpIsZ       = (m_axis_index == 1);
        config->Options.NormalsMode     = static_cast<uint8_t>(m_normals_mode);
        config->Options.MergeVertices   = m_merge_vertices;
        config->Options.ImportMaterials = m_import_materials;
        config->Options.ImportTextures  = m_import_textures;
        config->Options.FlipUVs         = m_flip_uvs;

        char msg[512];
        snprintf(msg, sizeof(msg), "Importing %.*s", (int)vfs_value.Filename().Length, vfs_value.Filename().Data);
        PushLog(msg, kWhite[0], kWhite[1], kWhite[2]);

        m_state.value.store(ImporterState::Importing, std::memory_order_release);
        m_progress.value.store(0.f, std::memory_order_relaxed);

        auto cfg_copy = *config;
        auto ext      = vfs_value.Extension();
        std::string src_str(m_path_buf);

        if (secure_strcmp(ext.Data, ".glb") == 0 || secure_strcmp(ext.Data, ".gltf") == 0)
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit([this, src_str, cfg_copy, arena = &m_local_arena]() mutable {
                m_gltf_importer->ImportFile(src_str.c_str(), cfg_copy, arena, this,
                                            OnImportFileComplete, OnImportProgress,
                                            OnImportError, OnImportLog);
            });
        }
        else if (secure_strcmp(ext.Data, ".fbx") == 0)
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit([this, src_str, cfg_copy, arena = &m_local_arena]() mutable {
                m_fbx_importer->ImportFile(src_str.c_str(), cfg_copy, arena, this,
                                           OnImportFileComplete, OnImportProgress,
                                           OnImportError, OnImportLog);
            });
        }
        else
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit([this, src_str, cfg_copy, arena = &m_local_arena]() mutable {
                m_assimp_importer->ImportFile(src_str.c_str(), cfg_copy, arena, this,
                                              OnImportFileComplete, OnImportProgress,
                                              OnImportError, OnImportLog);
            });
        }
    }

    // ── TriggerScan (main-thread only) ────────────────────────────────────────

    void AssetImporterPanel::TriggerScan()
    {
        if (!m_pending_actor.valid)
            return;

        auto* app   = m_layer ? reinterpret_cast<EditorPtr>(m_layer->CurrentApp) : nullptr;
        auto* scene = app ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;
        auto* ctx   = ZEngine::Engine::GetContext();

        if (scene && ctx && ctx->ActorManager)
        {
            using namespace ZEngine::ECS::Components;
            ZEngine::ECS::ActorHandle handle = ctx->ActorManager->Create();
            ZEngine::ECS::Actor*      actor  = ctx->ActorManager->Access(handle);
            if (actor)
            {
                NameComponent nc = {};
                secure_strncpy(nc.Value, sizeof(nc.Value),
                               m_pending_actor.name, secure_strlen(m_pending_actor.name));
                actor->AddComponent<NameComponent>(nc);
                actor->AddComponent<TransformComponent>({});
                MeshComponent mc    = {};
                mc.MeshUUID         = m_pending_actor.uuid;
                mc.RenderInstanceId = m_pending_actor.render_id;
                actor->AddComponent<MeshComponent>(mc);
            }
        }
        m_pending_actor = {};
    }

    // ── PushLog / PushHistory ─────────────────────────────────────────────────

    void AssetImporterPanel::PushLog(const char* text, float r, float g, float b)
    {
        std::lock_guard<std::mutex> lock(m_log_mutex);
        LogEntry& e     = m_log[m_log_head];
        secure_strncpy(e.text, sizeof(e.text), text, sizeof(e.text) - 1);
        e.color[0] = r; e.color[1] = g; e.color[2] = b; e.color[3] = 1.f;
        m_log_head = (m_log_head + 1) % kLogMax;
        if (m_log_count < kLogMax) ++m_log_count;
        m_scroll_log = true;
    }

    void AssetImporterPanel::PushHistory(const char* name, bool ok, const char* msg)
    {
        if (m_hist_count < kHistMax)
        {
            HistEntry& e = m_history[m_hist_count++];
            secure_strncpy(e.name,    sizeof(e.name),    name, sizeof(e.name)    - 1);
            secure_strncpy(e.message, sizeof(e.message), msg,  sizeof(e.message) - 1);
            e.ok = ok;
        }
        else
        {
            // Shift to keep newest at the end
            for (int i = 0; i < kHistMax - 1; ++i)
                m_history[i] = m_history[i + 1];
            HistEntry& e = m_history[kHistMax - 1];
            secure_strncpy(e.name,    sizeof(e.name),    name, sizeof(e.name)    - 1);
            secure_strncpy(e.message, sizeof(e.message), msg,  sizeof(e.message) - 1);
            e.ok = ok;
        }
    }

    // ── Static callbacks ──────────────────────────────────────────────────────

    void AssetImporterPanel::OnImportFileComplete(
        void* ctx,
        ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs)
    {
        auto* self = reinterpret_cast<AssetImporterPanel*>(ctx);

        bool    has_mesh  = false;
        cstring mesh_path = nullptr;
        for (unsigned i = 0; i < outputs.size(); ++i)
        {
            if (outputs[i].Type == ZEngine::Importers::AssetFileType::MESH)
            {
                has_mesh  = true;
                mesh_path = outputs[i].Path.c_str();
            }
        }

        if (has_mesh && mesh_path)
        {
            // Write meta file (source path for re-import)
            auto* ctx_engine = ZEngine::Engine::GetContext();
            if (ctx_engine && ctx_engine->VFS)
            {
                auto* vfs = reinterpret_cast<ZEngine::Core::VFS::IVFSContext*>(ctx_engine->VFS);
                auto* app = self->m_layer ? reinterpret_cast<EditorPtr>(self->m_layer->CurrentApp) : nullptr;
                cstring ws = (app && app->Configuration) ? app->Configuration->WorkingSpacePath.c_str() : "";
                size_t  ws_len = secure_strlen(ws);

                if (ws_len > 0 && strncmp(mesh_path, ws, ws_len) == 0)
                {
                    auto rel = VFSPath::Parse(mesh_path + ws_len);
                    if (rel.Succeeded())
                    {
                        auto meta_result = ZEngine::Core::VFS::MetaFileIO::Read(*vfs, rel.Value());
                        ZEngine::Core::VFS::MetaFileData meta =
                            meta_result.Succeeded() ? meta_result.Value()
                                                    : ZEngine::Core::VFS::MetaFileData{};
                        secure_strncpy(meta.SourcePath,   sizeof(meta.SourcePath),   self->m_path_buf, sizeof(meta.SourcePath)   - 1);
                        secure_strncpy(meta.ArtifactPath, sizeof(meta.ArtifactPath), mesh_path,        sizeof(meta.ArtifactPath) - 1);
                        secure_strncpy(meta.ImporterName, sizeof(meta.ImporterName), "GltfImporter/AssimpImporter", sizeof(meta.ImporterName) - 1);
                        ZEngine::Core::VFS::MetaFileIO::Write(*vfs, rel.Value(), meta);
                    }
                }
            }

            // Add mesh instance to scene if triggered by drag-drop
            if (self->m_add_to_scene)
            {
                ZEngine::Importers::AssetCodec::AssetMeshFileHeader header{};
                if (ZEngine::Importers::AssetCodec::ReadAssetMeshFileHeader(mesh_path, header))
                {
                    auto* app   = self->m_layer ? reinterpret_cast<EditorPtr>(self->m_layer->CurrentApp) : nullptr;
                    auto* scene = app ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;
                    if (scene)
                    {
                        char iname[256] = {};
                        if (self->m_instance_name[0])
                            secure_strncpy(iname, sizeof(iname), self->m_instance_name, sizeof(iname) - 1);
                        else
                        {
                            auto pr = VFSPath::Parse(self->m_path_buf);
                            if (pr.Succeeded()) { auto s = pr.Value().Stem(); snprintf(iname, sizeof(iname), "%.*s", (int)s.Length, s.Data); }
                        }
                        uint32_t render_id = scene->AddMeshInstance(header.Id, iname);
                        self->m_pending_actor.uuid      = header.Id;
                        self->m_pending_actor.render_id = render_id;
                        self->m_pending_actor.valid     = true;
                        secure_strncpy(self->m_pending_actor.name, sizeof(self->m_pending_actor.name),
                                       iname, sizeof(self->m_pending_actor.name) - 1);
                    }
                }
            }
            self->m_add_to_scene     = false;
            self->m_instance_name[0] = '\0';

            self->PushLog("Completed", kGreen[0], kGreen[1], kGreen[2]);
            char fn[256] = {};
            { auto pr = VFSPath::Parse(self->m_path_buf); if (pr.Succeeded()) { auto f = pr.Value().Filename(); snprintf(fn, sizeof(fn), "%.*s", (int)f.Length, f.Data); } }
            self->PushHistory(fn, true, "Completed");
        }
        else
        {
            self->m_add_to_scene     = false;
            self->m_instance_name[0] = '\0';
            self->PushLog("Import failed — no mesh output", kRed[0], kRed[1], kRed[2]);
            char fn[256] = {};
            { auto pr = VFSPath::Parse(self->m_path_buf); if (pr.Succeeded()) { auto f = pr.Value().Filename(); snprintf(fn, sizeof(fn), "%.*s", (int)f.Length, f.Data); } }
            self->PushHistory(fn, false, "No mesh output");
        }

        self->m_progress.value.store(1.f, std::memory_order_relaxed);
        self->m_state.value.store(ImporterState::Idle, std::memory_order_release);
        ZEngine::Core::MainThreadScheduler::Post(self, [](void* c) {
            reinterpret_cast<AssetImporterPanel*>(c)->TriggerScan();
        });
    }

    void AssetImporterPanel::OnImportProgress(void* ctx, float pct)
    {
        auto* self = reinterpret_cast<AssetImporterPanel*>(ctx);
        char  msg[128];
        snprintf(msg, sizeof(msg), "Processing... %.0f%%", pct * 100.f);
        self->PushLog(msg, kWhite[0], kWhite[1], kWhite[2]);
        self->m_progress.value.store(pct, std::memory_order_relaxed);
    }

    void AssetImporterPanel::OnImportError(void* ctx, std::string_view err)
    {
        auto* self = reinterpret_cast<AssetImporterPanel*>(ctx);
        char  msg[512];
        snprintf(msg, sizeof(msg), "Error: %.*s", (int)err.size(), err.data());
        self->PushLog(msg, kRed[0], kRed[1], kRed[2]);
        char fn[256] = {};
        { auto pr = VFSPath::Parse(self->m_path_buf); if (pr.Succeeded()) { auto f = pr.Value().Filename(); snprintf(fn, sizeof(fn), "%.*s", (int)f.Length, f.Data); } }
        self->PushHistory(fn, false, msg);
        self->m_state.value.store(ImporterState::Idle, std::memory_order_release);
        ZEngine::Core::MainThreadScheduler::Post(self, [](void* c) {
            reinterpret_cast<AssetImporterPanel*>(c)->TriggerScan();
        });
    }

    void AssetImporterPanel::OnImportLog(void* ctx, std::string_view msg)
    {
        auto* self = reinterpret_cast<AssetImporterPanel*>(ctx);
        char  buf[256];
        snprintf(buf, sizeof(buf), "%.*s", (int)msg.size(), msg.data());
        self->PushLog(buf, kWhite[0], kWhite[1], kWhite[2]);
    }

} // namespace Tetragrama::Panels
