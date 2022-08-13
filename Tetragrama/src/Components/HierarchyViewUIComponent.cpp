#include <pch.h>
#include <HierarchyViewUIComponent.h>
#include <Messenger.h>
#include <MessageToken.h>

using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Entities;

namespace Tetragrama::Components {
    HierarchyViewUIComponent::HierarchyViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {
        m_node_flag = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    }

    HierarchyViewUIComponent::~HierarchyViewUIComponent() {}

    void HierarchyViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void HierarchyViewUIComponent::SceneAvailableMessageHandler(Messengers::GenericMessage<ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>>& message) {
        m_active_scene = message.GetValue();
    }

    void HierarchyViewUIComponent::RequestStartOrPauseRenderMessageHandler(Messengers::GenericMessage<bool>& message) {
        m_is_allowed_to_render = message.GetValue();
    }

    bool HierarchyViewUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) {
        return false;
    }

    void HierarchyViewUIComponent::Render() {
        CHECK_IF_ALLOWED_TO_RENDER()

        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        if (!m_active_scene.expired()) {
            auto scene_ptr = m_active_scene.lock();
            scene_ptr->GetRegistry()->view<NameComponent>().each([&scene_ptr, this](entt::entity handle, NameComponent&) {
                ZEngine::Rendering::Entities::GraphicSceneEntity scene_entity(handle, scene_ptr->GetRegistry());
                RenderEntityNode(std::move(scene_entity));
            });
        }

        if (ImGui::BeginPopupContextWindow(0, 1, false)) {
            if (ImGui::MenuItem("Create Empty")) {
                if (auto scene_ptr = m_active_scene.lock()) {
                    scene_ptr->CreateEntity();
                }
            }
            ImGui::EndPopup();
        }

        // 0 means left buttom
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
            m_selected_scene_entity = GraphicSceneEntity(entt::null, nullptr);
            Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
                EDITOR_COMPONENT_HIERARCHYVIEW_NODE_UNSELECTED, Messengers::EmptyMessage{});
        }
        ImGui::End();
    }

    void HierarchyViewUIComponent::RenderEntityNode(ZEngine::Rendering::Entities::GraphicSceneEntity&& scene_entity) {
        auto component    = scene_entity.GetComponent<NameComponent>();
        auto seleted_flag = (m_selected_scene_entity == scene_entity) ? ImGuiTreeNodeFlags_Selected : 0;
        m_is_node_opened  = ImGui::TreeNodeEx(reinterpret_cast<void*>((uint32_t) scene_entity), (m_node_flag | seleted_flag), "%s", component.Name.c_str());

        if (ImGui::IsItemClicked()) {
            m_selected_scene_entity = std::move(scene_entity);
            Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::PointerValueMessage<GraphicSceneEntity>>(
                EDITOR_COMPONENT_HIERARCHYVIEW_NODE_SELECTED, Messengers::PointerValueMessage<GraphicSceneEntity>{&m_selected_scene_entity});
        }

        bool request_entity_removal = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                if (m_selected_scene_entity == scene_entity) {
                    Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
                        EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED, Messengers::EmptyMessage{});

                    m_selected_scene_entity = {entt::null, nullptr};
                }
                request_entity_removal = true;
            }
            ImGui::EndPopup();
        }

        if (m_is_node_opened) {
            ImGui::TreePop();
        }

        if (request_entity_removal) {
            if (auto scene_ptr = m_active_scene.lock()) {
                scene_ptr->RemoveEntity(scene_entity);
            }
        }
    }
} // namespace Tetragrama::Components
