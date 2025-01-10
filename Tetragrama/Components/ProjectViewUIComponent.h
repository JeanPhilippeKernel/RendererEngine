#pragma once
#include <UIComponent.h>
#include <filesystem>
#include <string>

namespace Tetragrama::Components
{
    class ProjectViewUIComponent : public UIComponent
    {
    public:
        ProjectViewUIComponent(std::string_view name = "Project", bool visibility = true);
        virtual ~ProjectViewUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Rendering::Buffers::CommandBuffer* const command_buffer) override;
        void         RenderBackButton();

    private:
        const std::filesystem::path                 m_assets_directory = std::filesystem::path("Assets");
        std::filesystem::path                       m_currentDirectory;

        ZEngine::Rendering::Textures::TextureHandle m_directoryIcon;
        ZEngine::Rendering::Textures::TextureHandle m_fileIcon;
        bool                                        m_texturesLoaded = false;
    };
} // namespace Tetragrama::Components
