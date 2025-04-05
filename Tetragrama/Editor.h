#pragma once
#include <EditorCameraController.h>
#include <Layers/ImguiLayer.h>
#include <Layers/RenderLayer.h>
#include <ZEngine/Core/Container/Array.h>
#include <ZEngine/Core/Container/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Windows/CoreWindow.h>

namespace Tetragrama::Serializers
{
    struct EditorSceneSerializer;
} // namespace Tetragrama::Serializers

namespace Tetragrama
{
    class EditorScene
    {
    public:
        struct Model;

        EditorScene() = default;

        void                                                              Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, const char* scene_name = "");
        void                                                              Push(ZEngine::Core::Memory::ArenaAllocator* arena, const char* mesh, const char* model, const char* material);
        bool                                                              HasPendingChange() const;

        const char*                                                       Name          = "";
        ZEngine::Core::Container::Array<ZEngine::Core::Container::String> MeshFiles     = {};
        ZEngine::Core::Container::Array<ZEngine::Core::Container::String> ModelFiles    = {};
        ZEngine::Core::Container::Array<ZEngine::Core::Container::String> MaterialFiles = {};

        ZEngine::Core::Container::Array<ZEngine::Core::Container::String> Hashes        = {};
        std::map<const char*, Model>                                      Data          = {};

        ZRawPtr(ZEngine::Rendering::Scenes::GraphicScene) RenderScene                   = nullptr;

    private:
        std::atomic_bool m_has_pending_change;
        friend struct Serializers::EditorSceneSerializer;
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
        ~Editor() {}

        ZRawPtr(EditorContext) Context               = nullptr;
        ZRawPtr(Layers::ImguiLayer) UILayer          = nullptr;
        ZRawPtr(Layers::RenderLayer) CanvasLayer     = nullptr;
        ZRawPtr(ZEngine::Windows::CoreWindow) Window = nullptr;

        void Initialize(ZEngine::Core::Memory::ArenaAllocator*, const char*);
        void Dispose();
        void Run();
    };

} // namespace Tetragrama
