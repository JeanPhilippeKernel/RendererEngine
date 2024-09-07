#pragma once
#include <Importers/IAssetImporter.h>
#include <Message.h>
#include <Serializers/EditorSceneSerializer.h>
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components
{
    class DockspaceUIComponent : public ZEngine::Components::UI::UIComponent
    {
    public:
        DockspaceUIComponent(std::string_view name = "Dockspace", bool visibility = true);
        virtual ~DockspaceUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render() override;

        void RenderMenuBar();
        void RenderSaveScene();
        void RenderSaveSceneAs();
        void ResetSaveAsBuffers();
        void RenderExitPopup();

        /*
         * Model Importer Funcs
         */
        void        RenderImporter();
        void        ResetImporterBuffers();
        static void OnAssetImporterComplete(Importers::ImporterData&&);
        static void OnAssetImporterProgress(float value);
        static void OnAssetImporterError(std::string_view);
        static void OnAssetImporterLog(std::string_view);

        /*
         * Editor Scene Funcs
         */
        static void OnEditorSceneSerializerProgress(float value);
        static void OnEditorSceneSerializerComplete();
        static void OnEditorSceneSerializerDeserializeComplete(EditorScene&&);
        static void OnEditorSceneSerializerError(std::string_view);

        std::future<void> OnNewSceneAsync();
        std::future<void> OnOpenSceneAsync();
        std::future<void> OnImportAssetAsync(std::string_view filename);
        std::future<void> OnExitAsync();

    private:
        static ImVec4      s_asset_importer_report_msg_color;
        static std::string s_asset_importer_report_msg;
        static char        s_asset_importer_input_buffer[1024];
        static char        s_save_as_input_buffer[1024];
        static float       s_editor_scene_serializer_progress;

    private:
        bool                                               m_open_asset_importer{false};
        bool                                               m_open_exit{false};
        bool                                               m_pending_shutdown{false};
        bool                                               m_open_save_scene{false};
        bool                                               m_open_save_scene_as{false};
        bool                                               m_request_save_scene_ui_close{false};
        ImGuiDockNodeFlags                                 m_dockspace_node_flag;
        ImGuiWindowFlags                                   m_window_flags;
        Importers::ImportConfiguration                     m_default_import_configuration;
        ZEngine::Scope<Importers::IAssetImporter>          m_asset_importer;
        ZEngine::Scope<Serializers::EditorSceneSerializer> m_editor_serializer;
    };
} // namespace Tetragrama::Components
