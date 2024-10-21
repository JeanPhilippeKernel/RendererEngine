#pragma once
#include <Layers/RenderLayer.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Window/CoreWindow.h>
#include <ZEngine/ZEngineDef.h>

namespace Tetragrama::Serializers
{
    class EditorSceneSerializer;
} // namespace Tetragrama::Serializers

namespace Tetragrama
{
    class EditorScene : public ZEngine::Helpers::RefCounted
    {
    public:
        struct Model;

        EditorScene() = default;
        EditorScene(std::string_view name) : m_name(name) {}
        EditorScene(const EditorScene& scene) : m_name(scene.m_name), m_models(scene.m_models), m_has_pending_change(scene.m_has_pending_change.load()) {}
        EditorScene(std::string_view name, std::unordered_map<std::string, Model> models) : m_name(name), m_models(models) {}

        void AddModel(const Model&);
        void AddModel(Model&&);

        void                                          SetName(std::string_view name);
        std::string_view                              GetName() const;
        const std::unordered_map<std::string, Model>& GetModels() const;
        bool                                          HasPendingChange() const;

    private:
        std::atomic_bool                       m_has_pending_change;
        std::mutex                             m_mutex;
        std::string                            m_name;
        std::unordered_map<std::string, Model> m_models;
        friend class Serializers::EditorSceneSerializer;
    };

    struct EditorScene::Model
    {
        std::string Name          = {};
        std::string MeshesPath    = {};
        std::string MaterialsPath = {};
        std::string ModelPath     = {};
    };

    struct EditorConfiguration
    {
        std::string WorkingSpacePath;
        std::string DefaultImportTexturePath;
        std::string DefaultImportSoundPath;
        std::string ScenePath;
        std::string SceneDataPath;
        std::string ProjectName;
        std::string ActiveSceneName;
    };

    class Editor : ZEngine::Core::IInitializable, public ZEngine::Helpers::RefCounted
    {
    public:
        Editor(const EditorConfiguration&);
        virtual ~Editor();

        void Initialize() override;
        void Run();

        static const EditorConfiguration& GetCurrentEditorConfiguration();
        static ZEngine::Ref<EditorScene>  GetCurrentEditorScene();
        static void                       SetCurrentEditorScene(EditorScene&&);

    private:
        ZEngine::Ref<ZEngine::Window::CoreWindow> m_window;

    private:
        static EditorConfiguration        s_editor_configuration;
        static ZEngine::Ref<EditorScene>  s_editor_scene;
        static std::recursive_mutex       s_mutex;
        ZEngine::EngineConfiguration      m_engine_configuration;
        ZEngine::Ref<Layers::ImguiLayer>  m_ui_layer;
        ZEngine::Ref<Layers::RenderLayer> m_render_layer;
    };

} // namespace Tetragrama
