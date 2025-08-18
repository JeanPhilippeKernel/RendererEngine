#pragma once
#include <ImguiLayer.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/IRenderable.h>
#include <ZEngine/Core/IUpdatable.h>

namespace Tetragrama::Layers
{
    class ImguiLayer;
}

namespace Tetragrama::Components
{
    struct UIComponent : public ZEngine::Core::IRenderable, public ZEngine::Core::IUpdatable
    {
        UIComponent()          = default;
        virtual ~UIComponent() = default;

        virtual void Initialize(Layers::ImguiLayer* parent, cstring name, bool visibility, bool closed)
        {
            ParentLayer = parent;
            Name        = name;
            IsVisible   = visibility;
            CanBeClosed = closed;
        }

        bool                                           IsVisible     = true;
        bool                                           CanBeClosed   = false;
        cstring                                        Name          = "";
        uint32_t                                       ChildrenCount = 0;
        Tetragrama::Layers::ImguiLayer*                ParentLayer   = nullptr;
        ZEngine::Core::Containers::Array<UIComponent*> Children      = {};
    };
    ZDEFINE_PTR(UIComponent);
} // namespace Tetragrama::Components
