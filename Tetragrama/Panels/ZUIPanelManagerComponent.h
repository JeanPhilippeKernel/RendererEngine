#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/EditorPanels.h>
#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Panels
{
    // Default editor layout — matches a standard game engine editor:
    //
    //   ┌─────────┬──────────────────────┬──────────┐
    //   │         │                      │          │
    //   │Hierarchy│      Viewport        │Inspector │
    //   │  (18%)  │       (60%)          │  (22%)   │
    //   │         ├──────────────────────┤          │
    //   │         │        Output        │          │
    //   │         │        (25%)         │          │
    //   └─────────┴──────────────────────┴──────────┘

    /// @brief ZUIComponent that hosts the ZUIPanelManager and owns all editor
    ///        panel instances.  Sets up the default dock-tree split layout and
    ///        delegates per-frame BuildUI to the panel manager.
    struct ZUIPanelManagerComponent : public Tetragrama::Components::ZUIComponent
    {
        ZEngine::UI::ZUIPanelManager Manager = {};

        HierarchyPanel        hierarchy;
        ViewportPanel         viewport;
        InspectorPanel        inspector;
        ConsolePanel          output;
        ProjectViewPanel      project;
        MemoryProfilerPanel   profiler;
        AssetImporterPanel    importer;

        /// @brief Initializes the panel manager, splits the dock tree, and registers
        ///        all panel views.
        /// @param parent     Owning ZUI layer.
        /// @param name       Component name used for identification.
        /// @param visibility Initial visibility.
        void Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "PanelManager", bool visibility = true) override;

        /// @brief Delegates to ZUIPanelManager::BuildUI with menu and status bar heights.
        /// @param ctx ZUI context for the current frame.
        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;
    };

} // namespace Tetragrama::Panels
