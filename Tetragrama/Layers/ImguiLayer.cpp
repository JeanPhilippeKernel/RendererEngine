#include <pch.h>
#include <Components/DemoUIComponent.h>
#include <Components/DockspaceUIComponent.h>
#include <Components/HierarchyViewUIComponent.h>
#include <Components/InspectorViewUIComponent.h>
#include <Components/LogUIComponent.h>
#include <Components/ProjectViewUIComponent.h>
#include <Components/SceneViewportUIComponent.h>
#include <ImguiLayer.h>
#include <MessageToken.h>
#include <Messenger.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/ImGUIRenderer.h>
#include <fmt/format.h>
#include <imgui.h>

using namespace ZEngine;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Windows::Events;
using namespace ZEngine::Helpers;
using namespace Tetragrama::Messengers;

namespace Tetragrama::Layers
{
    ImguiLayer::~ImguiLayer() {}

    void ImguiLayer::Initialize()
    {
        NodeHierarchies.reserve(10);

        auto dockspace_cmp           = CreateRef<Components::DockspaceUIComponent>();
        auto scene_cmp               = CreateRef<Components::SceneViewportUIComponent>();
        auto editor_log_cmp          = CreateRef<Components::LogUIComponent>();
        auto demo_cmp                = CreateRef<Components::DemoUIComponent>();
        auto project_view_cmp        = CreateRef<Components::ProjectViewUIComponent>();
        auto inspector_view_cmp      = CreateRef<Components::InspectorViewUIComponent>();
        auto hierarchy_view_cmp      = CreateRef<Components::HierarchyViewUIComponent>();

        dockspace_cmp->Children      = {scene_cmp.get(), editor_log_cmp.get(), demo_cmp.get(), project_view_cmp.get(), inspector_view_cmp.get(), hierarchy_view_cmp.get()};
        dockspace_cmp->ChildrenCount = dockspace_cmp->Children.size();

        AddUIComponent(dockspace_cmp.get(), -1, 0);

        /*
         *  Register Scene Component
         */
        IMessenger::Register<Components::UIComponent, GenericMessage<bool>>(scene_cmp.get(), EDITOR_COMPONENT_SCENEVIEWPORT_FOCUSED, [=](void* const message) -> std::future<void> {
            auto message_ptr = reinterpret_cast<GenericMessage<bool>*>(message);
            return scene_cmp->SceneViewportFocusedMessageHandlerAsync(*message_ptr);
        });

        IMessenger::Register<Components::UIComponent, ArrayValueMessage<int, 2>>(scene_cmp.get(), EDITOR_COMPONENT_SCENEVIEWPORT_CLICKED, [=](void* const message) -> std::future<void> {
            auto message_ptr = reinterpret_cast<ArrayValueMessage<int, 2>*>(message);
            return scene_cmp->SceneViewportClickedMessageHandlerAsync(*message_ptr);
        });
        /*
         *  Register Hierarchy Component
         */
        IMessenger::Register<Components::UIComponent, GenericMessage<Ref<Controllers::EditorCameraController>>>(hierarchy_view_cmp.get(), EDITOR_RENDER_LAYER_CAMERA_CONTROLLER_AVAILABLE, [=](void* const message) -> std::future<void> {
            auto message_ptr = reinterpret_cast<GenericMessage<Ref<Controllers::EditorCameraController>>*>(message);
            return hierarchy_view_cmp->EditorCameraAvailableMessageHandlerAsync(*message_ptr);
        });
        /*
         *  Register Inspector Component
         */
        IMessenger::Register<Components::UIComponent, GenericMessage<bool>>(inspector_view_cmp.get(), EDITOR_COMPONENT_INSPECTORVIEW_REQUEST_RESUME_OR_PAUSE_RENDER, [=](void* const message) -> std::future<void> {
            auto message_ptr = reinterpret_cast<GenericMessage<bool>*>(message);
            return inspector_view_cmp->RequestStartOrPauseRenderMessageHandlerAsync(*message_ptr);
        });

        IMessenger::Register<Components::UIComponent, GenericMessage<ZEngine::Rendering::Scenes::SceneEntity>>(inspector_view_cmp.get(), EDITOR_COMPONENT_HIERARCHYVIEW_NODE_SELECTED, [=](void* const message) -> std::future<void> {
            auto message_ptr = reinterpret_cast<GenericMessage<ZEngine::Rendering::Scenes::SceneEntity>*>(message);
            return inspector_view_cmp->SceneEntitySelectedMessageHandlerAsync(*message_ptr);
        });

        IMessenger::Register<Components::UIComponent, EmptyMessage>(inspector_view_cmp.get(), EDITOR_COMPONENT_HIERARCHYVIEW_NODE_UNSELECTED, [=](void* const message) -> std::future<void> {
            auto message_ptr = reinterpret_cast<EmptyMessage*>(message);
            return inspector_view_cmp->SceneEntityUnSelectedMessageHandlerAsync(*message_ptr);
        });

        IMessenger::Register<Components::UIComponent, EmptyMessage>(inspector_view_cmp.get(), EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED, [=](void* const message) -> std::future<void> {
            auto message_ptr = reinterpret_cast<EmptyMessage*>(message);
            return inspector_view_cmp->SceneEntityDeletedMessageHandlerAsync(*message_ptr);
        });

        IMessenger::Register<Components::UIComponent, GenericMessage<Ref<ZEngine::Rendering::Scenes::GraphicScene>>>(inspector_view_cmp.get(), EDITOR_RENDER_LAYER_SCENE_AVAILABLE, [=](void* const message) -> std::future<void> {
            auto message_ptr = reinterpret_cast<GenericMessage<Ref<ZEngine::Rendering::Scenes::GraphicScene>>*>(message);
            return inspector_view_cmp->SceneAvailableMessageHandlerAsync(*message_ptr);
        });
    }

