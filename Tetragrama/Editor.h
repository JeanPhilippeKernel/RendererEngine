#pragma once
#include <Tetragrama/EditorMeshDrop.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/EditorSceneCandidate.h>
#include <Tetragrama/EditorSceneChange.h>
#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoViewport.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/UI/ZUIKey.h>
#include <cstddef>

namespace Tetragrama::Serializers
{
    struct EditorSceneSerializer;
} // namespace Tetragrama::Serializers

namespace Tetragrama::Panels
{
    struct AssetImporterPanel;
}

namespace Tetragrama
{

    struct EditorConfiguration
    {
        ZEngine::Core::Containers::String     WorkingSpacePath         = {};
        ZEngine::Core::Containers::String     ScenePath                = {};
        // Asset import directories (all under Assets/)
        ZEngine::Core::Containers::String     TexturePath              = {};
        ZEngine::Core::Containers::String     SoundPath                = {};
        ZEngine::Core::Containers::String     MeshPath                 = {};
        ZEngine::Core::Containers::String     MaterialPath             = {};
        ZEngine::Core::Containers::String     SpritePath               = {};
        ZEngine::Core::Containers::String     EnvironmentMapImportPath = {};
        // New scenes begin with this template, then own and serialize their copy.
        ZEngine::Rendering::Scenes::SkyConfig DefaultSky               = {};
        ZEngine::Core::Containers::String     ProjectName              = {};
        ZEngine::Core::Containers::String     ActiveSceneName          = {};
        bool                                  DarkTheme                = true;
        int                                   GizmoOperation           = -1;
        bool                                  ShowContentBrowser       = true;
        bool                                  FocusContentBrowser      = false;
        bool                                  ShowConsole              = false;
        bool                                  FocusConsole             = false;
        bool                                  ShowImporter             = false;
        bool                                  FocusImporter            = false;
        char                                  PendingImportPath[1024]  = {};
        char                                  PendingImportName[256]   = {};
        uint64_t                              PendingImportEpoch       = 0;

        void                                  ReadConfig(ZEngine::Core::Memory::ArenaAllocator* arena, const char* file);
    };
    ZDEFINE_PTR(EditorConfiguration);

    struct Editor : public ZEngine::Applications::GameApplication
    {

        friend struct EditorPickTestAccess;

        EditorConfigurationPtr Configuration = nullptr;

        Editor();
        ~Editor() override;

        Layers::ZUILayer*                     ZUIUILayer                        = nullptr;
        Panels::AssetImporterPanel*           AssetImporter                     = nullptr;

        /// @brief Editor-only navigation aid; it never changes game-camera behavior.
        bool                                  ConstrainCameraToAtmosphereGround = false;
        // Explicit WP2 fixture. Production authored dragging stays gated by M0.
        bool                                  EnableGizmoDebug                  = false;
        float                                 GizmoLogicalViewportHeight        = 0.f;

        ZEngine::Core::VFS::VFSDiskBackend    WorkingSpaceBackend               = {};
        // All editor-owned persistent CPU state is bounded by the Editor budget.
        ZEngine::Core::Memory::ArenaAllocator EditorArena                       = {};
        EditorSession                         Session                           = {};
        EditorMeshDrop                        MeshDrop                          = {};
        Gizmo::Viewport                       GizmoViewport                     = {};

