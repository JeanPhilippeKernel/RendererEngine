#pragma once
#include <Tetragrama/Components/UIComponent.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <ZEngine/Importers/GltfImporter.h>
#include <ZEngine/ZEngineDef.h>
#include <mutex>

namespace Tetragrama::Components
{
    enum class ImporterState : uint8_t
    {
        Idle      = 0,
        Options   = 1,
        Importing = 2,
    };

    class AssetImporterUIComponent : public UIComponent
    {
    public:
        AssetImporterUIComponent()                       = default;
        ~AssetImporterUIComponent() override             = default;

        ZEngine::Core::Memory::ArenaAllocator LocalArena = {};

        void                                  Initialize(Layers::ImguiLayer* parent = nullptr, cstring name = "Asset Importer", bool visibility = true, bool closed = false) override;
        void                                  Update(ZEngine::Core::TimeStep dt) override;
        virtual void                          Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

    private:
        PaddedAtomic<ImporterState> m_state{}; // default = Idle (0)
        PaddedAtomic<float>         m_progress{};
        PaddedAtomic<bool>          m_pending_scan{}; // drained on main thread after background import

        // Selected file
        char                        m_path_buf[1024]     = {};
        bool                        m_add_to_scene       = false; // import was triggered by viewport drag-drop
        char                        m_instance_name[256] = {};

        // Import settings (shown in Options state)
        float                       m_scale              = 1.0f;
        int                         m_axis_index         = 0; // 0 = Y-Up, 1 = Z-Up
        bool                        m_gen_normals        = true;
        bool                        m_merge_vertices     = true;
        bool                        m_import_materials   = true;
        bool                        m_import_textures    = true;

        // Compact import log (Importing state — ring buffer)
        struct LogEntry
        {
            char  Text[256] = {};
            float Color[4]  = {0.8f, 0.8f, 0.8f, 1.0f};
        };
        static constexpr int kLogMax        = 64;
        LogEntry             m_log[kLogMax] = {};
        int                  m_log_head     = 0;
        int                  m_log_count    = 0;
        std::mutex           m_log_mutex;
        bool                 m_scroll_log = false;

        // Recent imports history (Idle state)
        struct HistoryEntry
        {
            char Name[256]    = {};
            char Message[256] = {};
            bool Success      = false;
        };
        static constexpr int                kHistMax            = 16;
        HistoryEntry                        m_history[kHistMax] = {};
        int                                 m_history_count     = 0;

        // Importers — allocated from parent arena in Initialize()
        ZEngine::Importers::GltfImporter*   m_gltf_importer     = nullptr;
        ZEngine::Importers::AssimpImporter* m_assimp_importer   = nullptr;

        void                                PushLog(cstring text, const float color[4]);
        void                                PushHistory(cstring name, bool success, cstring msg);
        void                                TriggerScan(); // main-thread only
        void                                StartImport();
        void                                BrowseFile();
        void                                RenderIdle();
        void                                RenderOptions();
        void                                RenderImporting();

        // Callback for ImportFile — receives cooked output paths
        static void                         OnImportFileComplete(void* ctx, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> outputs);
        static void                         OnImportProgress(void* ctx, float pct);
        static void                         OnImportError(void* ctx, std::string_view msg);
        static void                         OnImportLog(void* ctx, std::string_view msg);
    };
} // namespace Tetragrama::Components
