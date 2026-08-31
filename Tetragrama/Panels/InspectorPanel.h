#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Panels
{
    // Inspector panel
    //
    // Reflection-driven: iterates ComponentReflectionRegistry::ForEach for the
    // selected actor, draws every registered component with ZUI widgets.
    // Matches develop InspectorViewUIComponent architecture translated to ZUI.

    /// @brief Reflection-driven component inspector panel.  For the selected actor
    ///        it iterates ComponentReflectionRegistry and renders every registered
    ///        component section with collapsible headers, field widgets, and a
    ///        category pill filter row.
    struct InspectorPanel : ZEngine::UI::ZUIPanelView
    {
        InspectorPanel()
        {
            Title = "Inspector";
        }

        Tetragrama::Layers::ZUILayer* m_layer        = nullptr;
        char                          m_search[128]  = {};
        char                          m_category[64] = "All"; // active category pill ("All" = no filter)
        bool                          m_sec_open[64] = {};    // per TypeID, initialized to true
        bool                          m_sec_init     = false;

        /// @brief Builds the inspector UI for the currently selected actor.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void                          BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;
    };
} // namespace Tetragrama::Panels
