#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Panels/AssetImporterPanel.h>
#include <Tetragrama/Panels/ProjectViewPanel.h>
#include <ZEngine/Core/Coroutine.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/UI/ZUITokens.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <fmt/format.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace ZEngine::UI;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Helpers;
using namespace ZEngine::Importers;

namespace Tetragrama::Panels
{
    // Figma tokens (Editor tokens / Dark)
    static const float  kBg[4]    = ZUI_TOKEN(Tokens::BgPanel);
    static const float  kWhite[4] = ZUI_TOKEN(Tokens::TextPrimary);
    static const float  kGreen[4] = ZUI_TOKEN(Tokens::AccentGreen);
    static const float  kRed[4]   = ZUI_TOKEN(Tokens::AccentRed);

    // Figma "Button / Kind=Primary": accent/selection fill, 26 px, radius 4.
    static const float* Tokens_AccentBlue()
    {
        static const float c[4] = ZUI_TOKEN(Tokens::AccentBlue);
        return c;
    }

    static ZUISignal PrimaryButton(ZUIContext* ctx, const char* key, const char* label, ZUISize w)
    {
        char button_key[128] = {};
        snprintf(button_key, sizeof(button_key), "%s%s", label, key);
        return ZUIButton(ctx, button_key, w, ZPx(Metrics::HeightControl), ZUIButtonKind::Primary);
    }

    // Initialize

    void AssetImporterPanel::Initialize(Tetragrama::Layers::ZUILayer* layer, ProjectViewPanel* project_view)
    {
        m_layer        = layer;
        m_project_view = project_view;
        auto* engine   = ZEngine::Engine::GetContext();
        if (!layer || !engine)
            return;

        // Scratch arena for ImportConfiguration strings — carved from layer arena
        // ZKilo(64): ~8 path strings × ≤512 bytes each — a few KB is all we need
        layer->LocalArena.CreateSubArena(ZKilo(64), &m_local_arena, "EditorContext/AssetImporterPanelScratch");
        // Importer arenas carved from the engine's ImportPipeline budget so all
        // import memory — engine importers and editor importers — is budget-tracked.
        auto* import_arena = &engine->ImportPipelineArena;
        // Publication results can contain one entry per material and texture. Keep
        // them outside the 64 KiB configuration scratch arena and reuse storage on
        // the next import. The layer's shared local arena is only 4 MiB, so use the
        // import budget for this bounded 8 MiB result slab.
        m_import_result_slab.Init(import_arena, ZMega(8));
        m_import_task.PublishedPaths.init(&m_import_result_slab, 32);
        import_arena->CreateSubArena(ZMega(64), &m_gltf_importer_arena, "ImportPipeline/EditorGltfImporter");
        import_arena->CreateSubArena(ZMega(128), &m_assimp_importer_arena, "ImportPipeline/EditorAssimpImporter");

        m_gltf_importer   = ZPushStructCtor(import_arena, ZEngine::Importers::GltfImporter);
        m_fbx_importer    = ZPushStructCtor(import_arena, ZEngine::Importers::FbxImporter);
        m_assimp_importer = ZPushStructCtor(import_arena, ZEngine::Importers::AssimpImporter);

        m_gltf_importer->Initialize(&m_gltf_importer_arena);
        m_fbx_importer->Initialize(import_arena);
        m_assimp_importer->Initialize(&m_assimp_importer_arena);
    }

    // BuildContent entry

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
        if (app->Configuration && app->Configuration->PendingImportPath[0] != '\0' && m_state.value.load(std::memory_order_acquire) == ImporterState::Idle)
        {
            secure_strncpy(m_path_buf, sizeof(m_path_buf), app->Configuration->PendingImportPath, sizeof(m_path_buf) - 1);
            secure_strncpy(m_instance_name, sizeof(m_instance_name), app->Configuration->PendingImportName, sizeof(m_instance_name) - 1);
            app->Configuration->PendingImportPath[0] = '\0';
            app->Configuration->PendingImportName[0] = '\0';
            m_add_to_scene                           = true;
            m_state.value.store(ImporterState::Options, std::memory_order_release);
            StartImport();
        }

        ZUIBox* bg = ZUIBeginColumn(ctx, "##imp_bg", ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground | ZUI_ClipChildren;
        ZUIBoxSetColorArr(bg, kBg);
        bg->EdgeSoftness = 0.f;

        if (m_error[0])
        {
            auto* error       = ZUIBeginRow(ctx, "##imp_error", ZFill(), ZFit());
            error->Flags      = error->Flags | ZUI_Clickable | ZUI_NoKeyboardFocus;
            error->Padding[0] = error->Padding[2] = Metrics::Space12;
            error->Padding[1] = error->Padding[3] = Metrics::Space6;
            ZUILabelTight(ctx, m_error, kRed);
            ZUISetTooltip(ctx, ZUISignalFromBox(ctx, error), m_error);
            ZUIEndRow(ctx);
        }

        switch (m_state.value.load(std::memory_order_acquire))
        {
            case ImporterState::Idle:
                BuildIdle(ctx);
                break;
            case ImporterState::Options:
                BuildOptions(ctx);
                break;
            case ImporterState::Importing:
                BuildImporting(ctx);
                break;
        }

        ZUIEndColumn(ctx);
    }

    // Section chrome reuses the shared disclosure widget, with the importer
    // specimen's 28 px surface header and 12 px semibold label.
    static void ImportSection(ZUIContext* ctx, const char* label, bool* open)
    {
        const float surface[4] = ZUI_TOKEN(Tokens::BgSurface);
        ZUICollapsingHeader(ctx, label, open, surface);
        auto* header       = ctx->Current->LastChild;
        header->Size[1]    = ZPx(Metrics::HeightRow);
        header->Padding[0] = header->Padding[2] = Metrics::Space12;
        header->CrossAlign                      = ZUIAlign::Center;
        auto* arrow                             = header->FirstChild;
        arrow->Size[0] = arrow->Size[1] = ZPx(Metrics::Space10);
        arrow->NextSib->Size[0]         = ZPx(Metrics::Space8);
        header->LastChild->FontSize     = ZUIFontSize::Header;
        header->LastChild->TextAlign    = ZUITextAlign::Center;
    }

