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

        void                  Update(ZEngine::Core::TimeStep dt) override;

        virtual void          Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Rendering::Buffers::CommandBuffer* const command_buffer) override;

        void                  RenderContentBrowser(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer);
        void                  RenderTreeBrowser();
        void                  RenderDirectoryNode(const std::filesystem::path& directory);
        void                  HandleFolderContextMenu(const std::filesystem::path& path);
        void                  HandleCreateFolderPopup();
        void                  RenderGridItem(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::filesystem::directory_entry& entry);
        void                  RenderSearchResults(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::string& searchTerm);
        void                  RenderBackButton();

        std::filesystem::path MakeRelative(const std::filesystem::path& path, const std::filesystem::path& base);

    private:
        const std::filesystem::path                 m_assets_directory = std::filesystem::path("Assets");
        std::filesystem::path                       m_currentDirectory;
        std::filesystem::path                       m_lastRenderedFolder;

        ZEngine::Rendering::Textures::TextureHandle m_directoryIcon;
        ZEngine::Rendering::Textures::TextureHandle m_fileIcon;
        bool                                        m_texturesLoaded     = false;
        static constexpr float                      m_thumbnailSize      = 128.0f;
        char                                        m_search_buffer[256] = "";

        bool                                        m_show_create_folder = false;
        std::filesystem::path                       m_create_folder_path;
        std::string                                 m_new_folder_name = "New Folder";
    };
} // namespace Tetragrama::Components