    void ImguiLayer::Deinitialize()
    {
        NodeHierarchies.clear();
        NodeHierarchies.shrink_to_fit();
        NodeUIComponents.clear();
    }

    bool ImguiLayer::OnEvent(Core::CoreEvent& event)
    {
        Core::EventDispatcher event_dispatcher(event);

        event_dispatcher.Dispatch<KeyPressedEvent>(std::bind(&ImguiLayer::OnKeyPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<KeyReleasedEvent>(std::bind(&ImguiLayer::OnKeyReleased, this, std::placeholders::_1));

        event_dispatcher.Dispatch<MouseButtonPressedEvent>(std::bind(&ImguiLayer::OnMouseButtonPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<MouseButtonReleasedEvent>(std::bind(&ImguiLayer::OnMouseButtonReleased, this, std::placeholders::_1));
        event_dispatcher.Dispatch<MouseButtonMovedEvent>(std::bind(&ImguiLayer::OnMouseButtonMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<MouseButtonWheelEvent>(std::bind(&ImguiLayer::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<TextInputEvent>(std::bind(&ImguiLayer::OnTextInputRaised, this, std::placeholders::_1));

        event_dispatcher.Dispatch<WindowClosedEvent>(std::bind(&ImguiLayer::OnWindowClosed, this, std::placeholders::_1));

        return false;
    }

    void ImguiLayer::Update(Core::TimeStep dt)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (dt > 0.0f)
        {
            io.DeltaTime = dt;
        }

        if (!NodeToRender.empty())
        {
            NodeToRender.clear();
        }

        std::vector<int> roots    = {};
        std::vector<int> children = {};
        std::vector<int> siblings = {};

        uint32_t         i        = 0;
        for (auto& node : NodeHierarchies)
        {
            if (node.Parent == -1)
            {
                auto& cmp = NodeUIComponents[i];
                if (cmp->IsVisible)
                {
                    roots.emplace_back(i);
                    if (node.FirstChild != -1)
                    {
                        auto& fc = NodeUIComponents[node.FirstChild];
                        if (fc->IsVisible)
                        {
                            children.emplace_back(node.FirstChild);
                        }
                    }
                }
            }
            ++i;
        }

        for (auto ch : children)
        {
            for (auto sibling = NodeHierarchies[ch].RightSibling; sibling != -1; sibling = NodeHierarchies[sibling].RightSibling)
            {
                siblings.emplace_back(sibling);
            }
        }

        int size = roots.size() + children.size() + siblings.size();
        if (NodeToRender.capacity() < size)
        {
            NodeToRender.reserve(size);
        }

        for (auto r : roots)
        {
            NodeToRender.emplace_back(r);
        }

        for (auto c : children)
        {
            NodeToRender.emplace_back(c);
        }

        for (auto s : siblings)
        {
            NodeToRender.emplace_back(s);
        }

        for (auto i : NodeToRender)
        {
            auto& cmp = NodeUIComponents[i];
            cmp->Update(dt);
        }
    }

    int ImguiLayer::AddNode(Components::UIComponent* cmp, int parent, int depth)
    {
        if ((!cmp) || (depth < 0))
        {
            return -1;
        }

        auto node = NodeHierarchies.size();
        NodeHierarchies.emplace_back(NodeHierarchy{.Parent = parent});
        if (parent > -1)
        {
            int first = NodeHierarchies[parent].FirstChild;
            if (first == -1)
            {
                NodeHierarchies[parent].FirstChild = node;
            }
            else
            {
                int sibling = NodeHierarchies[first].RightSibling;
                if (sibling == -1)
                {
                    NodeHierarchies[first].RightSibling = node;
                }
                else
                {
                    for (sibling = first; NodeHierarchies[sibling].RightSibling != -1; sibling = NodeHierarchies[sibling].RightSibling)
                    {
                    }
                    NodeHierarchies[sibling].RightSibling = node;
                }
            }
        }
        NodeHierarchies[node].DepthLevel = depth;
        return node;
    }

    void ImguiLayer::AddUIComponent(Components::UIComponent* cmp, int parent, int depth)
    {
        if (!cmp)
        {
            return;
        }

        auto node_id              = AddNode(cmp, parent, depth);
        cmp->ParentLayer          = this;
        NodeUIComponents[node_id] = cmp;
        for (int i = 0; i < cmp->ChildrenCount; ++i)
        {
            AddUIComponent(cmp->Children[i], node_id, (depth + 1));
        }
    }

    bool ImguiLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.KeysDown[(int) e.GetKeyCode()] = true;
        return false;
    }

    bool ImguiLayer::OnKeyReleased(KeyReleasedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.KeysDown[(int) e.GetKeyCode()] = false;
        return false;
    }

    bool ImguiLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        ImGuiIO& io                            = ImGui::GetIO();
        io.MouseDown[(uint32_t) e.GetButton()] = true;
        return false;
    }

    bool ImguiLayer::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.MouseDown[(int) e.GetButton()] = false;
        return false;
    }

    bool ImguiLayer::OnMouseButtonMoved(MouseButtonMovedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(float(e.GetPosX()), float(e.GetPosY()));
        return false;
    }

    bool ImguiLayer::OnMouseButtonWheelMoved(MouseButtonWheelEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (e.GetOffetX() > 0)
        {
            io.MouseWheelH += 1;
        }
        else if (e.GetOffetX() < 0)
        {
            io.MouseWheelH -= 1;
        }
        else if (e.GetOffetY() > 0)
        {
            io.MouseWheel += 1;
        }
        else if (e.GetOffetY() < 0)
        {
            io.MouseWheel -= 1;
        }
        return false;
    }

    bool ImguiLayer::OnTextInputRaised(TextInputEvent& event)
    {
        ImGuiIO& io = ImGui::GetIO();
        for (unsigned char c : event.GetText())
        {
            io.AddInputCharacter(c);
        }
        return false;
    }

    bool ImguiLayer::OnWindowClosed(WindowClosedEvent& event)
    {
        Core::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<WindowClosedEvent>(std::bind(&ZEngine::Windows::CoreWindow::OnWindowClosed, GetAttachedWindow().get(), std::placeholders::_1));
        return true;
    }

    bool ImguiLayer::OnWindowResized(WindowResizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowMinimized(WindowMinimizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowMaximized(WindowMaximizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowRestored(WindowRestoredEvent&)
    {
        return false;
    }

    void ImguiLayer::Render(Rendering::Renderers::GraphicRenderer* const renderer, Rendering::Buffers::CommandBuffer* const command_buffer)
    {
        for (auto& id : NodeToRender)
        {
            auto& cmp = NodeUIComponents[id];
            cmp->Render(renderer, command_buffer);
        }
    }
} // namespace Tetragrama::Layers
