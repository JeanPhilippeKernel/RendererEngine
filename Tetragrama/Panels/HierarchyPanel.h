#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Panels
{
    // ── Hierarchy panel ───────────────────────────────────────────────────────
    //
    // Real ECS actor tree with DFS traversal, inline rename, context menu,
    // drag-and-drop reparenting, search filter, and two-column layout.
    // VS Code chevrons (∨/›) via ZUI_DrawTriArrow + UserData 2.f/3.f.
    //
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
        bool                          m_rename_started           = false;
        uint64_t                      m_rename_fkey              = 0;

        // Collapsed-entity set (linear scan, sufficient for small-medium scenes)
        static constexpr int          kMaxCollapsed              = 256;
        ZEngine::ECS::EntityID        m_collapsed[kMaxCollapsed] = {};
        int                           m_ncollapsed               = 0;
        bool                          m_root_open                = true;

        // Drag-reparent: panel owns drag state because ZUIInteractionPass clears
        // DragPayloadLen=0 on mouse release (before BuildContent sees DragDropFired=true)
        ZEngine::ECS::ActorHandle     m_dragging_actor           = {};
        char                          m_drag_label[128]          = {};

        // Panel-level double-click tracking (ZUISignalFromBox is called twice per row,
        // so we track in the panel to avoid double-firing)
        uint32_t                      m_last_click_idx           = UINT32_MAX;
        uint32_t                      m_last_click_gen           = 0;
        float                         m_last_click_time          = -1.f;

        bool IsCollapsed(ZEngine::ECS::EntityID id) const;
        void ToggleCollapsed(ZEngine::ECS::EntityID id);

        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;
    };
} // namespace Tetragrama::Panels
