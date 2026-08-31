#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>

namespace Tetragrama::Components
{
    /// @brief Legacy floating inspector panel (pre-ZUIPanelManager path).
    ///        Renders a draggable floating window with Transform, Mesh, and Light
    ///        sections for the selected actor.  Superseded by InspectorPanel but
    ///        kept as the legacy code path driven by ZUIDockspaceComponent.
    class ZUIInspectorViewComponent : public ZUIComponent
    {
    public:
        ZUIInspectorViewComponent()           = default;
        ~ZUIInspectorViewComponent() override = default;

        /// @brief Stores parent/name/visibility.
        /// @param parent     Owning ZUI layer.
        /// @param name       Component name.
        /// @param visibility Initial visibility.
        void Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "Inspector", bool visibility = true) override;

        /// @brief Builds the floating inspector panel for the currently selected actor.
        /// @param ctx ZUI context for the current frame.
        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

    private:
        bool m_transform_open = true;
        bool m_mesh_open      = true;
        bool m_light_open     = true;
    };
    ZDEFINE_PTR(ZUIInspectorViewComponent);
} // namespace Tetragrama::Components
