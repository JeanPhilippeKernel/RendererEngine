#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <ZEngine/Importers/FbxImporter.h>
#include <ZEngine/Importers/GltfImporter.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <future>
#include <mutex>
#include <uuid.h>

namespace Tetragrama::Panels
{
    struct AssetImporterPanel : ZEngine::UI::ZUIPanelView
    {
        AssetImporterPanel()
        {
            Title = "Importer";
        }

        void Initialize(Tetragrama::Layers::ZUILayer* layer);
        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        Tetragrama::Layers::ZUILayer* m_layer = nullptr;

        // Scratch arena for ImportConfiguration strings (cleared before each import)
        ZEngine::Core::Memory::ArenaAllocator m_local_arena          = {};
        // Importer-dedicated arenas carved from the engine ImportPipeline budget
        ZEngine::Core::Memory::ArenaAllocator m_gltf_importer_arena  = {};
        ZEngine::Core::Memory::ArenaAllocator m_assimp_importer_arena = {};

        // Importers (initialized in Initialize)
        ZEngine::Importers::GltfImporter*   m_gltf_importer   = nullptr;
        ZEngine::Importers::FbxImporter*    m_fbx_importer    = nullptr;
        ZEngine::Importers::AssimpImporter* m_assimp_importer = nullptr;

        // State machine (shared across main and background threads)
        enum class ImporterState : uint8_t { Idle = 0, Options = 1, Importing = 2 };
        PaddedAtomic<ImporterState> m_state    = {};
        PaddedAtomic<float>         m_progress = {};

        // Import settings
        char  m_path_buf[1024]     = {};
        bool  m_add_to_scene       = false; // true when triggered by viewport drag-drop
        char  m_instance_name[256] = {};
        bool  m_use_source_name    = true;
        float m_scale              = 1.f;
        int   m_axis_index         = 0; // 0=Y-Up, 1=Z-Up
        int   m_normals_mode       = 1; // 0=Off, 1=Flat, 2=Smooth
        bool  m_merge_vertices     = true;
        bool  m_flip_uvs           = false;
        bool  m_import_materials   = true;
        bool  m_import_textures    = true;
        bool  m_same_settings      = false;
        int   m_options_filter     = 5; // 0=General 1=Mesh 2=Material 3=Anim 4=LOD 5=All

        // Pending ECS actor creation — written on bg thread, consumed on main thread
        struct PendingActor
        {
            uuids::uuid uuid      = {};
            uint32_t    render_id = UINT32_MAX;
            char        name[128] = {};
            bool        valid     = false;
        } m_pending_actor = {};

        // Import history ring buffer (Idle state, newest first)
        static constexpr int kHistMax = 16;
        struct HistEntry
        {
            char name[256]    = {};
            char message[256] = {};
            bool ok           = false;
        };
        HistEntry m_history[kHistMax] = {};
        int       m_hist_count        = 0;

        // Import log ring buffer (Importing state)
        static constexpr int kLogMax = 64;
        struct LogEntry
        {
            char  text[256]         = {};
            float color[4]          = {0.8f, 0.8f, 0.8f, 1.f};
        };
        LogEntry   m_log[kLogMax]   = {};
        int        m_log_head       = 0;
        int        m_log_count      = 0;
        bool       m_scroll_log     = false;
        std::mutex m_log_mutex;

        // Build helpers
        void BuildIdle(ZEngine::UI::ZUIContext* ctx);
        void BuildOptions(ZEngine::UI::ZUIContext* ctx);
        void BuildImporting(ZEngine::UI::ZUIContext* ctx);

        void              BrowseFile();
        std::future<void> BrowseFileAsync();
        void              StartImport();
        void              TriggerScan(); // main-thread only
        void              PushLog(const char* text, float r, float g, float b);
        void              PushHistory(const char* name, bool ok, const char* msg);

        // Static callbacks for ImportFile (called from background thread)
        static void OnImportFileComplete(void* ctx, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs);
        static void OnImportProgress(void* ctx, float pct);
        static void OnImportError(void* ctx, std::string_view err);
        static void OnImportLog(void* ctx, std::string_view msg);
    };

} // namespace Tetragrama::Panels
