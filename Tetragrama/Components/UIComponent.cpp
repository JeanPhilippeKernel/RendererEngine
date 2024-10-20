#include <pch.h>
#include <UIComponent.h>

using namespace ZEngine;

namespace Tetragrama::Components
{
    UIComponent::UIComponent(std::string_view name, bool visibility, bool can_be_closed) : m_name(name.data()), m_visibility(visibility), m_can_be_closed(can_be_closed) {}

    UIComponent::UIComponent(const Ref<Layers::ImguiLayer>& layer, std::string_view name, bool visibility, bool can_be_closed)
        : m_parent_layer(layer), m_name(name.data()), m_visibility(visibility), m_can_be_closed(can_be_closed)
    {
    }

    void UIComponent::SetName(std::string_view name)
    {
        std::string_view current(m_name);
        if (current.compare(name) != 0)
        {
            m_name = name.data();
        }
    }

    void UIComponent::SetVisibility(bool visibility)
    {
        m_visibility = visibility;
    }

    std::string_view UIComponent::GetName() const
    {
        return m_name;
    }

    bool UIComponent::GetVisibility() const
    {
        return m_visibility;
    }

    void UIComponent::SetParentLayer(const Ref<Layers::ImguiLayer>& layer)
    {
        m_parent_layer = layer;
    }

    bool UIComponent::HasParentLayer() const
    {
        return m_parent_layer.expired() == false;
    }

    void UIComponent::SetParentUI(const Ref<UIComponent>& item)
    {
        m_parent_ui = item;
    }

    bool UIComponent::HasParentUI() const
    {
        return m_parent_ui.expired() == false;
    }
} // namespace Tetragrama::Components
