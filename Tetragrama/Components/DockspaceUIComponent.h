#pragma once
#include <Tetragrama/Components/UIComponent.h>
#include <Tetragrama/Messengers/Message.h>
#include <Tetragrama/Serializers/EditorSceneSerializer.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <imgui.h>

namespace Tetragrama::Components
{
    enum class ThemeId
    {
        Dark  = 0,
        Light = 1,
    };

    enum class SettingsPageId
    {
        Grid     = 0,
        Renderer = 1,
        Theme    = 2,
        COUNT
    };

    class DockspaceUIComponent : public UIComponent
    {
    public:
        DockspaceUIComponent();
        virtual ~DockspaceUIComponent();

        ZEngine::Core::Memory::ArenaAllocator LocalArena = {};

        void                                  Initialize(Layers::ImguiLayer* parent = nullptr, const char* name = "Dockspace", bool visibility = true, bool closed = false) override;

        void                                  Update(ZEngine::Core::TimeStep dt) override;
        virtual void                          Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        void                                  RenderMenuBar();
        void                                  ResetSaveAsBuffers();
        void                                  RenderExitPopup();

        /*
         * Model Importer Funcs
         */
        void                                  RenderImporter();
        void                                  ResetImporterBuffers();
        static void                           OnAssetImporterComplete(void* const context, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> result);
        static void                           OnAssetImporterProgress(void* const, float value);
        static void                           OnAssetImporterError(void* const, std::string_view);
        static void                           OnAssetImporterLog(void* const, std::string_view);

        /*
         * Performances Menu Windows
         */
        void                                  RenderMemoryProfilerWindow();

        /*
         * Engine Settings Window
         */
        void                                  RenderEngineSettingsWindow();
        void                                  RenderSettingsContentGrid();
        void                                  RenderSettingsContentRenderer();
        void                                  RenderSettingsContentTheme();
        static void                           DrawSettingsIcon(ImDrawList* dl, ImVec2 pos, SettingsPageId id, bool selected);
        void                                  ApplyTheme(ThemeId theme);

        /*
         * Editor Scene Funcs
         */
        void                                  RenderLoadScene();
        void                                  RenderSaveScene();
        void                                  RenderSaveSceneAs();
        static void                           OnEditorSceneSerializerProgress(void* const, float value);
        static void                           OnEditorSceneSerializerComplete(void* const);
        static void                           OnEditorSceneSerializerDeserializeComplete(void* const, EditorScene&&);
        static void                           OnEditorSceneSerializerError(void* const, std::string_view);
        static void                           OnEditorSceneSerializerLog(void* const, std::string_view);

        std::future<void>                     OnNewSceneAsync();
        std::future<void>                     OnOpenSceneAsync();
        std::future<void>                     OnOpenSceneRequestAsync(const char* filename);
        std::future<void>                     OnOpenMeshRequestAsync(const char* filename);
        std::future<void>                     OnImportAssetAsync(const char* filename);
        std::future<void>                     OnExitAsync();

    private:
        static ImVec4      s_asset_importer_report_msg_color;
        static std::string s_asset_importer_report_msg;
        static char        s_asset_importer_input_buffer[1024];
        static char        s_save_as_input_buffer[1024];
        static float       s_editor_scene_serializer_progress;

    private:
        bool                 m_open_asset_importer{false};
        bool                 m_open_engine_settings{false};
        bool                 m_open_memory_profiler{false};

        static constexpr int kMemHistorySize = 128;
        static constexpr int kMaxArenas      = 32;
        struct ArenaHistory
        {
            float    samples[kMemHistorySize] = {};
            int      head                     = 0;
            uint32_t count                    = 0;
        };
        ArenaHistory                                        m_arena_history[kMaxArenas] = {};
        uint32_t                                            m_arena_history_count       = 0;
        bool                                                m_open_exit{false};
        ThemeId                                             m_active_theme{ThemeId::Light};
        bool                                                m_pending_shutdown{false};
        bool                                                m_open_save_scene{false};
        bool                                                m_open_save_scene_as{false};
        bool                                                m_request_save_scene_ui_close{false};
        SettingsPageId                                      m_active_settings_page{SettingsPageId::Theme};
        ImGuiDockNodeFlags                                  m_dockspace_node_flag;
        ImGuiWindowFlags                                    m_window_flags;
        ZEngine::Importers::AssetCodec::ImportConfiguration m_default_import_configuration;
        ZRawPtr(ZEngine::Importers::AssimpImporter) m_asset_importer;
        ZRawPtr(Serializers::EditorSceneSerializer) m_editor_serializer;
    };
} // namespace Tetragrama::Components