    static void BeginImportProperty(ZUIContext* ctx, const char* key, const char* label)
    {
        auto* row       = ZUIBeginRow(ctx, key, ZFill(), ZPx(30.f));
        row->Padding[0] = Metrics::Space20;
        row->Padding[2] = Metrics::Space12;
        row->CrossAlign = ZUIAlign::Center;
        row->Gap        = Metrics::Space12;
        ZUIBeginColumn(ctx, "##imp_property_label", ZPx(120.f), ZFit());
        ZUILabelTight(ctx, label);
        ZUIEndColumn(ctx);
    }

    static bool ImportCheckbox(ZUIContext* ctx, const char* key, const char* label, bool* value)
    {
        auto* row             = ZUIBeginRow(ctx, key, ZFill(), ZPx(Metrics::HeightRowCheck));
        row->Padding[0]       = Metrics::Space20;
        row->Padding[2]       = Metrics::Space12;
        row->CrossAlign       = ZUIAlign::Center;
        char control_key[128] = {};
        snprintf(control_key, sizeof(control_key), "%s%s", label, key);
        const bool changed = ZUICheckbox(ctx, control_key, value, 120.f);
        ZUIEndRow(ctx);
        return changed;
    }

    void AssetImporterPanel::BuildIdle(ZUIContext* ctx)
    {
        ZUISpacer(ctx, Metrics::Space12);
        auto* controls       = ZUIBeginColumn(ctx, "##imp_idle_controls", ZFill(), ZFit());
        controls->Padding[0] = controls->Padding[2] = Metrics::Space12;
        controls->Gap                               = Metrics::Space8;

        auto* dz                                    = ZUIBeginColumn(ctx, "##imp_dz", ZFill(), ZPx(140.f));
        dz->Flags                                   = dz->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DashedBorder | ZUI_Clickable | ZUI_KeyboardActivate | ZUI_ClipChildren;
        const float dz_bg[4]                        = ZUI_TOKEN(Tokens::BgWindow);
        const float dz_border[4]                    = ZUI_TOKEN(Tokens::BorderField);
        ZUIBoxSetColorArr(dz, dz_bg);
        secure_memcpy(dz->BorderColor, sizeof(dz->BorderColor), dz_border, sizeof(dz_border));
        dz->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(dz, Metrics::Radius6);
        dz->MainAlign = dz->CrossAlign = ZUIAlign::Center;
        dz->Gap                        = Metrics::Space8;
        ZUIIconBox(ctx, ZUIIcon::Upload, 24.f, ctx->Theme.TextDim);
        ZUILabelTight(ctx, "Drag a 3D file here");
        // USD is in the mockup but is not supported by the import pipeline.
        ZUILabelTight(ctx, "FBX, glTF, GLB, OBJ", ctx->Theme.TextDim, ZUIFontSize::Tiny);
        char               dropped[sizeof(m_path_buf)] = {};
        ZUIDragPayloadKind kind                        = ZUIDragPayloadKind::Opaque;
        if (ZUIAcceptDrop(ctx, dz, dropped, sizeof(dropped), &kind))
        {
            auto* app                        = m_layer ? reinterpret_cast<EditorPtr>(m_layer->CurrentApp) : nullptr;
            auto* engine                     = ZEngine::Engine::GetContext();
            char  native[sizeof(m_path_buf)] = {};
            if (ResolveDroppedFile(dropped, kind, app && app->Configuration ? app->Configuration->WorkingSpacePath.c_str() : nullptr, engine ? engine->EngineAssetsNativeRoot : nullptr, native, sizeof(native)))
                SelectSource(native);
        }
        const auto source_signal = ZUISignalFromBox(ctx, dz);
        ZUIEndColumn(ctx);
        if (source_signal.Flags & ZUI_SignalClicked)
            BrowseFile();
        const auto browse = PrimaryButton(ctx, "##imp_browse", "Import File", ZFill());
        if (browse.Flags & ZUI_SignalClicked)
            BrowseFile();
        ZUIEndColumn(ctx);
        ZUISpacer(ctx, Metrics::Space12);

        ImportSection(ctx, "Recent Imports", &m_history_open);
        if (!m_history_open)
            return;
        auto* history  = ZUIBeginScrollRegion(ctx, "##imp_history", ZFill(), ZFill());
        history->Flags = history->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(history, kBg);
        if (!m_hist_count)
        {
            auto* empty       = ZUIBeginRow(ctx, "##imp_history_empty", ZFill(), ZPx(26.f));
            empty->Padding[0] = Metrics::Space20;
            empty->CrossAlign = ZUIAlign::Center;
            ZUILabelTight(ctx, "No recent imports", ctx->Theme.TextDim, ZUIFontSize::Tiny);
            ZUIEndRow(ctx);
        }
        for (int i = 0; i < m_hist_count; ++i)
        {
            const auto& entry   = m_history[m_hist_count - 1 - i];
            char        key[32] = {};
            snprintf(key, sizeof(key), "##imp_history_%d", i);
            auto* row       = ZUIBeginRow(ctx, key, ZFill(), ZPx(26.f));
            row->Padding[0] = Metrics::Space20;
            row->Padding[2] = Metrics::Space12;
            row->Gap        = Metrics::Space8;
            row->CrossAlign = ZUIAlign::Center;
            row->Flags      = row->Flags | ZUI_ClipChildren | ZUI_Clickable | ZUI_NoKeyboardFocus;
            ZUIIconBox(ctx, entry.ok ? ZUIIcon::Check : ZUIIcon::X, 12.f, entry.ok ? kGreen : kRed);
            auto* name  = ZUIBeginColumn(ctx, "##imp_history_name", ZFill(), ZFit());
            name->Flags = name->Flags | ZUI_ClipChildren;
            ZUILabelTight(ctx, entry.name);
            ZUIEndColumn(ctx);
            ZUILabelTight(ctx, entry.ok ? "Imported" : "Failed", entry.ok ? ctx->Theme.TextDim : kRed, ZUIFontSize::Tiny);
            const auto signal = ZUISignalFromBox(ctx, row);
            ZUIEndRow(ctx);
            if (!entry.ok)
                ZUISetTooltip(ctx, signal, entry.message);
        }
        ZUIEndScrollRegion(ctx);
    }

    void AssetImporterPanel::CancelSelection()
    {
        m_path_buf[0]      = '\0';
        m_instance_name[0] = '\0';
        m_error[0]         = '\0';
        m_add_to_scene     = false;
        m_state.value.store(ImporterState::Idle, std::memory_order_release);
    }

