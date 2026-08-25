#pragma once
#include <ZEngine/Core/IRenderable.h>
#include <ZEngine/Core/IUpdatable.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/ZEngineDef.h>

namespace Tetragrama::Layers { struct ZUILayer; }

namespace Tetragrama::Components
{
    struct ZUIComponent : public ZEngine::Core::IRenderable, public ZEngine::Core::IUpdatable
    {
        virtual ~ZUIComponent() = default;

        virtual void Initialize(Tetragrama::Layers::ZUILayer* parent,
                                cstring name       = "",
                                bool    visibility = true) {}

        void Update(ZEngine::Core::TimeStep) override {}

        void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const,
                    ZEngine::Hardwares::CommandBuffer* const) override {}

        // Called each frame inside ZUILayer::BuildUI — build the box tree for this component
        virtual void BuildUI(ZEngine::UI::ZUIContext* ctx) {}

        cstring                      Name        = nullptr;
        bool                         Visible     = true;
        Tetragrama::Layers::ZUILayer* ParentLayer = nullptr;
    };
    ZDEFINE_PTR(ZUIComponent);
} // namespace Tetragrama::Components
