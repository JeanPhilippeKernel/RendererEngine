#pragma once
#include <UIComponent.h>
#include <string>

namespace Tetragrama::Components
{
    enum class ContextMenuType
    {
        RightPane,
        LeftPane,
        File,
        Folder
    };

    enum class PopupType
    {
        None,
        CreateFolder,
        CreateNewFile,
        RenameFolder,
        RenameFile,
        DeleteFolder,
        DeleteFiled,
    };

    class ProjectViewUIComponent : public UIComponent
    {
    public:
        ProjectViewUIComponent(Layers::ImguiLayer* parent = nullptr, std::string_view name = "Project", bool visibility = true);
        virtual ~ProjectViewUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        // Render Panes
        void         RenderContentBrowser(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer);
        void         RenderFilteredContent(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::string& searchTerm);
        void         RenderDirectoryNode(const std::filesystem::path& directory);
        void         RenderContentTile(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::filesystem::directory_entry& entry);
        void         RenderBackButton();
        void         RenderTreeBrowser();

        // Popup helpers
        void         RenderContextMenu(ContextMenuType type, const std::filesystem::path& targetPath);
        void         RenderPopUpMenu();
        void         HandleCreateFolderPopup(const std::filesystem::path& path);
        void         HandleCreateFilePopup(const std::filesystem::path& path);

        void         HandleRenameFolderPopup(const std::filesystem::path& path);
        void         HandleDeleteFolderPopup(const std::filesystem::path& path);
        void         HandleRenameFilePopup(const std::filesystem::path& path);
        void         HandleDeleteFilePopup(const std::filesystem::path& path);

    private:
        const std::filesystem::path                 m_assets_directory = std::filesystem::path("Assets");
        std::filesystem::path                       m_currentDirectory;
        PopupType                                   m_active_popup = PopupType::None;
        std::filesystem::path                       m_popup_target_path;

        ZEngine::Rendering::Textures::TextureHandle m_directoryIcon;
        ZEngine::Rendering::Textures::TextureHandle m_fileIcon;
        bool                                        m_texturesLoaded     = false;
        static constexpr float                      m_thumbnailSize      = 128.0f;
        char                                        m_search_buffer[256] = "";
    };
} // namespace Tetragrama::Components