    void AssetImporterPanel::BuildOptions(ZUIContext* ctx)
    {
        char       stem[256] = {};
        const auto parsed    = VFSPath::Parse(m_path_buf);
        if (parsed.Succeeded())
        {
            const auto source_stem = parsed.Value().Stem();
            snprintf(stem, sizeof(stem), "%.*s", static_cast<int>(source_stem.Length), source_stem.Data);
        }
        auto* header       = ZUIBeginRow(ctx, "##imp_opt_hdr", ZFill(), ZPx(36.f));
        header->Padding[0] = Metrics::Space12;
        header->Padding[2] = Metrics::Space8;
        header->Gap        = Metrics::Space8;
        header->CrossAlign = ZUIAlign::Center;
        header->Flags      = header->Flags | ZUI_ClipChildren;
        ZUIIconBox(ctx, ZUIIcon::Hexagon, 16.f, Tokens_AccentBlue());
        auto* name  = ZUIBeginColumn(ctx, "##imp_hname", ZFill(), ZFit());
        name->Flags = name->Flags | ZUI_ClipChildren;
        ZUILabelTight(ctx, stem, nullptr, ZUIFontSize::Header);
        ZUIEndColumn(ctx);
        const auto close     = ZUIButton(ctx, "##imp_close", ZPx(25.f), ZPx(25.f));
        auto*      close_box = ctx->Current->LastChild;
        // Icon-only buttons need the entire fixed-size content area, not the
        // horizontal text padding and text-driven width of ZUIButton.
        close_box->Size[0] = close_box->Size[1] = ZPx(25.f);
        for (float& padding : close_box->Padding)
            padding = 0.f;
        close_box->MainAlign = close_box->CrossAlign = ZUIAlign::Center;
        ctx->Current                                 = close_box;
        ZUIIconBox(ctx, ZUIIcon::X, 13.f, close_box->TextColor);
        ZUIPopBox(ctx);
        ZUISetTooltip(ctx, close, "Cancel import");
        ZUIEndRow(ctx);
        if (close.Flags & ZUI_SignalClicked)
        {
            CancelSelection();
            return;
        }

        // Only this body scrolls: the file header and actions remain reachable
        // in short docks. Flexible field widths also accommodate narrow docks.
        auto* body  = ZUIBeginScrollRegion(ctx, "##imp_opt_scroll", ZFill(), ZFill());
        body->Flags = body->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(body, kBg);
        ImportSection(ctx, "Mesh", &m_mesh_open);
        if (m_mesh_open)
        {
            BeginImportProperty(ctx, "##imp_aname_r", "Instance name");
            if (m_use_source_name)
                ZUIBeginDisabled(ctx);
            ZUITextField(ctx, m_use_source_name ? "##imp_name_dis" : "##imp_name", m_use_source_name ? stem : m_instance_name, sizeof(m_instance_name), 240.f);
            ctx->Current->LastChild->Size[0].Strictness = 0.f;
            if (m_use_source_name)
                ZUIEndDisabled(ctx);
            ZUIEndRow(ctx);

            if (ImportCheckbox(ctx, "##imp_usn", "Use source name", &m_use_source_name) && !m_use_source_name && !m_instance_name[0])
                secure_strcpy(m_instance_name, sizeof(m_instance_name), stem);
            // Preserve the existing, functional scale option (not in the specimen).
            BeginImportProperty(ctx, "##imp_scale_r", "Uniform scale");
            ZUIInputFloat(ctx, "##imp_scale", &m_scale, 80.f);
            ZUIEndRow(ctx);

            BeginImportProperty(ctx, "##imp_axis_r", "Up axis");
            static const char* axes[]      = {"Y-Up", "Z-Up"};
            const ZUISize      combo_width = {ZUISizeKind::Pixels, 200.f, 0.f};
            const int          axis        = m_axis_index >= 0 && m_axis_index < 2 ? m_axis_index : 0;
            if (ZUIBeginCombo(ctx, "##imp_axis", axes[axis], combo_width))
            {
                for (int i = 0; i < 2; ++i)
                    if (ZUIComboItem(ctx, axes[i], m_axis_index == i))
                        m_axis_index = i;
                ZUIEndCombo(ctx);
            }
            ZUIEndRow(ctx);

            BeginImportProperty(ctx, "##imp_nrm_r", "Normals");
            // Mode 0 keeps source normals; modes 1/2 generate missing normals.
            static const char* normals[] = {"Import from file", "Generate flat", "Generate smooth"};
            const int          normal    = m_normals_mode >= 0 && m_normals_mode < 3 ? m_normals_mode : 0;
            if (ZUIBeginCombo(ctx, "##imp_normals", normals[normal], combo_width))
            {
                for (int i = 0; i < 3; ++i)
                    if (ZUIComboItem(ctx, normals[i], m_normals_mode == i))
                        m_normals_mode = i;
                ZUIEndCombo(ctx);
            }
            ZUIEndRow(ctx);
            ImportCheckbox(ctx, "##imp_mv", "Merge vertices", &m_merge_vertices);
            ImportCheckbox(ctx, "##imp_fuv", "Flip UVs", &m_flip_uvs);
        }
        ImportSection(ctx, "Materials", &m_materials_open);
        if (m_materials_open)
        {
            ImportCheckbox(ctx, "##imp_imat", "Import materials", &m_import_materials);
            ImportCheckbox(ctx, "##imp_itex", "Import textures", &m_import_textures);
        }
        ImportSection(ctx, "Animation", &m_animation_open);
        if (m_animation_open)
        {
            auto* hint       = ZUIBeginRow(ctx, "##imp_anim_hint", ZFill(), ZFit());
            hint->Padding[0] = Metrics::Space20;
            hint->Padding[1] = hint->Padding[3] = Metrics::Space6;
            const float disabled[4]             = ZUI_TOKEN(Tokens::TextDisabled);
            ZUILabelTight(ctx, "Animation import requires skeletal mesh support.", disabled, ZUIFontSize::Tiny);
            ZUIEndRow(ctx);
        }
        ZUIEndScrollRegion(ctx);

        auto* footer           = ZUIBeginColumn(ctx, "##imp_footer_col", ZFill(), ZFit());
        footer->Flags          = footer->Flags | ZUI_DrawBackground | ZUI_ClipChildren;
        const float surface[4] = ZUI_TOKEN(Tokens::BgSurface);
        ZUIBoxSetColorArr(footer, surface);
        footer->EdgeSoftness = 0.f;
        footer->Padding[0] = footer->Padding[2] = Metrics::Space12;
        footer->Padding[1] = footer->Padding[3] = Metrics::Space10;
        footer->Gap                             = Metrics::Space8;
        ZUICheckbox(ctx, "Use same settings for subsequent files##imp_ss", &m_same_settings);
        ctx->Current->LastChild->Size[1] = ZPx(16.f);
        auto* buttons                    = ZUIBeginRow(ctx, "##imp_footer", ZFill(), ZPx(Metrics::HeightControl));
        buttons->MainAlign               = ZUIAlign::End;
        buttons->Gap                     = Metrics::Space8;
        const auto cancel                = ZUIButton(ctx, "Cancel##imp_cancel", ZPx(84.f));
        auto*      preview_help          = ZUIBeginRow(ctx, "##imp_preview_help", ZPx(84.f), ZPx(Metrics::HeightControl));
        preview_help->Flags              = preview_help->Flags | ZUI_Clickable | ZUI_NoKeyboardFocus;
        ZUIBeginDisabled(ctx);
        ZUIButton(ctx, "Preview...##imp_prev", ZFill());
        ZUIEndDisabled(ctx);
        ZUISetTooltip(ctx, ZUISignalFromBox(ctx, preview_help), "Import preview is not supported yet.");
        ZUIEndRow(ctx);
        const auto import = PrimaryButton(ctx, "##imp_do", "Import All", ZPx(84.f));
        for (auto* button = buttons->FirstChild; button; button = button->NextSib)
            button->Size[0].Strictness = 0.f;
        ZUIEndRow(ctx);
        ZUIEndColumn(ctx);
        if (cancel.Flags & ZUI_SignalClicked)
            CancelSelection();
        else if (import.Flags & ZUI_SignalClicked)
            StartImport();
    }

