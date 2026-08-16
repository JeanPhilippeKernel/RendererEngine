#include <Tetragrama/Components/AssetImporterUIComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/Helpers/UIDispatcher.h>
#include <ZEngine/Core/Coroutine.h>
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <imgui.h>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Helpers;
using namespace ZEngine::Importers;

namespace Tetragrama::Components
{
    void AssetImporterUIComponent::Initialize(Layers::ImguiLayer* parent, cstring name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);

        parent->LocalArena.CreateSubArena(ZMega(2), &LocalArena);

        m_gltf_importer   = ZPushStructCtor(parent->Arena, ZEngine::Importers::GltfImporter);
        m_assimp_importer = ZPushStructCtor(parent->Arena, ZEngine::Importers::AssimpImporter);
        m_gltf_importer->Initialize(&LocalArena);
        m_assimp_importer->Initialize(&LocalArena);
    }

    void AssetImporterUIComponent::Update(ZEngine::Core::TimeStep /*dt*/) {}

    // ─── Utilities ───────────────────────────────────────────────────────────

    void AssetImporterUIComponent::PushLog(cstring text, const float color[4])
    {
        std::lock_guard<std::mutex> lock(m_log_mutex);
        auto&                       e = m_log[m_log_head];
        secure_strncpy(e.Text, sizeof(e.Text), text, sizeof(e.Text) - 1);
        e.Color[0] = color[0];
        e.Color[1] = color[1];
        e.Color[2] = color[2];
        e.Color[3] = color[3];
        m_log_head = (m_log_head + 1) % kLogMax;
        if (m_log_count < kLogMax)
            ++m_log_count;
        m_scroll_log = true;
    }

    void AssetImporterUIComponent::PushHistory(cstring name, bool success, cstring msg)
    {
        if (m_history_count < kHistMax)
        {
            auto& e = m_history[m_history_count++];
            secure_strncpy(e.Name, sizeof(e.Name), name, sizeof(e.Name) - 1);
            secure_strncpy(e.Message, sizeof(e.Message), msg, sizeof(e.Message) - 1);
            e.Success = success;
        }
        else
        {
            for (int i = 0; i < kHistMax - 1; ++i)
                m_history[i] = m_history[i + 1];
            auto& e = m_history[kHistMax - 1];
            secure_strncpy(e.Name, sizeof(e.Name), name, sizeof(e.Name) - 1);
            secure_strncpy(e.Message, sizeof(e.Message), msg, sizeof(e.Message) - 1);
            e.Success = success;
        }
    }

    void AssetImporterUIComponent::TriggerScan()
    {
        auto* vfs = static_cast<ZEngine::Core::VFS::IVFSContext*>(ZEngine::Engine::GetContext()->VFS);
        if (vfs && ParentLayer)
            ParentLayer->Scanner.Scan(vfs, ZEngine::Core::VFS::VFSPath::Root(), &ParentLayer->Cache);
    }

    void AssetImporterUIComponent::BrowseFile()
    {
        Helpers::UIDispatcher::RunAsync([this]() -> std::future<void> {
            if (!ParentLayer || !ParentLayer->CurrentApp)
                co_return;
            auto                          window  = ParentLayer->CurrentApp->CurrentWindow;
            std::vector<std::string_view> filters = {".glb", ".gltf", ".fbx", ".obj"};
            std::string                   picked  = co_await window->OpenFileDialogAsync(filters);
            if (!picked.empty())
            {
                secure_strncpy(m_path_buf, sizeof(m_path_buf), picked.c_str(), picked.size());
                m_state.value.store(ImporterState::Options);
            }
        });
    }

    // ─── Import ──────────────────────────────────────────────────────────────

    void AssetImporterUIComponent::StartImport()
    {
        if (!m_gltf_importer || !m_assimp_importer)
            return;

        auto* app = reinterpret_cast<Tetragrama::EditorPtr>(ParentLayer->CurrentApp);
        if (!app || !app->Configuration)
            return;

        static constexpr float kRed[]     = {1.0f, 0.3f, 0.3f, 1.0f};
        static constexpr float kWhite[]   = {0.8f, 0.8f, 0.8f, 1.0f};

        // Build ImportConfiguration from project settings
        const auto&            cfg        = *app->Configuration;
        auto                   asset_name = fs::path(m_path_buf).filename().replace_extension().string();
        auto                   asset_file = asset_name + ".zemesh";
        auto                   parent_dir = fs::path(m_path_buf).parent_path().string();

        // Use the arena-local config (arena will be cleared after import)
        LocalArena.Clear();
        auto* config = ZPushStruct(&LocalArena, AssetCodec::ImportConfiguration);
        config->OutputWorkingSpacePath.init(&LocalArena, cfg.WorkingSpacePath.c_str());
        config->OutputTextureFilesPath.init(&LocalArena, cfg.TexturePath.c_str());
        config->OutputAssetsPath.init(&LocalArena, cfg.SceneDataPath.c_str());
        config->AssetName.init(&LocalArena, asset_name.c_str());
        config->OutputAssetFile.init(&LocalArena, asset_file.c_str());
        config->InputBaseAssetFilePath.init(&LocalArena, parent_dir.c_str());

        char msg[512];
        snprintf(msg, sizeof(msg), "Importing %s", fs::path(m_path_buf).filename().string().c_str());
        PushLog(msg, kWhite);

        m_state.value.store(ImporterState::Importing);
        m_progress.value.store(0.0f);

        // Route by extension to the appropriate importer
        auto ext = fs::path(m_path_buf).extension().string();
        // Normalize: remove leading dot, lowercase
        if (!ext.empty() && ext[0] == '.')
            ext = ext.substr(1);
        for (auto& c : ext)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

        std::string src_path = m_path_buf;
        auto        cfg_copy = *config;

        if (ext == "glb" || ext == "gltf")
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit([this, src = src_path, cfg_copy, arena = &LocalArena, app]() mutable { m_gltf_importer->ImportFile(src.c_str(), cfg_copy, arena, this, OnImportFileComplete, OnImportProgress, OnImportError, OnImportLog); });
        }
        else
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit([this, src = src_path, cfg_copy, arena = &LocalArena, app]() mutable { m_assimp_importer->ImportFile(src.c_str(), cfg_copy, arena, this, OnImportFileComplete, OnImportProgress, OnImportError, OnImportLog); });
        }
    }

    // ─── Callbacks (background thread) ───────────────────────────────────────

    void AssetImporterUIComponent::OnImportFileComplete(void* ctx, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs)
    {
        auto*                  self      = static_cast<AssetImporterUIComponent*>(ctx);

        static constexpr float kGreen[]  = {0.3f, 1.0f, 0.4f, 1.0f};
        static constexpr float kRed[]    = {1.0f, 0.3f, 0.3f, 1.0f};

        bool                   has_mesh  = false;
        cstring                mesh_path = nullptr;
        for (unsigned i = 0; i < outputs.size(); ++i)
        {
            if (outputs[i].Type == ZEngine::Importers::AssetFileType::MESH)
            {
                has_mesh  = true;
                mesh_path = outputs[i].Path.c_str();
            }
        }

        if (has_mesh)
        {
            // Write meta file with SourcePath so reimport is possible later
            auto* ctx_engine = ZEngine::Engine::GetContext();
            if (ctx_engine && ctx_engine->VFS && mesh_path)
            {
                auto*   vfs    = static_cast<ZEngine::Core::VFS::IVFSContext*>(ctx_engine->VFS);
                auto*   app    = reinterpret_cast<Tetragrama::EditorPtr>(self->ParentLayer->CurrentApp);
                cstring ws     = app ? app->WorkingSpacePath : "";
                size_t  ws_len = secure_strlen(ws);

                // Build VFSPath for the .zemesh artifact
                if (ws_len > 0 && strncmp(mesh_path, ws, ws_len) == 0)
                {
                    auto rel_result = VFSPath::Parse(mesh_path + ws_len);
                    if (rel_result.Succeeded())
                    {
                        // Read existing meta or create a new one
                        auto                             meta_result = ZEngine::Core::VFS::MetaFileIO::Read(*vfs, rel_result.Value());
                        ZEngine::Core::VFS::MetaFileData meta        = meta_result.Succeeded() ? meta_result.Value() : ZEngine::Core::VFS::MetaFileData{};

                        // Store source path, artifact path, importer
                        ZEngine::Helpers::secure_strncpy(meta.SourcePath, sizeof(meta.SourcePath), self->m_path_buf, sizeof(meta.SourcePath) - 1);
                        ZEngine::Helpers::secure_strncpy(meta.ArtifactPath, sizeof(meta.ArtifactPath), mesh_path, sizeof(meta.ArtifactPath) - 1);
                        ZEngine::Helpers::secure_strncpy(meta.ImporterName, sizeof(meta.ImporterName), "GltfImporter/AssimpImporter", sizeof(meta.ImporterName) - 1);

                        ZEngine::Core::VFS::MetaFileIO::Write(*vfs, rel_result.Value(), meta);
                    }
                }
            }

            self->PushLog("Completed", kGreen);
            cstring filename = fs::path(self->m_path_buf).filename().string().c_str();
            self->PushHistory(filename, true, "Completed");
        }
        else
        {
            self->PushLog("Import failed — no mesh output", kRed);
            cstring filename = fs::path(self->m_path_buf).filename().string().c_str();
            self->PushHistory(filename, false, "No mesh output");
        }

        self->m_progress.value.store(1.0f);
        self->m_state.value.store(ImporterState::Idle);
        self->m_pending_scan.value.store(true);
    }

    void AssetImporterUIComponent::OnImportProgress(void* ctx, float pct)
    {
        auto*                  self     = static_cast<AssetImporterUIComponent*>(ctx);
        static constexpr float kWhite[] = {0.8f, 0.8f, 0.8f, 1.0f};
        char                   msg[128];
        snprintf(msg, sizeof(msg), "Processing… %.0f%%", pct * 100.0f);
        self->PushLog(msg, kWhite);
        self->m_progress.value.store(pct);
    }

    void AssetImporterUIComponent::OnImportError(void* ctx, std::string_view err)
    {
        auto*                  self   = static_cast<AssetImporterUIComponent*>(ctx);
        static constexpr float kRed[] = {1.0f, 0.3f, 0.3f, 1.0f};
        char                   msg[512];
        snprintf(msg, sizeof(msg), "Error: %.*s", static_cast<int>(err.size()), err.data());
        self->PushLog(msg, kRed);
        cstring filename = fs::path(self->m_path_buf).filename().string().c_str();
        self->PushHistory(filename, false, msg);
        self->m_state.value.store(ImporterState::Idle);
        self->m_pending_scan.value.store(true);
    }

    void AssetImporterUIComponent::OnImportLog(void* ctx, std::string_view msg)
    {
        auto*                  self     = static_cast<AssetImporterUIComponent*>(ctx);
        static constexpr float kWhite[] = {0.8f, 0.8f, 0.8f, 1.0f};
        char                   buf[256];
        snprintf(buf, sizeof(buf), "%.*s", static_cast<int>(msg.size()), msg.data());
        self->PushLog(buf, kWhite);
    }

    // ─── Sub-renders ─────────────────────────────────────────────────────────

    void AssetImporterUIComponent::RenderIdle()
    {
        if (ImGui::Button("+ Import File", ImVec2(-1, 0)))
            BrowseFile();

        ImGui::TextDisabled("or drag a 3D file here");

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_FILE_DRAG_OP"))
            {
                char buf[1024] = {};
                ZEngine::Helpers::secure_memcpy(buf, sizeof(buf), payload->Data, payload->DataSize);
                auto ext = fs::path(buf).extension().string();
                if (ext == ".glb" || ext == ".gltf" || ext == ".fbx" || ext == ".obj")
                {
                    secure_strncpy(m_path_buf, sizeof(m_path_buf), buf, sizeof(m_path_buf) - 1);
                    m_state.value.store(ImporterState::Options);
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (m_history_count > 0)
        {
            ImGui::Separator();
            ImGui::TextDisabled("Recent Imports");
            for (int i = m_history_count - 1; i >= 0; --i)
            {
                const auto& h = m_history[i];
                if (h.Success)
                    ImGui::TextColored({0.3f, 1.0f, 0.4f, 1.0f}, "[OK]  %s", h.Name);
                else
                {
                    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "[ERR] %s", h.Name);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", h.Message);
                }
            }
        }
    }

    void AssetImporterUIComponent::RenderOptions()
    {
        cstring filename = fs::path(m_path_buf).filename().string().c_str();
        ImGui::TextUnformatted(filename);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
        if (ImGui::SmallButton("X"))
        {
            m_path_buf[0] = '\0';
            m_state.value.store(ImporterState::Idle);
            return;
        }

        ImGui::Separator();

        static constexpr cstring kAxes[] = {"Y-Up", "Z-Up"};

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("Scale", &m_scale, 0.01f, 0.001f, 100.0f, "%.3f");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::SetNextItemWidth(80.0f);
            ImGui::Combo("Axis", &m_axis_index, kAxes, 2);
        }

        if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Generate Normals", &m_gen_normals);
            ImGui::Checkbox("Merge Identical Vertices", &m_merge_vertices);
        }

        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Import Materials", &m_import_materials);
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Checkbox("Import Textures", &m_import_textures);
        }

        ImGui::Separator();

        if (ImGui::Button("Import All", ImVec2(-108.0f, 0)))
            StartImport();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(-1, 0)))
        {
            m_path_buf[0] = '\0';
            m_state.value.store(ImporterState::Idle);
        }

        // Show last log entry for immediate error feedback
        {
            std::lock_guard<std::mutex> lk(m_log_mutex);
            if (m_log_count > 0)
            {
                const auto& last = m_log[(m_log_head - 1 + kLogMax) % kLogMax];
                ImGui::Spacing();
                ImGui::TextColored({last.Color[0], last.Color[1], last.Color[2], last.Color[3]}, "%s", last.Text);
            }
        }
    }

    void AssetImporterUIComponent::RenderImporting()
    {
        cstring filename = fs::path(m_path_buf).filename().string().c_str();
        ImGui::Text("Importing  %s", filename);
        ImGui::ProgressBar(m_progress.value.load(), ImVec2(-1, 0));

        ImGui::Separator();

        ImGui::BeginChild("##imp_log", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(m_log_mutex);
            int                         count = m_log_count < kLogMax ? m_log_count : kLogMax;
            int                         start = (m_log_count >= kLogMax) ? m_log_head : 0;
            for (int i = 0; i < count; ++i)
            {
                const auto& e = m_log[(start + i) % kLogMax];
                ImGui::TextColored({e.Color[0], e.Color[1], e.Color[2], e.Color[3]}, "> %s", e.Text);
            }
            if (m_scroll_log)
            {
                ImGui::SetScrollHereY(1.0f);
                m_scroll_log = false;
            }
        }
        ImGui::EndChild();
    }

    // ─── Main render ─────────────────────────────────────────────────────────

    void AssetImporterUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const, ZEngine::Hardwares::CommandBuffer* const)
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto* app = reinterpret_cast<Tetragrama::EditorPtr>(ParentLayer->CurrentApp);
        if (!app || !app->Configuration->ShowImporter)
            return;

        if (app->Configuration->FocusImporter)
        {
            ImGui::SetNextWindowFocus();
            app->Configuration->FocusImporter = false;
        }

        if (!ImGui::Begin(Name, &app->Configuration->ShowImporter, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        if (m_pending_scan.value.exchange(false))
            TriggerScan();

        switch (m_state.value.load())
        {
            case ImporterState::Idle:
                RenderIdle();
                break;
            case ImporterState::Options:
                RenderOptions();
                break;
            case ImporterState::Importing:
                RenderImporting();
                break;
        }

        ImGui::End();
    }
} // namespace Tetragrama::Components
