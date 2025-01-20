#pragma once
#include <EditorCameraController.h>
#include <Layers/ImguiLayer.h>
#include <Layers/RenderLayer.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <vector>

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
        EditorScene(std::string_view name) : Name(name) {}
        EditorScene(const EditorScene& scene) : Name(scene.Name), m_has_pending_change(scene.m_has_pending_change.load()) {}

        std::string                                                     Name          = {};
        std::vector<std::string>                                        MeshFiles     = {};
        std::vector<std::string>                                        ModelFiles    = {};
        std::vector<std::string>                                        MaterialFiles = {};
        std::map<std::string, Model>                                    Data          = {};

        ZEngine::Helpers::Ref<ZEngine::Rendering::Scenes::GraphicScene> RenderScene   = ZEngine::Helpers::CreateRef<ZEngine::Rendering::Scenes::GraphicScene>();

        void                                                            Push(std::string_view mesh, std::string_view model, std::string_view material);
        bool                                                            HasPendingChange() const;

    private:
        std::atomic_bool m_has_pending_change;
        friend class Serializers::EditorSceneSerializer;
    };

    struct EditorScene::Model
    {
        uint16_t MeshFileIndex     = 0xFFFF;
        uint16_t ModelPathIndex    = 0xFFFF;
        uint16_t MaterialPathIndex = 0xFFFF;
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

    struct EditorContext : public ZEngine::Helpers::RefCounted
    {
        EditorConfiguration*                 ConfigurationPtr    = nullptr;
        EditorScene*                         CurrentScenePtr     = nullptr;
        Controllers::EditorCameraController* CameraControllerPtr = nullptr;
    };

    class Editor : ZEngine::Core::IInitializable, public ZEngine::Helpers::RefCounted
    {
    public:
        Editor(const EditorConfiguration&);
        virtual ~Editor();

        EditorConfiguration                                        Configuration    = {};
        ZEngine::Helpers::Ref<EditorContext>                       Context          = nullptr;
        ZEngine::Helpers::Ref<Layers::ImguiLayer>                  UILayer          = nullptr;
        ZEngine::Helpers::Ref<Layers::RenderLayer>                 CanvasLayer      = nullptr;
        ZEngine::Helpers::Ref<Controllers::EditorCameraController> CameraController = nullptr;
        ZEngine::Helpers::Ref<EditorScene>                         CurrentScene     = nullptr;

        void                                                       Initialize() override;
        void                                                       Run();

    private:
        std::recursive_mutex                                m_mutex;
        ZEngine::Helpers::Ref<ZEngine::Windows::CoreWindow> m_window;
    };

} // namespace Tetragrama