    void AssetImporterPanel::BuildImporting(ZUIContext* ctx)
    {
        ZUISpacer(ctx, 24.f);
        auto* content       = ZUIBeginColumn(ctx, "##imp_progress_content", ZFill(), ZFit());
        content->Padding[0] = content->Padding[2]   = 24.f;
        content->Gap                                = Metrics::Space10;
        char       heading[sizeof(m_path_buf) + 16] = {};
        const auto parsed                           = VFSPath::Parse(m_path_buf);
        const auto filename                         = parsed.Succeeded() ? parsed.Value().Filename() : VFSPathComponent{};
        snprintf(heading, sizeof(heading), "Importing %.*s", filename.Length ? static_cast<int>(filename.Length) : 3, filename.Length ? filename.Data : "...");
        ZUILabelTight(ctx, heading, nullptr, ZUIFontSize::Header);

        const float raw_progress = m_progress.value.load(std::memory_order_relaxed);
        const float progress     = std::isfinite(raw_progress) ? std::clamp(raw_progress, 0.f, 1.f) : 0.f;
        ZUIProgressBar(ctx, "##imp_prog", progress, ZFill(), ZPx(6.f));
        char status[sizeof(m_status) + 16] = {};
        {
            std::lock_guard<std::mutex> lock(m_log_mutex);
            secure_strcpy(status, sizeof(status), m_status[0] ? m_status : "Processing...");
        }
        const size_t status_length = secure_strlen(status);
        snprintf(status + status_length, sizeof(status) - status_length, " %.0f%%", progress * 100.f);
        ZUILabelTight(ctx, status, ctx->Theme.TextDim, ZUIFontSize::Tiny);
        auto* cancel_help  = ZUIBeginRow(ctx, "##imp_cancel_help", ZPx(84.f), ZPx(Metrics::HeightControl));
        cancel_help->Flags = cancel_help->Flags | ZUI_Clickable | ZUI_NoKeyboardFocus;
        ZUIBeginDisabled(ctx);
        ZUIButton(ctx, "Cancel##imp_cancel_running", ZFill());
        ZUIEndDisabled(ctx);
        ZUISetTooltip(ctx, ZUISignalFromBox(ctx, cancel_help), "Cancellation is unavailable while the import is running.");
        ZUIEndRow(ctx);
        ZUIEndColumn(ctx);
    }

    // File browse

    std::future<void> AssetImporterPanel::BrowseFileAsync()
    {
        if (!m_layer || !m_layer->CurrentApp || !m_layer->CurrentApp->CurrentWindow)
        {
            m_browse_pending = false;
            co_return;
        }
        try
        {
            auto                          window  = m_layer->CurrentApp->CurrentWindow;
            std::vector<std::string_view> filters = {".glb", ".gltf", ".fbx", ".obj"};
            std::string                   picked  = co_await window->OpenFileDialogAsync(filters, {}, "Select a 3D asset file");
            if (!picked.empty())
                SelectSource(picked.c_str());
        }
        catch (const std::exception& error)
        {
            snprintf(m_error, sizeof(m_error), "Unable to open the file dialog: %s", error.what());
        }
        m_browse_pending = false;
    }

    bool AssetImporterPanel::TryGetImporterKind(const VFSPath& path, ImporterKind& kind)
    {
        const auto ext          = path.Extension();
        char       extension[8] = {};
        if (ext.Length >= sizeof(extension))
            return false;
        for (size_t i = 0; i < ext.Length; ++i)
            extension[i] = (char) std::tolower((unsigned char) ext.Data[i]);
        if (ZEngine::Helpers::secure_strcmp(extension, ".glb") == 0 || ZEngine::Helpers::secure_strcmp(extension, ".gltf") == 0)
            kind = ImporterKind::Gltf;
        else if (ZEngine::Helpers::secure_strcmp(extension, ".fbx") == 0)
            kind = ImporterKind::Fbx;
        else if (ZEngine::Helpers::secure_strcmp(extension, ".obj") == 0)
            kind = ImporterKind::Assimp;
        else
            return false;
        return true;
    }

