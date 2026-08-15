#pragma once
#include <Tetragrama/Components/UIComponent.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <vector>
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
        NewFile,
        RenameFolder,
        RenameFile,
        DeleteFolder,
        RemoveFile,
    };

    class ProjectViewUIComponent : public UIComponent
    {
    public:
        ProjectViewUIComponent();
        virtual ~ProjectViewUIComponent();

        void         Initialize(Layers::ImguiLayer* parent = nullptr, const char* name = "Project", bool visibility = true, bool closed = false) override;

        void         Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        // Render Panes
        void         RenderTopBar(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer);
        void         RenderContentBrowser(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer);
        void         RenderFilteredContent(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const char* searchTerm);
        void         RenderDirectoryNode(const ZEngine::Core::VFS::VFSPath& directory);
        void         RenderContentTile(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const ZEngine::Core::VFS::VFSDirEntry& entry);
        void         RenderTreeBrowser();

        // Popup helpers — all paths are native absolute C strings
        void         RenderContextMenu(ContextMenuType type, const char* targetPath);
        void         RenderPopUpMenu();
        void         HandleCreateFolderPopup(const char* path);
        void         HandleCreateFilePopup(const char* path);
        void         HandleRenameFolderPopup(const char* path);
        void         HandleDeleteFolderPopup(const char* path);
        void         HandleRenameFilePopup(const char* path);
        void         HandleDeleteFilePopup(const char* path);

        void         TriggerScan();

    private:
        ZEngine::Core::Memory::ArenaAllocator        m_local_arena                                = {};

        ZEngine::Core::VFS::IVFSContext*             m_vfs_context                                = nullptr;
        ZEngine::Core::VFS::VFSDirectoryCache*       m_directory_cache                            = nullptr;
        ZEngine::Core::VFS::VFSScanner*              m_scanner                                    = nullptr;

        ZEngine::Core::VFS::VFSPath                  m_assets_vfs_root                            = {};
        ZEngine::Core::VFS::VFSPath                  m_current_vfs_dir                            = {};
        char                                         m_root_label[MAX_FILE_PATH_COUNT]            = "";
        char                                         m_workspace_root[MAX_FILE_PATH_COUNT]        = "";

        PopupType                                    m_active_popup                               = PopupType::None;
        char                                         m_popup_target_path[MAX_FILE_PATH_COUNT]     = {};
        // Input buffers for popup modals — promoted from static locals so each
        // instance has its own state and opening a new item resets the buffer.
        char                                         m_popup_new_file_name[MAX_FILE_PATH_COUNT]   = "NewFile.txt";
        char                                         m_popup_new_folder_name[MAX_FILE_PATH_COUNT] = "New Folder";
        char                                         m_popup_rename_name[MAX_FILE_PATH_COUNT]     = {};
        bool                                         m_popup_rename_initialized                   = false;

        // Search result cache — rebuilt only when m_search_buffer changes.
        char                                         m_last_search[MAX_FILE_PATH_COUNT]           = {};
        std::vector<ZEngine::Core::VFS::VFSDirEntry> m_search_results;

        bool                                         m_right_pane_hovered                 = false;
        static constexpr float                       m_thumbnail_size                     = 80.0f;
        char                                         m_search_buffer[MAX_FILE_PATH_COUNT] = "";
    };
} // namespace Tetragrama::Components
