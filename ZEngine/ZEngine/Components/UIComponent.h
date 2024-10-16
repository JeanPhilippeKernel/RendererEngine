#pragma once
#include <Components/UIComponentEvent.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Layers/ImguiLayer.h>
#include <ZEngineDef.h>
#include <string>
#include <vector>

namespace ZEngine::Layers
{
    class ImguiLayer;
}

namespace ZEngine::Components::UI
{
    class UIComponent : public Core::IRenderable, public Core::IUpdatable, public Helpers::RefCounted
    {

    public:
        UIComponent() = default;
        UIComponent(std::string_view name, bool visibility, bool can_be_closed);
        UIComponent(const Ref<Layers::ImguiLayer>& layer, std::string_view name, bool visibility, bool can_be_closed);
        virtual ~UIComponent() = default;

        void SetName(std::string_view name);
        void SetVisibility(bool visibility);

        std::string_view GetName() const;
        bool             GetVisibility() const;

        void SetParentLayer(const Ref<Layers::ImguiLayer>& layer);
        void SetParentUI(const Ref<UIComponent>& item);

        bool HasParentLayer() const;
        bool HasParentUI() const;

    protected:
        bool                        m_visibility{false};
        bool                        m_can_be_closed{false};
        bool                        m_is_allowed_to_render{true};
        std::string                 m_name;
        WeakRef<Layers::ImguiLayer> m_parent_layer;
        WeakRef<UIComponent>        m_parent_ui;
    };
} // namespace ZEngine::Components::UI
