#pragma once
#include <ImguiLayer.h>
#include <ZEngine/Core/IRenderable.h>
#include <ZEngine/Core/IUpdatable.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <string>

namespace Tetragrama::Layers
{
    class ImguiLayer;
}

namespace Tetragrama::Components
{
    struct UIComponent : public ZEngine::Core::IRenderable, public ZEngine::Core::IUpdatable, public ZEngine::Helpers::RefCounted
    {
        UIComponent() = default;
        UIComponent(Layers::ImguiLayer* parent, std::string_view name, bool visibility, bool can_be_closed) : ParentLayer(parent), Name(name.data()), IsVisible(visibility), CanBeClosed(can_be_closed) {}
        virtual ~UIComponent()                        = default;

        bool                            IsVisible     = true;
        bool                            CanBeClosed   = false;
        uint32_t                        ChildrenCount = 0;
        Tetragrama::Layers::ImguiLayer* ParentLayer   = nullptr;
        std::string                     Name          = "";
        std::vector<UIComponent*>       Children      = {};
    };
} // namespace Tetragrama::Components
