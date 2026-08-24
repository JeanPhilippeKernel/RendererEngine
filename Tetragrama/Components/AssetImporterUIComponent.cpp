#include <Tetragrama/Components/AssetImporterUIComponent.h>
#include <Tetragrama/Editor.h>
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
#include <imgui.h>
#include <cstdio>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Helpers;
using namespace ZEngine::Importers;

namespace Tetragrama::Components
{
    void AssetImporterUIComponent::Initialize(Layers::ImguiLayer* parent, cstring name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);

        parent->LocalArena.CreateSubArena(ZMega(8), &LocalArena);
        parent->LocalArena.CreateSubArena(ZMega(4), &LocalStringArena);

        // Importer arenas carved from the engine's ImportPipeline budget so all
        // import memory — engine importers and editor importers — is budget-tracked.
        auto* import_arena = &ZEngine::Engine::GetContext()->ImportPipelineArena;
        import_arena->CreateSubArena(ZMega(64), &GltfImporterArena);
        import_arena->CreateSubArena(ZMega(350), &AssimpImporterArena);

        m_gltf_importer   = ZPushStructCtor(import_arena, ZEngine::Importers::GltfImporter);
        m_fbx_importer    = ZPushStructCtor(import_arena, ZEngine::Importers::FbxImporter);
        m_assimp_importer = ZPushStructCtor(import_arena, ZEngine::Importers::AssimpImporter);
        m_gltf_importer->Initialize(&GltfImporterArena);
        m_fbx_importer->Initialize(import_arena);
        m_assimp_importer->Initialize(&AssimpImporterArena);

