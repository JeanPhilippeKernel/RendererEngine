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
        CreateFile,
        RenameFolder,
        RenameFile,
        DeleteFolder,
        DeleteFile,
    };

    class ProjectViewUIComponent : public UIComponent
    {
    public:
        ProjectViewUIComponent(Layers::ImguiLayer* parent = nullptr, std::string_view name = "Project", bool visibility = true);
        virtual ~ProjectViewUIComponent();

        void                  Update(ZEngine::Core::TimeStep dt) override;

        virtual void          Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        // Render Panes
        void                  RenderContentBrowser(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer);
        void                  RenderFilteredContent(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, std::string_view searchTerm);
        void                  RenderDirectoryNode(const std::filesystem::path& directory);
        void                  RenderContentTile(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::filesystem::directory_entry& entry);
        void                  RenderBackButton();
        void                  RenderTreeBrowser();

        // Popup helpers
        void                  RenderContextMenu(ContextMenuType type, const std::filesystem::path& targetPath);
        void                  RenderPopUpMenu();
        void                  HandleCreateFolderPopup(const std::filesystem::path& path);
        void                  HandleCreateFilePopup(const std::filesystem::path& path);

        void                  HandleRenameFolderPopup(const std::filesystem::path& path);
        void                  HandleDeleteFolderPopup(const std::filesystem::path& path);
        void                  HandleRenameFilePopup(const std::filesystem::path& path);
        void                  HandleDeleteFilePopup(const std::filesystem::path& path);

        std::filesystem::path MakeRelative(const std::filesystem::path& path, const std::filesystem::path& base);

    private:
        std::filesystem::path                       m_assets_directory;
        std::filesystem::path                       m_current_directory;
        PopupType                                   m_active_popup = PopupType::None;
        std::filesystem::path                       m_popup_target_path;

        ZEngine::Rendering::Textures::TextureHandle m_directory_icon;
        ZEngine::Rendering::Textures::TextureHandle m_file_icon;
        bool                                        m_textures_loaded                    = false;
        static constexpr float                      m_thumbnail_size                     = 64.0f;
        char                                        m_search_buffer[MAX_FILE_PATH_COUNT] = "";
    };
} // namespace Tetragrama::Components