        /// @brief Drains synthetic/UI history flags without stealing widget undo/redo.
        /// Native shortcuts are queued at event time to preserve their order/count.
        /// Both routes are independent from the presentation mailbox.
        void                                  ProcessHistoryShortcuts(ZEngine::UI::ZUIContext* ctx);
        [[nodiscard]] bool                    ProcessGizmoShortcut(const ZEngine::UI::ZUIContext* ctx, ZEngine::UI::ZUIKey key);
        /// @brief Process queued edits and interaction termination independently from UI publishing.
        void                                  UpdateSessionInput(ZEngine::UI::ZUIContext* ctx, bool focused, ZEngine::EngineContext* engine = nullptr);
        void                                  UpdateGizmoInput(ZEngine::UI::ZUIContext* ctx, bool focused);
        void                                  RefreshGizmoHitFrame(const ZEngine::UI::ZUIContext* ctx);
        [[nodiscard]] SceneChangePrompt       SceneChangePromptState() const
        {
            return m_scene_change.Prompt();
        }
        [[nodiscard]] bool                                  ChooseSceneChange(SceneChangeChoice choice, uint64_t prompt_serial);
        void                                                BuildSceneChangePrompt(ZEngine::UI::ZUIContext* ctx);
        /// Synchronize modal capture even when an overlay cannot be rebuilt.
        void                                                SyncSceneChangeCapture(ZEngine::UI::ZUIContext* ctx);
        /// Copied settings views include accepted pending UI changes, never authored writes.
        [[nodiscard]] ZEngine::Rendering::Scenes::SkyConfig SceneSkyForUI() const;
        [[nodiscard]] ZEngine::ECS::SceneGridSettings       SceneGridForUI() const;
        [[nodiscard]] bool                                  RequestSceneSkySettings(const ZEngine::Rendering::Scenes::SkyConfig& value);
        [[nodiscard]] bool                                  RequestSceneGridSettings(const ZEngine::ECS::SceneGridSettings& value);
        /// Explicit scene-local selection intent; Settings builds never apply it directly.
        /// Read-only scenes remain selectable, but cannot begin an authored interaction.
        [[nodiscard]] bool                                  RequestSceneGridSelection();
        [[nodiscard]] bool                                  ProcessSceneSettings(ZEngine::EngineContext& engine, ZEngine::ECS::SceneDiagnostics* diagnostics = nullptr);
        void                                                DiscardSceneSettings();

        virtual void                                        OnInitializing() override;
        virtual void                                        OverrideWindowConfiguration() override;
        virtual void                                        OnInitialized() override;

        virtual void                                        OnUpdate(float dt) override;
        virtual void                                        OnEvent(ZEngine::Core::CoreEvent&) override;

        void                                                ProcessEvent(ZEngine::Core::CoreEvent&) override;

        virtual void                                        OnPreRender() override;
        virtual void                                        OnPostRender() override;
        virtual void                                        OnRenderUI() override;
        void                                                PrepareScene(ZEngine::Applications::RenderFrameState& state) override;

        virtual void                                        OnClosing() override;
        virtual void                                        OnClosed() override;
        void                                                OnWorldShutdown(ZEngine::EngineContext& engine) override;
        bool                                                OnCloseRequested() override;
        /// The authoring world never simulates. Play requires a separate, complete
        /// snapshot/restore lifecycle before this policy can be changed.
        [[nodiscard]] bool                                  ShouldSimulate() const final
        {
            return false;
        }
        [[nodiscard]] bool ProcessCloseRequest(ZEngine::EngineContext& engine);

        /// @brief Enqueue an atomic source save; unnamed scenes open the native Save As picker.
        void               SaveScene();

        /// @brief Opens the native save picker on the main thread, outside UI construction.
        void               SaveSceneAs();
        [[nodiscard]] bool RequestSceneSave(const ZEngine::Core::VFS::VFSPath& path, uint64_t expected_epoch);
        /// @brief Called only within the session mutation phase, after queued edits.
        [[nodiscard]] bool ProcessSceneSave(ZEngine::EngineContext& engine);
        /// @brief Owner-thread polling; picker results retain the original scene/project binding.
        void               PollSceneSaveDialog();
        /// @brief Queued whole-scene reset. Dirty scenes are preserved, never silently discarded.
        [[nodiscard]] bool RequestNewScene();
        [[nodiscard]] bool ProcessNewScene(ZEngine::EngineContext& engine);

        /// Native dialog invocation is scheduled outside UI construction; completion
        /// is polled on the owner thread and retains the requesting scene/project binding.
        void               OpenSceneDialog();
        void               PollSceneOpenDialog();
        /// Queue a project-native source path on the session owner thread; never
        /// replaces the world inline. Dispatch captures project/service identities;
        /// stale work retires before preview cancellation or candidate publication.
        void               OpenScene(const char* path);
        [[nodiscard]] bool RequestSceneOpen(const ZEngine::Core::VFS::VFSPath& path, uint64_t expected_epoch);
        [[nodiscard]] bool ProcessSceneOpen(ZEngine::EngineContext& engine);
        [[nodiscard]] bool SceneOpenPending() const;
        /// Copies a drop intent with its original scene epoch; no I/O or authored write.
        [[nodiscard]] bool RequestMeshDrop(const ZEngine::Core::VFS::VFSPath& path, uint64_t expected_epoch);
        /// Import completion queues placement; only the shared mutation-point loader creates entities.
        [[nodiscard]] bool RequestImportedMesh(const ZEngine::EngineContext& engine, const ZEngine::Core::VFS::VFSPath& path, std::string_view name, const uuids::uuid& scene, uint64_t expected_epoch);
        /// Complete, unpublished candidate only. Must run in the mutation phase;
        /// rejection preserves both the candidate and current committed content.
        [[nodiscard]] bool PublishSceneCandidate(ZEngine::EngineContext& engine, ZEngine::Helpers::Scope<EditorSceneCandidate>& candidate, uint64_t expected_epoch);

