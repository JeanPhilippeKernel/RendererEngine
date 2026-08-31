#pragma once
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
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
        ZEngine::Core::Containers::String ScenePath                = {};
        // Asset import directories (all under Assets/)
        ZEngine::Core::Containers::String TexturePath              = {};
        ZEngine::Core::Containers::String SoundPath                = {};
        ZEngine::Core::Containers::String MeshPath                 = {};
        ZEngine::Core::Containers::String MaterialPath             = {};
        ZEngine::Core::Containers::String SpritePath               = {};
        ZEngine::Core::Containers::String EnvironmentMapImportPath = {};
        ZEngine::Core::Containers::String ProjectName              = {};
        ZEngine::Core::Containers::String ActiveSceneName          = {};
        bool                              DarkTheme                = true;
        int                               GizmoOperation           = -1;
        bool                              ShowContentBrowser       = true;
        bool                              FocusContentBrowser      = false;
        bool                              ShowConsole              = false;
        bool                              FocusConsole             = false;
        bool                              ShowImporter             = false;
        bool                              FocusImporter            = false;
        char                              PendingImportPath[1024]  = {};
        char                              PendingImportName[256]   = {};

        void                              ReadConfig(ZEngine::Core::Memory::ArenaAllocator* arena, const char* file);
    };
    ZDEFINE_PTR(EditorConfiguration);

    struct Editor : public ZEngine::Applications::GameApplication
    {

        EditorConfigurationPtr Configuration = nullptr;

        virtual ~Editor() {}

        ZRawPtr(Layers::ZUILayer) ZUIUILayer                   = nullptr;

        ZEngine::Core::VFS::VFSDiskBackend WorkingSpaceBackend = {};

        virtual void                       OnInitializing() override;
        virtual void                       OverrideWindowConfiguration() override;
        virtual void                       OnInitialized() override;

        virtual void                       OnUpdate(float dt) override;
        virtual void                       OnEvent(ZEngine::Core::CoreEvent&) override;

        // Gates camera-controller routing on viewport hover (Gap 3)
        void                               ProcessEvent(ZEngine::Core::CoreEvent&) override;

        virtual void                       OnPreRender() override;
        virtual void                       OnPostRender() override;
        virtual void                       OnRenderUI() override;

        virtual void                       OnClosing() override;
        virtual void                       OnClosed() override;
    };
    ZDEFINE_PTR(Editor);

} // namespace Tetragrama
