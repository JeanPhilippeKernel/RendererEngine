#include <Tetragrama/Components/DockspaceUIComponent.h>
#include <Tetragrama/Components/HierarchyViewUIComponent.h>
#include <Tetragrama/Components/InspectorViewUIComponent.h>
#include <Tetragrama/Components/ProjectViewUIComponent.h>
#include <Tetragrama/Components/SceneViewportUIComponent.h>
#include <Tetragrama/Components/StatusBarUIComponent.h>
#include <Tetragrama/Layers/ImguiLayer.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/ImGUIRenderer.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <fmt/format.h>
#include <imgui.h>

using namespace ZEngine;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Windows::Events;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Helpers;
using namespace Tetragrama::Messengers;

namespace Tetragrama::Layers
{
    ImguiLayer::~ImguiLayer() {}

    void ImguiLayer::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Applications::GameApplicationPtr app)
    {
        Arena      = arena;
        CurrentApp = app;
        arena->CreateSubArena(ZMega(10), &LocalArena);

        Scanner.Initialize(arena);
        Cache.Initialize(arena);

        NodeHierarchies.init(arena, 10, 0);
        NodeUIComponents.init(arena);
        NodeToRender.init(arena, 10);
        KeyEntries.init(arena, 32);

        KeyEntries[ZENGINE_KEY_SPACE]        = ImGuiKey_Space;
        KeyEntries[ZENGINE_KEY_BACKSPACE]    = ImGuiKey_Backspace;
        KeyEntries[ZENGINE_KEY_RIGHT]        = ImGuiKey_RightArrow;
        KeyEntries[ZENGINE_KEY_LEFT]         = ImGuiKey_LeftArrow;
        KeyEntries[ZENGINE_KEY_DOWN]         = ImGuiKey_DownArrow;
        KeyEntries[ZENGINE_KEY_UP]           = ImGuiKey_UpArrow;
        KeyEntries[ZENGINE_KEY_DELETE]       = ImGuiKey_Delete;
        KeyEntries[ZENGINE_KEY_ENTER]        = ImGuiKey_Enter;
        KeyEntries[ZENGINE_KEY_MOUSE_LEFT]   = ImGuiKey_MouseLeft;
        KeyEntries[ZENGINE_KEY_MOUSE_RIGHT]  = ImGuiKey_MouseRight;
        KeyEntries[ZENGINE_KEY_MOUSE_MIDDLE] = ImGuiKey_MouseMiddle;

        auto dockspace_cmp                   = ZPushStructCtor(arena, Components::DockspaceUIComponent);
        auto scene_cmp                       = ZPushStructCtor(arena, Components::SceneViewportUIComponent);
        auto project_view_cmp                = ZPushStructCtor(arena, Components::ProjectViewUIComponent);
        auto inspector_view_cmp              = ZPushStructCtor(arena, Components::InspectorViewUIComponent);
        auto hierarchy_view_cmp              = ZPushStructCtor(arena, Components::HierarchyViewUIComponent);
        auto status_bar_cmp                  = ZPushStructCtor(arena, Components::StatusBarUIComponent);

        dockspace_cmp->Initialize(this);
        scene_cmp->Initialize(this);
        project_view_cmp->Initialize(this);
        inspector_view_cmp->Initialize(this);
        hierarchy_view_cmp->Initialize(this);
        status_bar_cmp->Initialize(this);

        dockspace_cmp->Children.init(arena, 5);
        dockspace_cmp->Children.push(scene_cmp);
        dockspace_cmp->Children.push(project_view_cmp);
        dockspace_cmp->Children.push(inspector_view_cmp);
        dockspace_cmp->Children.push(hierarchy_view_cmp);
        dockspace_cmp->Children.push(status_bar_cmp);

        dockspace_cmp->ChildrenCount = dockspace_cmp->Children.size();

        AddUIComponent(dockspace_cmp, -1, 0);
        /*
         * Register Dockspace Component
         */
        IMessenger::Register<Components::UIComponent, GenericMessage<std::string>>(dockspace_cmp, EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENSCENE, [=](void* const message) -> std::future<void> {
            auto        message_ptr = reinterpret_cast<GenericMessage<std::string>*>(message);
            const auto& value       = message_ptr->GetValue();
            return dockspace_cmp->OnOpenSceneRequestAsync(value.c_str());
        });

        IMessenger::Register<Components::UIComponent, GenericMessage<std::string>>(dockspace_cmp, EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENMESH, [=](void* const message) -> std::future<void> {
            auto        message_ptr = reinterpret_cast<GenericMessage<std::string>*>(message);
            const auto& value       = message_ptr->GetValue();
            return dockspace_cmp->OnOpenMeshRequestAsync(value.c_str());
        });
    }

    void ImguiLayer::Deinitialize()
    {
        NodeHierarchies.clear();
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

        auto       temp_arena = ZGetScratch(&LocalArena);

        Array<int> roots      = {};
        Array<int> children   = {};
        Array<int> siblings   = {};

        roots.init(temp_arena.Arena, 1);
        children.init(temp_arena.Arena, 1);
        siblings.init(temp_arena.Arena, 1);

        uint32_t i = 0;
        for (auto& node : NodeHierarchies)
        {
            if (node.Parent == -1)
            {
                auto& cmp = NodeUIComponents.at(i);
                if (cmp->IsVisible)
                {
                    roots.push(i);
                    if (node.FirstChild != -1)
                    {
                        auto& fc = NodeUIComponents[node.FirstChild];
                        if (fc->IsVisible)
                        {
                            children.push(node.FirstChild);
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
                siblings.push(sibling);
            }
        }

        int size = roots.size() + children.size() + siblings.size();
        if (NodeToRender.capacity() < size)
        {
        }

        for (auto r : roots)
        {
            NodeToRender.push(r);
        }

        for (auto c : children)
        {
            NodeToRender.push(c);
        }

        for (auto s : siblings)
        {
            NodeToRender.push(s);
        }

        for (auto node : NodeToRender)
        {

            auto& cmp = NodeUIComponents[node];
            cmp->Update(dt);
        }

        ZReleaseScratch(temp_arena);
    }

    int ImguiLayer::AddNode(Components::UIComponent* cmp, int parent, int depth)
    {
        if ((!cmp) || (depth < 0))
        {
            return -1;
        }

        auto node = NodeHierarchies.size();
        NodeHierarchies.push(Helpers::NodeHierarchy{.Parent = parent});

        ArrayView<Helpers::NodeHierarchy> nodes_view(NodeHierarchies.data(), NodeHierarchies.size());
        if (parent > -1)
        {
            int first = NodeHierarchies[parent].FirstChild;
            if (first == -1)
            {
                nodes_view[parent].FirstChild = node;
            }
            else
            {
                int sibling = NodeHierarchies[first].RightSibling;
                if (sibling == -1)
                {
                    nodes_view[first].RightSibling = node;
                }
                else
                {
                    for (sibling = first; NodeHierarchies[sibling].RightSibling != -1; sibling = NodeHierarchies[sibling].RightSibling)
                    {
                    }
                    nodes_view[sibling].RightSibling = node;
                }
            }
        }
        nodes_view[node].DepthLevel = depth;
        return node;
    }

    void ImguiLayer::AddUIComponent(Components::UIComponent* cmp, int parent, int depth)
    {
        if (!cmp)
        {
            return;
        }

        auto node_id = AddNode(cmp, parent, depth);
        if (!cmp->ParentLayer)
        {
            cmp->ParentLayer = this;
        }
        NodeUIComponents[node_id] = cmp;
        for (int i = 0; i < cmp->ChildrenCount; ++i)
        {
            AddUIComponent(cmp->Children[i], node_id, (depth + 1));
        }
    }

    bool ImguiLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (KeyEntries.contains(e.GetKeyCode()))
        {
            io.AddKeyEvent((ImGuiKey) KeyEntries.at(e.GetKeyCode()), true);
        }
        return false;
    }

    bool ImguiLayer::OnKeyReleased(KeyReleasedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (KeyEntries.contains(e.GetKeyCode()))
        {
            io.AddKeyEvent((ImGuiKey) KeyEntries.at(e.GetKeyCode()), false);
        }
        return false;
    }

    bool ImguiLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent((int) e.GetButton(), true);
        return false;
    }

    bool ImguiLayer::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent((int) e.GetButton(), false);
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
        event_dispatcher.ForwardTo<WindowClosedEvent>(std::bind(&ZEngine::Windows::CoreWindow::OnWindowClosed, CurrentApp->CurrentWindow, std::placeholders::_1));
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

    void ImguiLayer::Render(Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        for (auto& id : NodeToRender)
        {
            auto& cmp = NodeUIComponents[id];
            cmp->Render(renderer, command_buffer);
        }
    }
} // namespace Tetragrama::Layers
