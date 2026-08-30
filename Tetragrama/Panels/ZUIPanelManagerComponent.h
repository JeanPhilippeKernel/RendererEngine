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

    struct ZUIPanelManagerComponent : public Tetragrama::Components::ZUIComponent
    {
        ZEngine::UI::ZUIPanelManager Manager;

        HierarchyPanel        hierarchy;
        ViewportPanel         viewport;
        InspectorPanel        inspector;
        ConsolePanel          output;
        ProjectViewPanel      project;
        MemoryProfilerPanel   profiler;
        AssetImporterPanel    importer;

        void Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "PanelManager", bool visibility = true) override;
        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;
    };

} // namespace Tetragrama::Panels