        m_path_buf.init(&LocalStringArena, 1024);
    }

    void AssetImporterUIComponent::Update(ZEngine::Core::TimeStep /*dt*/) {}

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
        // Consume any pending Actor creation from the importer background thread.
        // This runs on the main thread, so ECS operations are safe here.
        if (m_pending_actor.valid)
        {
            auto* app   = ParentLayer ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;
            auto* scene = app ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;
            auto* ctx   = ZEngine::Engine::GetContext();
            if (scene && ctx && ctx->ActorManager)
            {
                ZEngine::ECS::ActorHandle handle = ctx->ActorManager->Create();
                ZEngine::ECS::Actor*      actor  = ctx->ActorManager->Access(handle);
                if (actor)
                {
                    using namespace ZEngine::ECS::Components;
                    NameComponent nc = {};
                    ZEngine::Helpers::secure_strncpy(nc.Value, sizeof(nc.Value), m_pending_actor.name, ZEngine::Helpers::secure_strlen(m_pending_actor.name));
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

        auto* vfs = reinterpret_cast<ZEngine::Core::VFS::IVFSContext*>(ZEngine::Engine::GetContext()->VFS);
        if (vfs && ParentLayer)
            ParentLayer->Scanner.Scan(vfs, ZEngine::Core::VFS::VFSPath::Root(), &ParentLayer->Cache);
    }

    std::future<void> AssetImporterUIComponent::BrowseFileAsync()
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            co_return;
        auto                          window  = ParentLayer->CurrentApp->CurrentWindow;
        std::vector<std::string_view> filters = {".glb", ".gltf", ".fbx", ".obj"};
        std::string                   picked  = co_await window->OpenFileDialogAsync(filters);
        if (!picked.empty())
        {
            m_path_buf.clear();
            m_path_buf.append(picked.c_str());
            if (m_same_settings)
                StartImport();
            else
                m_state.value.store(ImporterState::Options);
        }
    }

    void AssetImporterUIComponent::BrowseFile()
    {
        ZEngine::Core::MainThreadScheduler::Post(this, [](void* context) { reinterpret_cast<AssetImporterUIComponent*>(context)->BrowseFileAsync(); });
    }

    void AssetImporterUIComponent::StartImport()
    {
        if (!m_gltf_importer || !m_fbx_importer || !m_assimp_importer)
            return;

        auto* app = reinterpret_cast<Tetragrama::EditorPtr>(ParentLayer->CurrentApp);
        if (!app || !app->Configuration)
            return;

        static constexpr float kWhite[]   = {0.8f, 0.8f, 0.8f, 1.0f};

        // Build ImportConfiguration from project settings
        const auto&            cfg        = *app->Configuration;
        auto                   vfs_result = VFSPath::Parse(m_path_buf.c_str());
        if (vfs_result.Failed())
        {
            return;
        }
        auto& vfs_value  = vfs_result.Value();
        auto  asset_name = vfs_value.Stem();
        auto  parent_dir = vfs_value.Parent();

        char  asset_file_buf[256];
        snprintf(asset_file_buf, sizeof(asset_file_buf), "%.*s.zemesh", (int) asset_name.Length, asset_name.Data);

        // Use the arena-local config (arena will be cleared after import)
        LocalArena.Clear();
        auto* config = ZPushStruct(&LocalArena, AssetCodec::ImportConfiguration);
        config->OutputWorkingSpacePath.init(&LocalArena, cfg.WorkingSpacePath.c_str());
        config->OutputTextureFilesPath.init(&LocalArena, cfg.TexturePath.c_str());
        config->OutputAssetsPath.init(&LocalArena, cfg.MeshPath.c_str());
        config->OutputMaterialPath.init(&LocalArena, cfg.MaterialPath.c_str());
        if (!m_use_source_name && m_instance_name[0] != '\0')
            config->AssetName.init(&LocalArena, m_instance_name);
        else
            config->AssetName.init(&LocalArena, asset_name.Data);
        config->OutputAssetFile.init(&LocalArena, asset_file_buf);
        config->InputBaseAssetFilePath.init(&LocalArena, parent_dir.CStr());
        config->VFS                     = reinterpret_cast<ZEngine::Core::VFS::IVFSContext*>(ZEngine::Engine::GetContext()->VFS);
        config->Options.UniformScale    = m_scale;
        config->Options.AxisUpIsZ       = m_axis_index == 1;
        config->Options.NormalsMode     = static_cast<uint8_t>(m_normals_mode);
        config->Options.MergeVertices   = m_merge_vertices;
        config->Options.ImportMaterials = m_import_materials;
        config->Options.ImportTextures  = m_import_textures;
        config->Options.FlipUVs         = m_flip_uvs;

        char msg[512];
        snprintf(msg, sizeof(msg), "Importing %s", vfs_value.Filename().Data);
        PushLog(msg, kWhite);

        m_state.value.store(ImporterState::Importing);
        m_progress.value.store(0.0f);

        // Route by extension to the appropriate importer
        auto ext      = vfs_value.Extension();
        auto cfg_copy = *config;

        if (secure_strcmp(ext.Data, ".glb") == 0 || secure_strcmp(ext.Data, ".gltf") == 0)
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit([this, src = m_path_buf, cfg_copy, arena = &LocalArena, app]() mutable { m_gltf_importer->ImportFile(src.c_str(), cfg_copy, arena, this, OnImportFileComplete, OnImportProgress, OnImportError, OnImportLog); });
        }
        else if (secure_strcmp(ext.Data, ".fbx") == 0)
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit([this, src = m_path_buf, cfg_copy, arena = &LocalArena, app]() mutable { m_fbx_importer->ImportFile(src.c_str(), cfg_copy, arena, this, OnImportFileComplete, OnImportProgress, OnImportError, OnImportLog); });
        }
        else
        {
            ZEngine::Helpers::ThreadPoolHelper::Submit([this, src = m_path_buf, cfg_copy, arena = &LocalArena, app]() mutable { m_assimp_importer->ImportFile(src.c_str(), cfg_copy, arena, this, OnImportFileComplete, OnImportProgress, OnImportError, OnImportLog); });
        }
    }

    void AssetImporterUIComponent::OnImportFileComplete(void* ctx, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs)
    {
        auto*                  self      = reinterpret_cast<AssetImporterUIComponent*>(ctx);

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
                auto*   vfs    = reinterpret_cast<ZEngine::Core::VFS::IVFSContext*>(ctx_engine->VFS);
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
                        ZEngine::Helpers::secure_strncpy(meta.SourcePath, sizeof(meta.SourcePath), self->m_path_buf.c_str(), sizeof(meta.SourcePath) - 1);
                        ZEngine::Helpers::secure_strncpy(meta.ArtifactPath, sizeof(meta.ArtifactPath), mesh_path, sizeof(meta.ArtifactPath) - 1);
                        ZEngine::Helpers::secure_strncpy(meta.ImporterName, sizeof(meta.ImporterName), "GltfImporter/AssimpImporter", sizeof(meta.ImporterName) - 1);

                        ZEngine::Core::VFS::MetaFileIO::Write(*vfs, rel_result.Value(), meta);
                    }
                }
            }

            // If triggered by viewport drag-drop, add the mesh instance to the scene
            if (self->m_add_to_scene && mesh_path)
            {
                ZEngine::Importers::AssetCodec::AssetMeshFileHeader header{};
                if (ZEngine::Importers::AssetCodec::ReadAssetMeshFileHeader(mesh_path, header))
                {
                    auto* app   = reinterpret_cast<Tetragrama::EditorPtr>(self->ParentLayer->CurrentApp);
                    auto* scene = app ? reinterpret_cast<Tetragrama::EditorScenePtr>(app->CurrentScene) : nullptr;
                    if (scene)
                    {
                        char iname_buf[256] = {};
                        if (self->m_instance_name[0])
                            secure_strncpy(iname_buf, sizeof(iname_buf), self->m_instance_name, sizeof(iname_buf) - 1);
                        else
                        {
                            auto pr = VFSPath::Parse(self->m_path_buf.c_str());
                            if (pr.Succeeded())
                            {
                                auto stem = pr.Value().Stem();
                                snprintf(iname_buf, sizeof(iname_buf), "%.*s", (int) stem.Length, stem.Data);
                            }
                        }
                        cstring  iname                  = iname_buf;
                        // AddMeshInstance is seqlock-protected — safe from this background thread.
                        // Actor creation (ECS, not thread-safe) is deferred to TriggerScan on the main thread.
                        uint32_t render_id              = scene->AddMeshInstance(header.Id, iname);
                        self->m_pending_actor.uuid      = header.Id;
                        self->m_pending_actor.render_id = render_id;
                        self->m_pending_actor.valid     = true;
                        ZEngine::Helpers::secure_strncpy(self->m_pending_actor.name, sizeof(self->m_pending_actor.name), iname, ZEngine::Helpers::secure_strlen(iname));
                    }
                }
                self->m_add_to_scene     = false;
                self->m_instance_name[0] = '\0';
            }

            self->PushLog("Completed", kGreen);
            char fn_buf[256] = {};
            {
                auto pr = VFSPath::Parse(self->m_path_buf.c_str());
                if (pr.Succeeded())
                {
                    auto fn = pr.Value().Filename();
                    snprintf(fn_buf, sizeof(fn_buf), "%.*s", (int) fn.Length, fn.Data);
                }
            }
            self->PushHistory(fn_buf, true, "Completed");
        }
        else
        {
            self->m_add_to_scene     = false;
            self->m_instance_name[0] = '\0';
            self->PushLog("Import failed — no mesh output", kRed);
            char fn_buf2[256] = {};
            {
                auto pr = VFSPath::Parse(self->m_path_buf.c_str());
                if (pr.Succeeded())
                {
                    auto fn = pr.Value().Filename();
                    snprintf(fn_buf2, sizeof(fn_buf2), "%.*s", (int) fn.Length, fn.Data);
                }
            }
            self->PushHistory(fn_buf2, false, "No mesh output");
        }

        self->m_progress.value.store(1.0f);
        self->m_state.value.store(ImporterState::Idle);
        ZEngine::Core::MainThreadScheduler::Post(self, [](void* ctx) { reinterpret_cast<AssetImporterUIComponent*>(ctx)->TriggerScan(); });
    }

    void AssetImporterUIComponent::OnImportProgress(void* ctx, float pct)
    {
        auto*                  self     = reinterpret_cast<AssetImporterUIComponent*>(ctx);
        static constexpr float kWhite[] = {0.8f, 0.8f, 0.8f, 1.0f};
        char                   msg[128];
        snprintf(msg, sizeof(msg), "Processing… %.0f%%", pct * 100.0f);
        self->PushLog(msg, kWhite);
        self->m_progress.value.store(pct);
    }

    void AssetImporterUIComponent::OnImportError(void* ctx, std::string_view err)
    {
        auto*                  self   = reinterpret_cast<AssetImporterUIComponent*>(ctx);
        static constexpr float kRed[] = {1.0f, 0.3f, 0.3f, 1.0f};
        char                   msg[512];
        snprintf(msg, sizeof(msg), "Error: %.*s", static_cast<int>(err.size()), err.data());
        self->PushLog(msg, kRed);
        char fn_buf[256] = {};
        {
            auto pr = VFSPath::Parse(self->m_path_buf.c_str());
            if (pr.Succeeded())
            {
                auto fn = pr.Value().Filename();
                snprintf(fn_buf, sizeof(fn_buf), "%.*s", (int) fn.Length, fn.Data);
            }
        }
        self->PushHistory(fn_buf, false, msg);
        self->m_state.value.store(ImporterState::Idle);
        ZEngine::Core::MainThreadScheduler::Post(self, [](void* ctx) { reinterpret_cast<AssetImporterUIComponent*>(ctx)->TriggerScan(); });
    }

    void AssetImporterUIComponent::OnImportLog(void* ctx, std::string_view msg)
    {
        auto*                  self     = reinterpret_cast<AssetImporterUIComponent*>(ctx);
        static constexpr float kWhite[] = {0.8f, 0.8f, 0.8f, 1.0f};
        char                   buf[256];
        snprintf(buf, sizeof(buf), "%.*s", static_cast<int>(msg.size()), msg.data());
        self->PushLog(buf, kWhite);
    }

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
                auto pr = VFSPath::Parse(buf);
                if (pr.Succeeded())
                {
                    auto ext = pr.Value().Extension();
                    if (ext.Equals(".glb") || ext.Equals(".gltf") || ext.Equals(".fbx") || ext.Equals(".obj"))
                    {
                        m_path_buf.clear();
                        m_path_buf.append(buf);
                        if (m_same_settings)
                            StartImport();
                        else
                            m_state.value.store(ImporterState::Options);
                    }
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
        char fn_buf[256] = {};
        {
            auto pr = VFSPath::Parse(m_path_buf.c_str());
            if (pr.Succeeded())
            {
                auto fn = pr.Value().Filename();
                snprintf(fn_buf, sizeof(fn_buf), "%.*s", (int) fn.Length, fn.Data);
            }
        }
        ImGui::TextUnformatted(fn_buf);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
        if (ImGui::SmallButton("X"))
        {
            m_path_buf.clear();
            m_state.value.store(ImporterState::Idle);
            return;
        }

        ImGui::Separator();

        if (ImGui::BeginTabBar("##import_tabs"))
        {
            // General tab
            if (ImGui::BeginTabItem("General"))
            {
                ImGui::Checkbox("Use Source Name", &m_use_source_name);
                if (!m_use_source_name)
                {
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Asset Name", m_instance_name, sizeof(m_instance_name));
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("Asset Name", fn_buf, sizeof(fn_buf));
                    ImGui::EndDisabled();
                }
                ImGui::Spacing();
                ImGui::SetNextItemWidth(100.f);
                ImGui::DragFloat("Uniform Scale", &m_scale, 0.01f, 0.001f, 100.f, "%.3f");
                ImGui::SetNextItemWidth(100.f);
                static constexpr cstring kAxes[] = {"Y-Up", "Z-Up"};
                ImGui::Combo("Axis Up", &m_axis_index, kAxes, 2);
                ImGui::EndTabItem();
            }

            // Mesh tab
            if (ImGui::BeginTabItem("Mesh"))
            {
                static constexpr cstring kNormals[] = {"Off", "Flat", "Smooth"};
                ImGui::SetNextItemWidth(120.f);
                ImGui::Combo("Normals", &m_normals_mode, kNormals, 3);
                ImGui::Checkbox("Merge Identical Vertices", &m_merge_vertices);
                ImGui::Checkbox("Flip UVs", &m_flip_uvs);
                ImGui::BeginDisabled(true);
                static bool s_keep_sections = false;
                ImGui::Checkbox("Keep Sections Separate", &s_keep_sections);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Not yet supported");
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }

            // Material tab
            if (ImGui::BeginTabItem("Material"))
            {
                ImGui::Checkbox("Import Materials", &m_import_materials);
                ImGui::Checkbox("Import Textures", &m_import_textures);
                ImGui::EndTabItem();
            }

            // Animation tab — placeholder, all disabled
            if (ImGui::BeginTabItem("Animation"))
            {
                ImGui::BeginDisabled(true);
                static bool s_import_anims = true, s_only_anims = false, s_bone_tracks = true;
                ImGui::Checkbox("Import Animations", &s_import_anims);
                ImGui::Checkbox("Import Only Animations", &s_only_anims);
                ImGui::Checkbox("Import Bone Tracks", &s_bone_tracks);
                ImGui::EndDisabled();
                ImGui::Spacing();
                ImGui::TextDisabled("Animation import requires skeletal mesh support.");
                ImGui::EndTabItem();
            }

            // LOD tab — placeholder, all disabled
            if (ImGui::BeginTabItem("LOD"))
            {
                ImGui::BeginDisabled(true);
                static bool s_import_lods = false;
                static int  s_max_lods    = 4;
                ImGui::Checkbox("Import LODs", &s_import_lods);
                ImGui::SetNextItemWidth(120.f);
                ImGui::SliderInt("Max LOD Count", &s_max_lods, 1, 8);
                ImGui::EndDisabled();
                ImGui::Spacing();
                ImGui::TextDisabled("LOD support requires virtual geometry streaming.");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::Checkbox("Use same settings for subsequent files", &m_same_settings);
        ImGui::Spacing();

        if (ImGui::Button("Import All", ImVec2(-168.f, 0)))
            StartImport();
        ImGui::SameLine();
        ImGui::BeginDisabled(true);
        ImGui::Button("Preview...", ImVec2(-88.f, 0));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Not yet supported");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(-1, 0)))
        {
            m_path_buf.clear();
            m_state.value.store(ImporterState::Idle);
        }
    }

    void AssetImporterUIComponent::RenderImporting()
    {
        char fn_buf[256] = {};
        {
            auto pr = VFSPath::Parse(m_path_buf.c_str());
            if (pr.Succeeded())
            {
                auto fn = pr.Value().Filename();
                snprintf(fn_buf, sizeof(fn_buf), "%.*s", (int) fn.Length, fn.Data);
            }
        }
        ImGui::Text("Importing  %s", fn_buf);
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

        // Consume viewport drag-drop: auto-import and add mesh to scene when done
        if (app->Configuration->PendingImportPath[0] != '\0' && m_state.value.load() == ImporterState::Idle)
        {
            m_path_buf.clear();
            m_path_buf.append(app->Configuration->PendingImportPath);
            secure_strncpy(m_instance_name, sizeof(m_instance_name), app->Configuration->PendingImportName, sizeof(m_instance_name) - 1);
            app->Configuration->PendingImportPath[0] = '\0';
            app->Configuration->PendingImportName[0] = '\0';
            m_add_to_scene                           = true;
            StartImport();
        }

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
