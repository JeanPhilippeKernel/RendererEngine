#pragma once
#include <ZEngine/Core/IRenderable.h>
#include <ZEngine/Core/IUpdatable.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/ZEngineDef.h>

namespace Tetragrama::Layers
{
    struct ZUILayer;
}

namespace Tetragrama::Components
{
    struct ZUIComponent : public ZEngine::Core::IRenderable, public ZEngine::Core::IUpdatable
    {
        virtual ~ZUIComponent() = default;

        virtual void                  Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "", bool visibility = true) {}

        void                          Update(ZEngine::Core::TimeStep) override {}

        void                          Render(ZEngine::Rendering::Renderers::GraphicRenderer* const, ZEngine::Hardwares::CommandBuffer* const) override {}

        // Called each frame inside ZUILayer::BuildUI — build the box tree for this component
        virtual void                  BuildUI(ZEngine::UI::ZUIContext* ctx) {}

        cstring                       Name        = nullptr;
        bool                          Visible     = true;
        Tetragrama::Layers::ZUILayer* ParentLayer = nullptr;

        // Layout region — set by ZUIDockspaceComponent before BuildUI is called.
        // When RegionW > 0 the component uses these values for its panel position/size.
        // When RegionW == 0 the component falls back to its own hardcoded defaults.
        float                         RegionX     = 0.f;
        float                         RegionY     = 0.f;
        float                         RegionW     = 0.f;
        float                         RegionH     = 0.f;
        // When true the dockspace skips assigning this panel's region — the panel
        // controls its own position via ZUIPanelDragHeader. Double-click to snap back.
        bool                          Detached    = false;
    };
    ZDEFINE_PTR(ZUIComponent);
} // namespace Tetragrama::Components