    private:
        enum class ObjectPickPhase : uint32_t
        {
            Free      = 0,
            Queued    = 1,
            Recording = 2,
            Submitted = 3,
            Complete  = 4,
            Resolving = 5,
        };

        /// Main-thread-owned persistent pick table. The renderer sees it only
        /// through opaque callbacks in RenderObjectPickRequest and never holds
        /// an ECS Scene pointer. One fixed table bounds memory and avoids a
        /// second submission while a timeline-gated result is outstanding.
        struct ObjectPickFrame
        {
            PaddedAtomic<uint32_t> Phase                                                                  = {.value = static_cast<uint32_t>(ObjectPickPhase::Free)};
            PaddedAtomic<uint32_t> Cancelled                                                              = {};
            uint64_t               SceneEpoch                                                             = 0;
            uint64_t               ViewportRevision                                                       = 0;
            uint64_t               CameraRevision                                                         = 0;
            uint64_t               RenderTargetRevision                                                   = 0;
            float                  Pointer[2]                                                             = {};
            SelectionMode          Mode                                                                   = SelectionMode::Replace;
            uint32_t               Token                                                                  = 0;
            ZEngine::ECS::EntityID Entities[ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS + 1] = {};
        };
        static_assert(sizeof(ObjectPickFrame) <= 80 * 1024);

        static bool        BeginObjectPick(void* context);
        static void        RecordObjectPickToken(void* context, uint32_t token, ZEngine::ECS::EntityID entity);
        static void        SubmitObjectPick(void* context);
        static void        CompleteObjectPick(const void* data, size_t size, void* context);
        static void        CancelSubmittedObjectPick(void* context);
        void               UpdateCameraRevision(const ZEngine::Rendering::Cameras::CameraFrameData& camera);
        void               QueueViewportObjectPick(ZEngine::UI::ZUIContext* ctx, bool focused);
        void               QueueViewportHoverPick(ZEngine::UI::ZUIContext* ctx, bool focused);
        [[nodiscard]] bool CanQueueViewportPick(const ZEngine::UI::ZUIContext* ctx, bool focused) const;
        void               CancelObjectPick();
        void               CancelHoverPick();
        void               ResolveObjectPick();
        void               ResolveHoverPick();
        void               PrepareObjectPick(ZEngine::Applications::RenderFrameState& state);
        void               PrepareHoverPick(ZEngine::Applications::RenderFrameState& state);
        void               PrepareSelectionOutline(ZEngine::Applications::RenderFrameState& state);

        enum class SceneFileDrag : uint8_t
        {
            None,
            Control,
            Viewport,
        };
        void                        ProcessHistoryShortcut(ZEngine::UI::ZUIContext* ctx, bool redo);
        [[nodiscard]] SceneFileDrag SceneFileDragOwner(const ZEngine::UI::ZUIContext* ctx) const;
        [[nodiscard]] bool          CanUseSceneFileShortcut(const ZEngine::UI::ZUIContext* ctx, SceneFileDrag drag) const;
        void                        QueueSceneFileShortcut(const ZEngine::UI::ZUIContext* ctx, int keycode);
        void                        ProcessSceneFileInput(ZEngine::UI::ZUIContext* ctx, bool focused, bool before_requests = false);

        enum class SceneFileCommand : uint8_t
        {
            None,
            New,
            Open,
            Save,
            SaveAs,
        };
        // First accepted edge wins; never grow a queue of native picker callbacks.
        struct SceneFileInput
        {
            uint64_t         Epoch      = 0;
            uint64_t         PointerKey = 0, PointerSerial = 0;
            SceneFileCommand Command = SceneFileCommand::None;
            SceneFileDrag    Drag    = SceneFileDrag::None;
        } m_scene_file_input = {};
        static_assert(sizeof(SceneFileInput) <= 32);