    bool AssetImporterPanel::SelectSource(const char* path)
    {
        if (!path || ZEngine::Helpers::secure_strlen(path) >= sizeof(m_path_buf) || m_state.value.load(std::memory_order_acquire) != ImporterState::Idle)
            return false;
        const auto   parsed = VFSPath::Parse(path);
        ImporterKind kind   = {};
        if (parsed.Failed() || !TryGetImporterKind(parsed.Value(), kind))
        {
            secure_strcpy(m_error, sizeof(m_error), "Select a GLB, glTF, FBX or OBJ file.");
            return false;
        }
        ZEngine::Helpers::secure_memcpy(m_path_buf, sizeof(m_path_buf), path, ZEngine::Helpers::secure_strlen(path) + 1);
        m_error[0]         = '\0';
        m_instance_name[0] = '\0';
        m_add_to_scene     = false;
        // Keep failed preflight/queue submissions recoverable in Options, even
        // when the user opted to reuse settings for subsequent files.
        m_state.value.store(ImporterState::Options, std::memory_order_release);
        if (m_same_settings)
            StartImport();
        return true;
    }

    void AssetImporterPanel::BrowseFile()
    {
        if (m_browse_pending || m_state.value.load(std::memory_order_acquire) != ImporterState::Idle || !m_layer || !m_layer->CurrentApp || !m_layer->CurrentApp->CurrentWindow)
            return;
        m_browse_pending = true;
        ZEngine::Core::MainThreadScheduler::Post(this, [](void* ctx) { reinterpret_cast<AssetImporterPanel*>(ctx)->BrowseFileAsync(); });
    }

    bool AssetImporterPanel::BuildImportConfiguration(AssetCodec::ImportConfiguration& config, IVFSContext* vfs)
    {
        m_error[0]        = '\0';
        auto*      app    = m_layer ? reinterpret_cast<EditorPtr>(m_layer->CurrentApp) : nullptr;
        const auto source = VFSPath::Parse(m_path_buf);
        if (!app || !app->Configuration || source.Failed())
        {
            secure_strcpy(m_error, sizeof(m_error), "Import requires a project and a valid source path.");
            return false;
        }
        const auto     stem            = source.Value().Stem();
        const char*    name            = m_use_source_name ? stem.Data : m_instance_name;
        const size_t   name_length     = m_use_source_name ? stem.Length : secure_strlen(m_instance_name);
        constexpr char suffix[]        = ".zemesh";
        char           asset_name[256] = {};
        char           asset_file[256] = {};
        bool           valid_name      = name_length > 0 && name_length + sizeof(suffix) <= sizeof(asset_file);
        for (size_t i = 0; i < name_length && valid_name; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            valid_name            = c >= 32 && c != 127 && !strchr("/\\:*?\"<>|", c);
        }
        valid_name = valid_name && name[name_length - 1] != '.' && name[name_length - 1] != ' ';
        if (!valid_name)
        {
            secure_strcpy(m_error, sizeof(m_error), "Asset name must be 1-248 characters, without path separators, reserved characters or a trailing dot/space.");
            return false;
        }
        if (!std::isfinite(m_scale) || m_scale <= 0.f || m_axis_index < 0 || m_axis_index > 1 || m_normals_mode < 0 || m_normals_mode > 2)
        {
            secure_strcpy(m_error, sizeof(m_error), "Use a finite, positive scale and valid axis/normal options.");
            return false;
        }
        secure_strncpy(asset_name, sizeof(asset_name), name, name_length);
        snprintf(asset_file, sizeof(asset_file), "%s%s", asset_name, suffix);

        m_local_arena.Clear();
        config          = {};
        const auto& cfg = *app->Configuration;
        config.OutputWorkingSpacePath.init(&m_local_arena, cfg.WorkingSpacePath.c_str());
        config.OutputTextureFilesPath.init(&m_local_arena, cfg.TexturePath.c_str());
        config.OutputAssetsPath.init(&m_local_arena, cfg.MeshPath.c_str());
        config.OutputMaterialPath.init(&m_local_arena, cfg.MaterialPath.c_str());
        config.AssetName.init(&m_local_arena, asset_name);
        config.OutputAssetFile.init(&m_local_arena, asset_file);
        config.InputBaseAssetFilePath.init(&m_local_arena, source.Value().Parent().CStr());
        config.VFS                     = vfs;
        config.Options.UniformScale    = m_scale;
        config.Options.AxisUpIsZ       = m_axis_index == 1;
        config.Options.NormalsMode     = static_cast<uint8_t>(m_normals_mode);
        config.Options.MergeVertices   = m_merge_vertices;
        config.Options.ImportMaterials = m_import_materials;
        config.Options.ImportTextures  = m_import_textures;
        config.Options.FlipUVs         = m_flip_uvs;
        if (AssetCodec::ValidateImportConfiguration(config).Failed())
        {
            secure_strcpy(m_error, sizeof(m_error), "The project import destinations are invalid or unavailable.");
            return false;
        }
        return true;
    }

    // StartImport

    void AssetImporterPanel::StartImport()
    {
        if (m_state.value.load(std::memory_order_acquire) == ImporterState::Importing)
            return;

        if (!m_gltf_importer || !m_fbx_importer || !m_assimp_importer)
            return;

        auto* app = m_layer ? reinterpret_cast<EditorPtr>(m_layer->CurrentApp) : nullptr;
        if (!app || !app->Configuration)
            return;

        auto vfs_result = VFSPath::Parse(m_path_buf);
        if (vfs_result.Failed())
            return;
        ImporterKind kind = {};
        if (!TryGetImporterKind(vfs_result.Value(), kind))
            return;

        auto*                           engine = ZEngine::Engine::GetContext();
        AssetCodec::ImportConfiguration config = {};
        if (!BuildImportConfiguration(config, engine ? engine->VFS : nullptr))
            return;
        std::error_code source_error = {};
        if (!std::filesystem::is_regular_file(std::filesystem::path(m_path_buf), source_error))
        {
            secure_strcpy(m_error, sizeof(m_error), "The selected source file is missing or cannot be read.");
            return;
        }

        const auto& vfs_value = vfs_result.Value();

        {
            std::lock_guard<std::mutex> lock(m_log_mutex);
            m_log_head = m_log_count = 0;
            m_status[0]              = '\0';
        }

        char msg[512] = {};
        snprintf(msg, sizeof(msg), "Importing %.*s", (int) vfs_value.Filename().Length, vfs_value.Filename().Data);
        PushLog(msg, kWhite[0], kWhite[1], kWhite[2]);

        m_state.value.store(ImporterState::Importing, std::memory_order_release);
        m_progress.value.store(0.f, std::memory_order_relaxed);

        m_import_task.Outputs.clear();
        m_import_task.PublishedPaths.clear();
        m_import_task.Error.clear();
        m_import_task.Outcome                           = ImportTaskOutcome::Pending;
        m_import_task.Publication                       = {};
        m_import_task.Panel                             = this;
        m_import_task.Configuration                     = config;
        m_import_task.Configuration.ArtifactContext     = &m_import_task;
        m_import_task.Configuration.OnArtifactPublished = &AssetImporterPanel::OnImportArtifactPublished;
        if (m_import_task.Configuration.VFS)
            m_import_task.Publication = m_import_task.Configuration.VFS->BeginImportPublication();
        secure_strncpy(m_import_task.SourcePath, sizeof(m_import_task.SourcePath), m_path_buf, secure_strlen(m_path_buf));

        m_import_task.Kind = kind;

        if (!ZEngine::Helpers::ThreadPoolHelper::Submit(&m_import_task, &AssetImporterPanel::RunImportTask))
        {
            EndImportPublication(m_import_task);
            m_state.value.store(ImporterState::Options, std::memory_order_release);
            secure_strcpy(m_error, sizeof(m_error), "Import task rejected because the thread pool is shutting down.");
            PushLog(m_error, kRed[0], kRed[1], kRed[2]);
        }
    }

