#include <Components/UIComponent.h>

namespace ZEngine::Components::UI
{
    UIComponent::UIComponent(std::string_view name, bool visibility)
        :m_name(name.data()), m_visibility(visibility)
    {
    }

    void UIComponent::SetName(std::string_view name) {
        std::string_view current(m_name);
        if(current.compare(name) != 0) {
            m_name = name.data();
        }
    }

    void UIComponent::SetVisibility(bool visibility) {
        m_visibility = visibility;
    }

    std::string_view UIComponent::GetName() const {
        return m_name;
    }

    bool UIComponent::GetVisibility() const { 
        return m_visibility;
    }
}