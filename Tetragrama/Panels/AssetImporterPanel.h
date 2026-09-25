#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <ZEngine/Importers/FbxImporter.h>
#include <ZEngine/Importers/GltfImporter.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <uuid.h>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace Tetragrama::Panels
{
    struct ProjectViewPanel;

    /// @brief Three-state asset importer panel (Idle → Options → Importing).
    ///        Supports glTF/GLB, FBX, and Assimp-backed formats.  Import runs
    ///        on a background thread; completion is finalized on the main thread.
    struct AssetImporterPanel : ZEngine::UI::ZUIPanelView
    {
        AssetImporterPanel()
        {
            Title = "Importer";
        }

        /// @brief Allocates importers and arenas; must be called once before first use.
        /// @param layer Owning ZUI layer (provides arena and app pointers).
        /// @param project_view Content browser refreshed after a successful import.
        void Initialize(Tetragrama::Layers::ZUILayer* layer, ProjectViewPanel* project_view);

        /// @brief Dispatches to BuildIdle/BuildOptions/BuildImporting based on current state.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        Tetragrama::Layers::ZUILayer*         m_layer                 = nullptr;
        ProjectViewPanel*                     m_project_view          = nullptr;

        // Scratch arena for ImportConfiguration strings (cleared before each import)
        ZEngine::Core::Memory::ArenaAllocator m_local_arena           = {};
        // Importer-dedicated arenas carved from the engine ImportPipeline budget
        ZEngine::Core::Memory::ArenaAllocator m_gltf_importer_arena   = {};
        ZEngine::Core::Memory::ArenaAllocator m_assimp_importer_arena = {};

        // Importers (initialized in Initialize)
        ZEngine::Importers::GltfImporter*     m_gltf_importer         = nullptr;
        ZEngine::Importers::FbxImporter*      m_fbx_importer          = nullptr;
        ZEngine::Importers::AssimpImporter*   m_assimp_importer       = nullptr;

        enum class ImporterKind : uint8_t
        {
            Gltf,
            Fbx,
            Assimp,
        };

        enum class ImportTaskOutcome : uint8_t
        {
            Pending,
            Completed,
            Failed,
        };

        struct ImportTask
        {
            AssetImporterPanel*                                  Panel                           = nullptr;
            ZEngine::Importers::AssetCodec::ImportConfiguration  Configuration                   = {};
            ImporterKind                                         Kind                            = ImporterKind::Gltf;
            char                                                 SourcePath[MAX_FILE_PATH_COUNT] = {};
            // The importer releases its scratch arena after it calls its callback.
            // Retain the small output list until the main thread consumes it.
            std::vector<ZEngine::Importers::AssetImporterOutput> Outputs                         = {};
            std::string                                          Error                           = {};
            ImportTaskOutcome                                    Outcome                         = ImportTaskOutcome::Pending;
        };
        ImportTask m_import_task = {};

        // State machine (shared across main and background threads)
        enum class ImporterState : uint8_t
        {
            Idle      = 0, ///< No import in progress.
            Options   = 1, ///< Displaying import options to the user.
            Importing = 2  ///< Import is running.
        };
        PaddedAtomic<ImporterState> m_state              = {};
        PaddedAtomic<float>         m_progress           = {};

        // Import settings
        char                        m_path_buf[1024]     = {};
        bool                        m_add_to_scene       = false; // true when triggered by viewport drag-drop
        char                        m_instance_name[256] = {};
        bool                        m_use_source_name    = true;
        float                       m_scale              = 1.f;
        int                         m_axis_index         = 0; // 0=Y-Up, 1=Z-Up
        int                         m_normals_mode       = 1; // 0=Off, 1=Flat, 2=Smooth
        bool                        m_merge_vertices     = true;
        bool                        m_flip_uvs           = false;
        bool                        m_import_materials   = true;
        bool                        m_import_textures    = true;
        bool                        m_same_settings      = false;
        int                         m_options_filter     = 5; // 0=General 1=Mesh 2=Material 3=Anim 4=LOD 5=All

        // Import history ring buffer (Idle state, newest first)
        static constexpr int        kHistMax             = 16;
        struct HistEntry
        {
            char name[256]    = {};
            char message[256] = {};
            bool ok           = false;
        };
        HistEntry            m_history[kHistMax] = {};
        int                  m_hist_count        = 0;

        // Import log ring buffer (Importing state)
        static constexpr int kLogMax             = 64;
        struct LogEntry
        {
            char  text[256] = {};
            float color[4]  = {0.8f, 0.8f, 0.8f, 1.f};
        };
        LogEntry          m_log[kLogMax] = {};
        int               m_log_head     = 0;
        int               m_log_count    = 0;
        std::mutex        m_log_mutex;

        // Build helpers
        /// @brief Build the idle state UI (file selection).
        void              BuildIdle(ZEngine::UI::ZUIContext* ctx);
        /// @brief Build the options state UI (import settings).
        void              BuildOptions(ZEngine::UI::ZUIContext* ctx);
        /// @brief Build the in-progress state UI (progress bar, log).
        void              BuildImporting(ZEngine::UI::ZUIContext* ctx);

        /// @brief Open the file browser dialog synchronously.
        void              BrowseFile();
        /// @brief Open the file browser dialog asynchronously.
        std::future<void> BrowseFileAsync();
        /// @brief Begin the import process with the current settings.
        void              StartImport();
        /// @brief Add an imported mesh instance to the ECS scene on the main thread.
        void              CreateMeshActor(uuids::uuid mesh_uuid, uint32_t render_instance_id, const char* name);
        /// @brief Append a log message to the import log.
        void              PushLog(const char* text, float r, float g, float b);
        /// @brief Record the completed import in the history list.
        void              PushHistory(const char* name, bool ok, const char* msg);

        /// @brief Runs one import on a worker through the C-style task API.
        static void       RunImportTask(void* context);
        /// @brief Consumes a completed worker task on the main thread.
        static void       FinalizeImportTask(void* context);

        // Static callbacks for ImportFile. They execute on a worker, so they only
        // copy the result into ImportTask; FinalizeImportTask performs editor work.
        static void       OnImportFileComplete(void* ctx, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs);
        static void       OnImportProgress(void* ctx, float pct);
        static void       OnImportError(void* ctx, std::string_view err);
        static void       OnImportLog(void* ctx, std::string_view msg);

        // Main-thread-only completion handlers.
        static void       CompleteImportOnMainThread(void* ctx, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs);
        static void       CompleteImportErrorOnMainThread(void* ctx, std::string_view err);
    };

} // namespace Tetragrama::Panels