    void AssetImporterPanel::RunImportTask(void* context)
    {
        auto* task = static_cast<ImportTask*>(context);
        if (!task)
            return;

        auto* panel = task->Panel;
        if (!panel)
        {
            EndImportPublication(*task);
            return;
        }

        const auto& config = task->Configuration;
        switch (task->Kind)
        {
            case ImporterKind::Gltf:
                panel->m_gltf_importer->ImportFile(task->SourcePath, config, &panel->m_local_arena, task, OnImportFileComplete, OnImportProgress, OnImportError, OnImportLog);
                break;
            case ImporterKind::Fbx:
                panel->m_fbx_importer->ImportFile(task->SourcePath, config, &panel->m_local_arena, task, OnImportFileComplete, OnImportProgress, OnImportError, OnImportLog);
                break;
            case ImporterKind::Assimp:
                panel->m_assimp_importer->ImportFile(task->SourcePath, config, &panel->m_local_arena, task, OnImportFileComplete, OnImportProgress, OnImportError, OnImportLog);
                break;
        }

        if (task->Outcome == ImportTaskOutcome::Pending)
        {
            task->Error   = "Importer returned without reporting a result";
            task->Outcome = ImportTaskOutcome::Failed;
        }

        // ImportFile's local scratch storage is gone once it returns. The worker
        // callbacks have already copied the required output paths into ImportTask.
        ZEngine::Core::MainThreadScheduler::Post(task, &AssetImporterPanel::FinalizeImportTask);
    }

    void AssetImporterPanel::FinalizeImportTask(void* context)
    {
        auto* task = static_cast<ImportTask*>(context);
        if (!task)
            return;

        auto* panel = task->Panel;
        if (panel)
        {
            if (task->Outcome == ImportTaskOutcome::Completed)
            {
                ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs(task->Outputs.data(), task->Outputs.size());
                CompleteImportOnMainThread(panel, outputs);
            }
            else
            {
                CompleteImportErrorOnMainThread(panel, task->Error.empty() ? "Importer returned without an error message" : task->Error);
            }
        }

        // CompleteImportOnMainThread can return early on metadata errors. Release the
        // publication afterwards in every case so watcher processing cannot remain
        // deferred after an import failure.
        EndImportPublication(*task);
        task->PublishedPaths.clear();
    }

    void AssetImporterPanel::CreateMeshActor(uuids::uuid mesh_uuid, uint32_t render_instance_id, const char* name)
    {
        auto* ctx = ZEngine::Engine::GetContext();

        if (ctx && ctx->ActorManager)
        {
            using namespace ZEngine::ECS::Components;
            ZEngine::ECS::ActorHandle handle = ctx->ActorManager->Create();
            ZEngine::ECS::Actor*      actor  = ctx->ActorManager->Access(handle);
            if (actor)
            {
                NameComponent nc = {};
                secure_strncpy(nc.Value, sizeof(nc.Value), name, secure_strlen(name));
                actor->AddComponent<NameComponent>(nc);
                actor->AddComponent<TransformComponent>({});
                MeshComponent mc    = {};
                mc.MeshUUID         = mesh_uuid;
                mc.RenderInstanceId = render_instance_id;
                actor->AddComponent<MeshComponent>(mc);
            }
        }
    }

    // PushLog / PushHistory

    void AssetImporterPanel::PushLog(const char* text, float r, float g, float b)
    {
        std::lock_guard<std::mutex> lock(m_log_mutex);
        LogEntry&                   e = m_log[m_log_head];
        secure_strncpy(e.text, sizeof(e.text), text, sizeof(e.text) - 1);
        e.color[0] = r;
        e.color[1] = g;
        e.color[2] = b;
        e.color[3] = 1.f;
        m_log_head = (m_log_head + 1) % kLogMax;
        if (m_log_count < kLogMax)
            ++m_log_count;
    }

    void AssetImporterPanel::PushHistory(const char* name, bool ok, const char* msg)
    {
        if (m_hist_count < kHistMax)
        {
            HistEntry& e = m_history[m_hist_count++];
            secure_strncpy(e.name, sizeof(e.name), name, sizeof(e.name) - 1);
            secure_strncpy(e.message, sizeof(e.message), msg, sizeof(e.message) - 1);
            e.ok = ok;
        }
        else
        {
            // Shift to keep newest at the end
            for (int i = 0; i < kHistMax - 1; ++i)
                m_history[i] = m_history[i + 1];
            HistEntry& e = m_history[kHistMax - 1];
            secure_strncpy(e.name, sizeof(e.name), name, sizeof(e.name) - 1);
            secure_strncpy(e.message, sizeof(e.message), msg, sizeof(e.message) - 1);
            e.ok = ok;
        }
    }

    // Static callbacks

    void AssetImporterPanel::OnImportFileComplete(void* ctx, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs)
    {
        auto* task = static_cast<ImportTask*>(ctx);
        if (!task)
            return;
        // An importer may have published some artifacts before reporting an error.
        // Once it reports that error, completion must not turn the task into a
        // success and hide it from the editor.
        if (task->Outcome == ImportTaskOutcome::Failed)
            return;

        task->Outputs.clear();
        if (outputs.size() > 0)
        {
            task->Outputs.assign(outputs.data(), outputs.data() + outputs.size());
            for (const auto& output : task->Outputs)
                RecordPublishedPath(*task, output.Path.c_str());
        }
        task->Error.clear();
        task->Outcome = ImportTaskOutcome::Completed;
    }

