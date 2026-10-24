#pragma once
#include <Tetragrama/EditorTarget.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace ZEngine
{
    struct EngineContext;
}
namespace Tetragrama
{
    struct Editor;
    struct EditorScene;
} // namespace Tetragrama

namespace Tetragrama::Panels
{
    // Hierarchy panel
    //
    // Real ECS actor tree with DFS traversal, inline rename, context menu,
    // drag-and-drop reparenting, search filter, and two-column layout.
    // VS Code chevrons (∨/›) via ZUI_DrawTriArrow + UserData 2.f/3.f.

    /// @brief ECS entity outliner panel. Renders actor-backed and pure entities as a
    ///        two-column DataTable (Actor Label, Type) with per-frame DFS
    ///        traversal, search filtering, inline rename, drag-reparent, and a
    ///        deferred-mutation delete path.
    struct HierarchyPanel : ZEngine::UI::ZUIPanelView
    {
        HierarchyPanel();

        // Set by ZUIPanelManagerComponent::Initialize before first BuildContent call
        Tetragrama::Layers::ZUILayer* m_layer                    = nullptr;

        // Search bar buffer
        char                          m_search[128]              = {};

        // Inline rename state
        ZEngine::ECS::EntityID        m_rename_id                = {};
        char                          m_rename_buf[128]          = {};
        char                          m_rename_original[128]     = {};
        EntityTarget                  m_rename_target            = {};
        bool                          m_rename_started           = false;
        uint64_t                      m_rename_fkey              = 0;

        // Collapsed-entity set (linear scan, sufficient for small-medium scenes)
        static constexpr int          kMaxCollapsed              = 256;
        ZEngine::ECS::EntityID        m_collapsed[kMaxCollapsed] = {};
        int                           m_ncollapsed               = 0;
        bool                          m_root_open                = true;
        int                           m_sort_col                 = 0; ///< 0 = Actor Label, 1 = Type (kept here: the data table reloads its own sort state every frame)
        bool                          m_sort_asc                 = true;

        // UUID/epoch identity plus the complete runtime ID; never an actor-only drag.
        EntityTarget                  m_dragging_target          = {};
        char                          m_drag_label[128]          = {};
        const ZEngine::ECS::Scene*    m_view_scene               = nullptr;
        uint64_t                      m_view_epoch               = 0;
        EntityTarget                  m_selection_anchor         = {};

        // Panel-level double-click tracking (ZUISignalFromBox is called twice per row,
        // so we track in the panel to avoid double-firing)
        uint64_t                      m_last_click_idx           = UINT64_MAX;
        uint64_t                      m_last_click_gen           = 0;
        double                        m_last_click_time          = -1.0;

        /// @brief Returns true if @p id is in the collapsed set.
        /// @param id Entity to query.
        /// @returns True when collapsed.
        bool                          IsCollapsed(ZEngine::ECS::EntityID id) const;

        /// @brief Toggles the collapsed state of @p id.
        /// @param id Entity whose state to toggle.
        void                          ToggleCollapsed(ZEngine::ECS::EntityID id);

        /// @brief Builds the full hierarchy UI tree for this frame.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void                          BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        friend struct HierarchyPanelTestAccess;
        struct RowState
        {
            ZEngine::ECS::EntityID Entity = {};
            uint64_t               Row = 0, Arrow = 0, Rename = 0;
        };
        // At least 16 boxes per row: this exceeds the rows an 8,192-box UI can
        // emit, while keeping retained state independent of the entity count.
        static constexpr uint32_t                                        MaxRenderedRows = 512;
        ZEngine::Core::Containers::FixedArray<RowState, MaxRenderedRows> m_rows          = {};
        uint32_t                                                         m_row_count     = 0;
        uint64_t                                                         m_rows_epoch    = 0;
        void                                                             BuildHierarchy(ZEngine::UI::ZUIContext* ctx, float width, float height, Editor* app, EditorScene* scene, ZEngine::EngineContext* eng);
    };
} // namespace Tetragrama::Panels
