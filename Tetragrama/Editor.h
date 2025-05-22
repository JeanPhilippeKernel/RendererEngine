#pragma once
#include <AssetManager.h>
#include <EditorCameraController.h>
#include <EditorScene.h>
#include <Helpers/NodeHierarchyHelper.h>
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
        std::atomic_int                       SelectedSceneNode          = -1;
        ZRawPtr(EditorConfiguration) ConfigurationPtr                    = nullptr;
        ZRawPtr(EditorScene) CurrentScenePtr                             = nullptr;
        ZRawPtr(Controllers::EditorCameraController) CameraControllerPtr = nullptr;
        ZRawPtr(Managers::AssetManager) AssetManagerPtr                  = nullptr;
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
