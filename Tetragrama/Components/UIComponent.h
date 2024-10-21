#pragma once
#include <Events/UIComponentEvent.h>
#include <ImguiLayer.h>
#include <ZEngine/Core/IRenderable.h>
#include <ZEngine/Core/IUpdatable.h>
#include <ZEngine/ZEngineDef.h>
#include <string>
#include <vector>

namespace Tetragrama::Layers
{
    class ImguiLayer;
}

namespace Tetragrama::Components
{
    class UIComponent : public ZEngine::Core::IRenderable, public ZEngine::Core::IUpdatable, public ZEngine::Helpers::RefCounted
    {

    public:
        UIComponent() = default;
        UIComponent(std::string_view name, bool visibility, bool can_be_closed);
        UIComponent(const ZEngine::Ref<Tetragrama::Layers::ImguiLayer>& layer, std::string_view name, bool visibility, bool can_be_closed);
        virtual ~UIComponent() = default;

        void SetName(std::string_view name);
        void SetVisibility(bool visibility);

        std::string_view GetName() const;
        bool             GetVisibility() const;

        void SetParentLayer(const ZEngine::Ref<Tetragrama::Layers::ImguiLayer>& layer);
        void SetParentUI(const ZEngine::Ref<UIComponent>& item);

        bool HasParentLayer() const;
        bool HasParentUI() const;

    protected:
        bool                                             m_visibility{false};
        bool                                             m_can_be_closed{false};
        bool                                             m_is_allowed_to_render{true};
        std::string                                      m_name;
        ZEngine::WeakRef<Tetragrama::Layers::ImguiLayer> m_parent_layer;
        ZEngine::WeakRef<UIComponent>                    m_parent_ui;
    };
} // namespace Tetragrama::Components