    void AssetImporterPanel::OnImportArtifactPublished(void* ctx, ZEngine::Importers::AssetFileType, const char* path)
    {
        auto* task = static_cast<ImportTask*>(ctx);
        if (task)
            RecordPublishedPath(*task, path);
    }

    void AssetImporterPanel::RecordPublishedPath(ImportTask& task, const char* path)
    {
        if (!path || path[0] == '\0')
            return;
        auto parsed = VFSPath::Parse(path);
        if (parsed.Failed())
            return;

        for (const ZEngine::Core::VFS::VFSPath& existing : task.PublishedPaths)
            if (existing == parsed.Value())
                return;

        task.PublishedPaths.push(parsed.Value());
    }

    void AssetImporterPanel::EndImportPublication(ImportTask& task)
    {
        auto* vfs = task.Configuration.VFS;
        if (!vfs || !task.Publication.IsValid())
            return;

        for (const ZEngine::Core::VFS::VFSPath& published : task.PublishedPaths)
            vfs->RecordImportPublicationArtifact(task.Publication, published);
        vfs->EndImportPublication(task.Publication);
        task.Publication = {};
    }

    void AssetImporterPanel::CompleteImportOnMainThread(void* ctx, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs)
    {
        auto* self = reinterpret_cast<AssetImporterPanel*>(ctx);

        if (self->m_project_view)
            self->m_project_view->RequestRefresh();

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

        // Write a .meta for every cooked material (#762) — mirrors the mesh handling
        // below. Without this, AssimpImporter/GltfImporter/FbxImporter's material
        // UUID stabilization (also #762) has no .meta to read on the next re-import,
        // and every re-cook mints a brand-new, disconnected material identity.
        {
            auto*   ctx_engine = ZEngine::Engine::GetContext();
            auto*   app        = self->m_layer ? reinterpret_cast<EditorPtr>(self->m_layer->CurrentApp) : nullptr;
            cstring ws         = (app && app->Configuration) ? app->Configuration->WorkingSpacePath.c_str() : "";
            if (ctx_engine && ctx_engine->VFS && ws[0] != '\0')
            {
                auto* vfs = reinterpret_cast<ZEngine::Core::VFS::IVFSContext*>(ctx_engine->VFS);
                for (unsigned i = 0; i < outputs.size(); ++i)
                {
                    if (outputs[i].Type != ZEngine::Importers::AssetFileType::MATERIAL)
                        continue;

                    cstring mat_path = outputs[i].Path.c_str();
                    auto    rel      = VFSPath::Parse(mat_path);
                    if (!rel.Succeeded())
                        continue;

                    auto material_uuid = ReadEmbeddedAssetUUID(*vfs, rel.Value());
                    if (material_uuid.Failed())
                    {
                        CompleteImportErrorOnMainThread(self, fmt::format("Failed to read material identity for '{}' (VFS error {})", mat_path, static_cast<uint32_t>(material_uuid.Error())));
                        return;
                    }

                    auto meta_result = ZEngine::Core::VFS::MetaFileIO::Read(*vfs, rel.Value());
                    if (meta_result.Failed() && meta_result.Error() != ZEngine::Core::VFS::VFSError::NotFound && meta_result.Error() != ZEngine::Core::VFS::VFSError::Corrupted)
                    {
                        CompleteImportErrorOnMainThread(self, fmt::format("Failed to read metadata for '{}' (VFS error {})", mat_path, static_cast<uint32_t>(meta_result.Error())));
                        return;
                    }
                    ZEngine::Core::VFS::MetaFileData meta = meta_result.Succeeded() ? meta_result.Value() : ZEngine::Core::VFS::MetaFileData{};
                    meta.AssetUUID                        = material_uuid.Value();
                    secure_strncpy(meta.SourcePath, sizeof(meta.SourcePath), self->m_path_buf, sizeof(meta.SourcePath) - 1);
                    secure_strncpy(meta.ArtifactPath, sizeof(meta.ArtifactPath), mat_path, sizeof(meta.ArtifactPath) - 1);
                    secure_strncpy(meta.ImporterName, sizeof(meta.ImporterName), "GltfImporter/AssimpImporter", sizeof(meta.ImporterName) - 1);
                    auto write = ZEngine::Core::VFS::MetaFileIO::Write(*vfs, rel.Value(), meta);
                    if (write.Failed())
                    {
                        CompleteImportErrorOnMainThread(self, fmt::format("Failed to persist metadata for '{}' (VFS error {})", mat_path, static_cast<uint32_t>(write.Error())));
                        return;
                    }
                }
            }
        }

        if (has_mesh && mesh_path)
        {
            // mesh_path is VFS-relative (e.g. "/Assets/Meshes/test_cube.zemesh") —
            // ReadAssetMeshFileHeader does raw filesystem I/O and needs a native path.
            char native_mesh_path[MAX_FILE_PATH_COUNT] = {};
            {
                auto*   header_app = self->m_layer ? reinterpret_cast<EditorPtr>(self->m_layer->CurrentApp) : nullptr;
                cstring header_ws  = (header_app && header_app->Configuration) ? header_app->Configuration->WorkingSpacePath.c_str() : "";
                auto    mesh_pr    = VFSPath::Parse(mesh_path);
                if (mesh_pr.Succeeded() && header_ws[0] != '\0')
                    mesh_pr.Value().ResolveNative(header_ws, native_mesh_path, sizeof(native_mesh_path));
            }

            // Read once, shared by the meta write below and the add-to-scene block.
            ZEngine::Importers::AssetCodec::AssetMeshFileHeader header{};
            bool                                                has_header = ZEngine::Importers::AssetCodec::ReadAssetMeshFileHeader(native_mesh_path, header);

            // Write meta file (source path for re-import)
            auto*                                               ctx_engine = ZEngine::Engine::GetContext();
            if (ctx_engine && ctx_engine->VFS)
            {
                auto* vfs = reinterpret_cast<ZEngine::Core::VFS::IVFSContext*>(ctx_engine->VFS);

                // mesh_path (AssetImporterOutput::Path) is already a VFS-relative path
                // (e.g. "Assets/Meshes/test_cube.zemesh") — config.OutputAssetsPath has
                // no native workspace prefix to strip (see Editor.cpp's expand_nested:
                // "Result is a workspace-relative sub-path ... no leading slash"). The
                // previous strncmp-against-WorkingSpacePath check could never match,
                // silently skipping this whole block on every import (#762).
                auto  rel = VFSPath::Parse(mesh_path);
                if (rel.Succeeded())
                {
                    auto meta_result = ZEngine::Core::VFS::MetaFileIO::Read(*vfs, rel.Value());
                    if (meta_result.Failed() && meta_result.Error() != ZEngine::Core::VFS::VFSError::NotFound && meta_result.Error() != ZEngine::Core::VFS::VFSError::Corrupted)
                    {
                        CompleteImportErrorOnMainThread(self, fmt::format("Failed to read metadata for '{}' (VFS error {})", mesh_path, static_cast<uint32_t>(meta_result.Error())));
                        return;
                    }
                    ZEngine::Core::VFS::MetaFileData meta = meta_result.Succeeded() ? meta_result.Value() : ZEngine::Core::VFS::MetaFileData{};
                    // Sync to the file's own embedded UUID — otherwise a nil
                    // AssetUUID gets locked in forever (#755).
                    if (has_header)
                        meta.AssetUUID = header.Id;
                    secure_strncpy(meta.SourcePath, sizeof(meta.SourcePath), self->m_path_buf, sizeof(meta.SourcePath) - 1);
                    secure_strncpy(meta.ArtifactPath, sizeof(meta.ArtifactPath), mesh_path, sizeof(meta.ArtifactPath) - 1);
                    secure_strncpy(meta.ImporterName, sizeof(meta.ImporterName), "GltfImporter/AssimpImporter", sizeof(meta.ImporterName) - 1);
                    auto write = ZEngine::Core::VFS::MetaFileIO::Write(*vfs, rel.Value(), meta);
                    if (write.Failed())
                    {
                        CompleteImportErrorOnMainThread(self, fmt::format("Failed to persist metadata for '{}' (VFS error {})", mesh_path, static_cast<uint32_t>(write.Error())));
                        return;
                    }
                }
            }

            // Add mesh instance to scene if triggered by drag-drop
            if (self->m_add_to_scene && has_header)
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
                        if (pr.Succeeded())
                        {
                            auto s = pr.Value().Stem();
                            snprintf(iname, sizeof(iname), "%.*s", (int) s.Length, s.Data);
                        }
                    }
                    self->CreateMeshActor(header.Id, scene->AddMeshInstance(header.Id, iname), iname);
                }
            }
            self->m_add_to_scene     = false;
            self->m_instance_name[0] = '\0';

            self->PushLog("Completed", kGreen[0], kGreen[1], kGreen[2]);
            char fn[256] = {};
            {
                auto pr = VFSPath::Parse(self->m_path_buf);
                if (pr.Succeeded())
                {
                    auto f = pr.Value().Filename();
                    snprintf(fn, sizeof(fn), "%.*s", (int) f.Length, f.Data);
                }
            }
            self->PushHistory(fn, true, "Completed");
        }
        else
        {
            self->m_add_to_scene     = false;
            self->m_instance_name[0] = '\0';
            self->PushLog("Import failed — no mesh output", kRed[0], kRed[1], kRed[2]);
            char fn[256] = {};
            {
                auto pr = VFSPath::Parse(self->m_path_buf);
                if (pr.Succeeded())
                {
                    auto f = pr.Value().Filename();
                    snprintf(fn, sizeof(fn), "%.*s", (int) f.Length, f.Data);
                }
            }
            self->PushHistory(fn, false, "No mesh output");
        }

        self->m_progress.value.store(1.f, std::memory_order_relaxed);
        self->m_state.value.store(ImporterState::Idle, std::memory_order_release);
    }

    void AssetImporterPanel::OnImportProgress(void* ctx, float pct)
    {
        auto* task = static_cast<ImportTask*>(ctx);
        auto* self = task ? task->Panel : nullptr;
        if (!self)
            return;

        pct           = std::isfinite(pct) ? std::clamp(pct, 0.f, 1.f) : 0.f;
        char msg[128] = {};
        snprintf(msg, sizeof(msg), "Processing... %.0f%%", pct * 100.f);
        self->PushLog(msg, kWhite[0], kWhite[1], kWhite[2]);
        self->m_progress.value.store(pct, std::memory_order_relaxed);
    }

    void AssetImporterPanel::OnImportError(void* ctx, std::string_view err)
    {
        auto* task = static_cast<ImportTask*>(ctx);
        if (!task)
            return;

        task->Outputs.clear();
        task->Error.assign(err.data(), err.size());
        task->Outcome = ImportTaskOutcome::Failed;
    }

    void AssetImporterPanel::CompleteImportErrorOnMainThread(void* ctx, std::string_view err)
    {
        auto* self               = reinterpret_cast<AssetImporterPanel*>(ctx);
        self->m_add_to_scene     = false;
        self->m_instance_name[0] = '\0';
        char msg[512]            = {};
        snprintf(msg, sizeof(msg), "Error: %.*s", (int) err.size(), err.data());
        self->PushLog(msg, kRed[0], kRed[1], kRed[2]);
        char fn[256] = {};
        {
            auto pr = VFSPath::Parse(self->m_path_buf);
            if (pr.Succeeded())
            {
                auto f = pr.Value().Filename();
                snprintf(fn, sizeof(fn), "%.*s", (int) f.Length, f.Data);
            }
        }
        self->PushHistory(fn, false, msg);
        self->m_state.value.store(ImporterState::Idle, std::memory_order_release);
    }

    void AssetImporterPanel::OnImportLog(void* ctx, std::string_view msg)
    {
        auto* task = static_cast<ImportTask*>(ctx);
        auto* self = task ? task->Panel : nullptr;
        if (!self)
            return;

        char buf[256] = {};
        snprintf(buf, sizeof(buf), "%.*s", (int) msg.size(), msg.data());
        {
            std::lock_guard<std::mutex> lock(self->m_log_mutex);
            secure_strcpy(self->m_status, sizeof(self->m_status), buf);
        }
        self->PushLog(buf, kWhite[0], kWhite[1], kWhite[2]);
    }

} // namespace Tetragrama::Panels
