#include <pch.h>
#include <Components/UIComponent.h>

namespace ZEngine::Components::UI {
    UIComponent::UIComponent(std::string_view name, bool visibility, bool can_be_closed) : m_name(name.data()), m_visibility(visibility), m_can_be_closed(can_be_closed) {}

    UIComponent::UIComponent(const Ref<Layers::ImguiLayer>& layer, std::string_view name, bool visibility, bool can_be_closed)
        : m_parent_layer(layer), m_name(name.data()), m_visibility(visibility), m_can_be_closed(can_be_closed) {}

    void UIComponent::SetName(std::string_view name) {
        std::string_view current(m_name);
        if (current.compare(name) != 0) {
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

    void UIComponent::SetParentLayer(const Ref<Layers::ImguiLayer>& layer) {
        m_parent_layer = layer;
        if (HasChildren()) {
            std::for_each(std::begin(m_children), std::end(m_children), [layer](Ref<UIComponent>& child) { child->m_parent_layer = layer; });
        }
    }

    bool UIComponent::HasParentLayer() const {
        return m_parent_layer.expired() == false;
    }

    void UIComponent::SetParentUI(const Ref<UIComponent>& item) {
        m_parent_ui = item;
    }

    bool UIComponent::HasParentUI() const {
        return m_parent_ui.expired() == false;
    }

    bool UIComponent::HasChildren() const {
        return m_children.size() > 0;
    }

    void UIComponent::AddChild(Ref<UIComponent>& item) {
        if (!item->HasParentUI()) {
            item->SetParentUI(shared_from_this());
        }
        m_children.push_back(item);
    }

    void UIComponent::AddChild(Ref<UIComponent>&& item) {
        if (!item->HasParentUI()) {
            item->SetParentUI(shared_from_this());
        }
        m_children.push_back(std::move(item));
    }

    void UIComponent::AddChild(const Ref<UIComponent>& item) {
        m_children.push_back(item);
        auto last = std::prev(std::end(m_children));

        if (!(last->get()->HasParentUI())) {
            last->get()->SetParentUI(shared_from_this());
        }
    }

    void UIComponent::AddChildren(std::vector<Ref<UIComponent>>& items) {
        std::for_each(std::begin(items), std::end(items), [this](Ref<UIComponent>& component) { this->AddChild(component); });
    }

    void UIComponent::AddChildren(std::vector<Ref<UIComponent>>&& items) {
        std::for_each(std::begin(items), std::end(items), [this](Ref<UIComponent>& component) { this->AddChild(std::move(component)); });
    }
} // namespace ZEngine::Components::UI
