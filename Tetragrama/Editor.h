#pragma once
#include <EditorCameraController.h>
#include <Layers/ImguiLayer.h>
#include <Layers/RenderLayer.h>
#include <ZEngine/Core/Memory/Allocator.h>
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

        EditorScene()                                                 = default;

        char                         Name[50]                         = {0};
        std::vector<std::string>     MeshFiles                        = {};
        std::vector<std::string>     ModelFiles                       = {};
        std::vector<std::string>     MaterialFiles                    = {};
        std::map<std::string, Model> Data                             = {};

        ZRawPtr(ZEngine::Rendering::Scenes::GraphicScene) RenderScene = nullptr;

        void Push(std::string_view mesh, std::string_view model, std::string_view material);
        bool HasPendingChange() const;

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
        char WorkingSpacePath[MAX_FILE_PATH_COUNT]         = {0};
        char DefaultImportTexturePath[MAX_FILE_PATH_COUNT] = {0};
        char DefaultImportSoundPath[MAX_FILE_PATH_COUNT]   = {0};
        char ScenePath[MAX_FILE_PATH_COUNT]                = {0};
        char SceneDataPath[MAX_FILE_PATH_COUNT]            = {0};
        char ProjectName[50]                               = {0};
        char ActiveSceneName[50]                           = {0};

        void ReadConfig(std::string_view file);
    };

    struct EditorContext
    {
        ZEngine::Core::Memory::ArenaAllocator Arena                      = {};
        ZRawPtr(EditorConfiguration) ConfigurationPtr                    = nullptr;
        ZRawPtr(EditorScene) CurrentScenePtr                             = nullptr;
        ZRawPtr(Controllers::EditorCameraController) CameraControllerPtr = nullptr;
    };

    struct Editor
    {
        ~Editor();

        ZRawPtr(EditorContext) Context               = nullptr;
        ZRawPtr(Layers::ImguiLayer) UILayer          = nullptr;
        ZRawPtr(Layers::RenderLayer) CanvasLayer     = nullptr;
        ZRawPtr(ZEngine::Windows::CoreWindow) Window = nullptr;

        void Initialize(ZEngine::Core::Memory::ArenaAllocator*, const char*);
        void Run();
    };

} // namespace Tetragrama