        struct SceneSettingsRequest
        {
            ZEngine::Rendering::Scenes::SkyConfig SkyBefore = {}, SkyAfter = {};
            ZEngine::ECS::SceneGridSettings       GridBefore = {}, GridAfter = {};
            EntityTarget                          PrimaryTarget = {};
            uuids::uuid                           Scene         = {};
            uint64_t                              Epoch         = 0;
            bool                                  Sky = false, Grid = false;
        } m_scene_settings = {};
        static_assert(sizeof(SceneSettingsRequest) <= 1280);
        [[nodiscard]] bool SceneSettingsMatch() const;
        void               PrepareSceneSettingsRequest();
        // Capture once per picker/request, never per update. Borrowed owner tokens
        // are compared before dereferencing current owners; no old owner is followed.
        struct SceneFileOwner
        {
            EditorScene*                                                                  SceneOwner      = nullptr;
            ZEngine::ECS::Scene*                                                          World           = nullptr;
            ZEngine::Windows::CoreWindow*                                                 Window          = nullptr;
            EditorConfiguration*                                                          Config          = nullptr;
            uuids::uuid                                                                   SceneUUID       = {};
            uint64_t                                                                      Epoch           = 0;
            ZEngine::Core::Containers::FixedArray<char, ZEngine::Core::VFS::VFS_MAX_PATH> Workspace       = {};
            size_t                                                                        WorkspaceLength = 0;
            bool                                                                          HasWorkspace    = false;

            [[nodiscard]] bool                                                            Capture(const Editor& editor);
            [[nodiscard]] bool                                                            Matches(const Editor& editor) const;
            [[nodiscard]] bool                                                            MatchesWorkspace(cstring current) const;
            void                                                                          Reset()
            {
                Epoch           = 0;
                WorkspaceLength = 0; // unused path bytes are never copied, scanned or cleared
            }
        };
        static_assert(sizeof(SceneFileOwner) <= ZEngine::Core::VFS::VFS_MAX_PATH + 80);
        struct SceneFileRequest
        {
            ZEngine::Core::VFS::VFSPath Path    = {};
            SceneFileOwner              Owner   = {};
            bool                        Pending = false;
        };
        SceneFileRequest                    m_scene_save              = {};
        SceneFileOwner                      m_scene_file_input_owner  = {};
        ZEngine::Windows::FileDialogRequest m_scene_save_picker       = {};
        SceneFileOwner                      m_scene_save_picker_owner = {};
        SceneFileOwner                      m_new_scene_owner         = {};
        bool                                m_new_scene_pending       = false;
        SceneFileOwner                      m_editor_close_owner      = {};
        bool                                m_editor_close_pending    = false;
        struct SceneOpenWork;
        SceneFileRequest                              m_scene_open              = {};
        ZEngine::Windows::FileDialogRequest           m_scene_open_picker       = {};
        SceneFileOwner                                m_scene_open_picker_owner = {};
        ZEngine::Helpers::Scope<SceneOpenWork>        m_scene_open_work;
        ZEngine::Helpers::Scope<EditorSceneCandidate> m_loaded_scene       = {};
        ZEngine::ECS::Scene*                          m_original_world     = nullptr;
        ZEngine::ECS::ActorManager*                   m_original_actors    = nullptr;
        bool                                          m_closing            = false;
        // A rapid native loss/regain still cancels at the next safe mutation point.
        bool                                          m_focus_lost_pending = false;
        bool                                          m_gizmo_focused      = true;
        EditorSceneChange                             m_scene_change       = {};
        uint64_t                                      m_scene_prompt_focus = 0;
        ObjectPickFrame                               m_object_pick        = {};
        ObjectPickFrame                               m_hover_pick         = {};
        static_assert(sizeof(ObjectPickFrame) * 2 <= 160 * 1024);
        ZEngine::Rendering::Cameras::CameraFrameData m_last_pick_camera       = {};
        uint64_t                                     m_camera_revision        = 1;
        uint64_t                                     m_render_target_revision = 0;
        uint64_t                                     m_last_pick_release      = 0;
        bool                                         m_pick_camera_valid      = false;
        [[nodiscard]] SceneChangeGate                GateSceneChange(SceneChangeKind kind);
        void                                         ProcessSceneChangeInput(ZEngine::UI::ZUIContext* ctx, bool focused);
        void                                         CancelSceneOpen();
    };
    ZDEFINE_PTR(Editor);

} // namespace Tetragrama
