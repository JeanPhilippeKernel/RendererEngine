#include <pch.h>
#include <UILayer.h>
#include <Messenger.h>
#include <MessageToken.h>

using namespace Tetragrama::Messengers;

namespace Tetragrama::Layers
{
    UILayer::~UILayer() {}

    void UILayer::Initialize()
    {
        ImguiLayer::Initialize();

        m_dockspace_component      = ZEngine::CreateRef<Components::DockspaceUIComponent>();
        m_scene_component          = ZEngine::CreateRef<Components::SceneViewportUIComponent>();
        m_editor_log_component     = ZEngine::CreateRef<Components::LogUIComponent>();
        m_demo_component           = ZEngine::CreateRef<Components::DemoUIComponent>();
        m_project_view_component   = ZEngine::CreateRef<Components::ProjectViewUIComponent>();
        m_inspector_view_component = ZEngine::CreateRef<Components::InspectorViewUIComponent>();
        m_hierarchy_view_component = ZEngine::CreateRef<Components::HierarchyViewUIComponent>();

        AddUIComponent(m_dockspace_component);
        AddUIComponent(m_demo_component);
        AddUIComponent(m_scene_component);
        AddUIComponent(m_editor_log_component);
        AddUIComponent(m_project_view_component);
        AddUIComponent(m_inspector_view_component);
        AddUIComponent(m_hierarchy_view_component);
        /*
         *  Register Dockspace Component
         */
        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            GenericMessage<ZEngine::Event::WindowClosedEvent>,
            EDITOR_COMPONENT_DOCKSPACE_REQUEST_EXIT,
            m_dockspace_component.get(),
            return m_dockspace_component->RequestExitMessageHandlerAsync(*message_ptr))
        /*
         *  Register Scene Component
         */
        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            GenericMessage<bool>,
            EDITOR_COMPONENT_SCENEVIEWPORT_FOCUSED,
            m_scene_component.get(),
            return m_scene_component->SceneViewportFocusedMessageHandler(*message_ptr))

        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            GenericMessage<bool>,
            EDITOR_COMPONENT_SCENEVIEWPORT_UNFOCUSED,
            m_scene_component.get(),
            return m_scene_component->SceneViewportUnfocusedMessageHandler(*message_ptr))

        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            SINGLE_ARG(ArrayValueMessage<int, 2>),
            EDITOR_COMPONENT_SCENEVIEWPORT_CLICKED,
            m_scene_component.get(),
            return m_scene_component->SceneViewportClickedMessageHandler(*message_ptr))
        /*
         *  Register Hierarchy Component
         */
        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            GenericMessage<ZEngine::Ref<EditorCameraController>>,
            EDITOR_RENDER_LAYER_CAMERA_CONTROLLER_AVAILABLE,
            m_hierarchy_view_component.get(),
            return m_hierarchy_view_component->EditorCameraAvailableMessageHandlerAsync(*message_ptr))
        /*
         *  Register Inspector Component
         */
        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            GenericMessage<bool>,
            EDITOR_COMPONENT_INSPECTORVIEW_REQUEST_RESUME_OR_PAUSE_RENDER,
            m_inspector_view_component.get(),
            return m_inspector_view_component->RequestStartOrPauseRenderMessageHandler(*message_ptr))

        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            PointerValueMessage<ZEngine::Rendering::Entities::GraphicSceneEntity>,
            EDITOR_COMPONENT_HIERARCHYVIEW_NODE_SELECTED,
            m_inspector_view_component.get(),
            return m_inspector_view_component->SceneEntitySelectedMessageHandler(*message_ptr));

        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            EmptyMessage,
            EDITOR_COMPONENT_HIERARCHYVIEW_NODE_UNSELECTED,
            m_inspector_view_component.get(),
            return m_inspector_view_component->SceneEntityUnSelectedMessageHandler(*message_ptr));

        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            EmptyMessage,
            EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED,
            m_inspector_view_component.get(),
            return m_inspector_view_component->SceneEntityDeletedMessageHandler(*message_ptr));

        MESSENGER_REGISTER(
            ZEngine::Components::UI::UIComponent,
            GenericMessage<ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>>,
            EDITOR_RENDER_LAYER_SCENE_AVAILABLE,
            m_inspector_view_component.get(),
            return m_inspector_view_component->SceneAvailableMessageHandler(*message_ptr));
    }
} // namespace Tetragrama::Layers
