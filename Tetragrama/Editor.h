#pragma once
#include <EditorScene.h>
#include <Layers/ImguiLayer.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Managers/AssetManager.h>

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
    ZDEFINE_PTR(EditorConfiguration);

    struct Editor : public ZEngine::Applications::GameApplication
    {

        EditorConfigurationPtr Configuration = nullptr;

        virtual ~Editor() {}

        ZRawPtr(Layers::ImguiLayer) UILayer = nullptr;

        virtual void OnInitializing() override;
        virtual void OverrideWindowConfiguration() override;
        virtual void OnInitialized() override;

        virtual void OnUpdate(float dt) override;
        virtual void OnEvent(ZEngine::Core::CoreEvent&) override;

        virtual void OnPreRender() override;
        virtual void OnPostRender() override;
        virtual void OnRenderUI() override;

        virtual void OnClosing() override;
        virtual void OnClosed() override;
    };
    ZDEFINE_PTR(Editor);

} // namespace Tetragrama
