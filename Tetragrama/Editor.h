#pragma once
#include <EditorCameraController.h>
#include <Layers/ImguiLayer.h>
#include <Layers/RenderLayer.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
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

        void                                                                Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, const char* scene_name = "");
        void                                                                Push(ZEngine::Core::Memory::ArenaAllocator* arena, const char* mesh, const char* model, const char* material);
        bool                                                                HasPendingChange() const;

        const char*                                                         Name          = "";
        ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String> MeshFiles     = {};
        ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String> ModelFiles    = {};
        ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String> MaterialFiles = {};

        ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String> Hashes        = {};
        std::map<const char*, Model>                                        Data          = {};

        ZRawPtr(ZEngine::Rendering::Scenes::GraphicScene) RenderScene                     = nullptr;

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
        ZEngine::Core::Containers::String WorkingSpacePath         = {};
        ZEngine::Core::Containers::String DefaultImportTexturePath = {};
        ZEngine::Core::Containers::String DefaultImportSoundPath   = {};
        ZEngine::Core::Containers::String ScenePath                = {};
        ZEngine::Core::Containers::String SceneDataPath            = {};
        ZEngine::Core::Containers::String ProjectName              = {};
        ZEngine::Core::Containers::String ActiveSceneName          = {};

        void                              ReadConfig(ZEngine::Core::Memory::ArenaAllocator* arena, const char* file);
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
