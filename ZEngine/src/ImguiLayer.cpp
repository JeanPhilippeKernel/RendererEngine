#include <pch.h>
#include <Layers/ImguiLayer.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ZEngineDef.h>
#include <fmt/format.h>

#include <GLFW/glfw3.h>

namespace ZEngine::Layers {

    bool ImguiLayer::m_initialized = false;

    ImguiLayer::~ImguiLayer() {
        m_ui_components.clear();
        if (m_initialized) {
            ImGui_ImplOpenGL3_Shutdown();


            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();

            m_initialized = false;
        }
    }

    void ImguiLayer::Initialize() {
        if (!m_initialized) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            StyleDarkTheme();

            ImGuiIO& io = ImGui::GetIO();

            std::string_view default_layout_ini = "Settings/DefaultLayout.ini";
            const auto       current_directoy   = std::filesystem::current_path();
            auto             layout_file_path   = fmt::format("{0}/{1}", current_directoy.string(), default_layout_ini);
            if (std::filesystem::exists(std::filesystem::path(layout_file_path))) {
                io.IniFilename = default_layout_ini.data();
            }

            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

            io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Bold.ttf", 17.f);
            io.FontDefault = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

            auto& style            = ImGui::GetStyle();
            style.WindowBorderSize = 0.f;
            style.ChildBorderSize  = 0.f;


            ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(m_window.lock()->GetNativeWindow()), false);

            // we should get Version information from attached Window...
#ifdef _WIN32
            ImGui_ImplOpenGL3_Init("#version 450");
#else
            ImGui_ImplOpenGL3_Init("#version 330");
#endif
            m_initialized = true;
        }
    }

    void ImguiLayer::Update(Core::TimeStep dt) {
        ImGuiIO& io = ImGui::GetIO();
        if (dt > 0.0f) {
            io.DeltaTime = dt;
        }
        for (const auto& component : m_ui_components) {
            component->Update(dt);
        }
    }

    void ImguiLayer::AddUIComponent(const Ref<Components::UI::UIComponent>& component) {
        m_ui_components.push_back(component);

        if (!component->HasParentLayer()) {
            auto last = std::prev(std::end(m_ui_components));
            (*last)->SetParentLayer(shared_from_this());
        }
    }

    void ImguiLayer::AddUIComponent(Ref<Components::UI::UIComponent>&& component) {
        if (!component->HasParentLayer()) {
            component->SetParentLayer(shared_from_this());
        }
        m_ui_components.push_back(component);
    }

    void ImguiLayer::AddUIComponent(std::vector<Ref<Components::UI::UIComponent>>&& components) {
        std::for_each(std::begin(components), std::end(components), [this](Ref<Components::UI::UIComponent>& component) {
            if (!component->HasParentLayer()) {
                component->SetParentLayer(shared_from_this());
            }
        });

        std::move(std::begin(components), std::end(components), std::back_inserter(m_ui_components));
    }

    void ImguiLayer::Render() {
        ImGui_ImplOpenGL3_NewFrame();

        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        for (const auto& component : m_ui_components) {
            if (component->GetVisibility() == true) {
                component->Render();
            }
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {

            GLFWwindow* backup_current_context = static_cast<GLFWwindow*>(m_window.lock()->GetNativeContext());
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();


            glfwMakeContextCurrent(backup_current_context);
        }
    }

    bool ImguiLayer::OnKeyPressed(Event::KeyPressedEvent& e) {
        ImGuiIO& io                       = ImGui::GetIO();
        io.KeysDown[(int) e.GetKeyCode()] = true;
        return false;
    }

    bool ImguiLayer::OnKeyReleased(Event::KeyReleasedEvent& e) {
        ImGuiIO& io                       = ImGui::GetIO();
        io.KeysDown[(int) e.GetKeyCode()] = false;
        return false;
    }

    bool ImguiLayer::OnMouseButtonPressed(Event::MouseButtonPressedEvent& e) {
        ImGuiIO& io                            = ImGui::GetIO();
        io.MouseDown[(uint32_t) e.GetButton()] = true;
        return false;
    }

    bool ImguiLayer::OnMouseButtonReleased(Event::MouseButtonReleasedEvent& e) {
        ImGuiIO& io                       = ImGui::GetIO();
        io.MouseDown[(int) e.GetButton()] = false;
        return false;
    }

    bool ImguiLayer::OnMouseButtonMoved(Event::MouseButtonMovedEvent& e) {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(float(e.GetPosX()), float(e.GetPosY()));
        return false;
    }

    bool ImguiLayer::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e) {
        ImGuiIO& io = ImGui::GetIO();
        if (e.GetOffetX() > 0)
            io.MouseWheelH += 1;
        if (e.GetOffetX() < 0)
            io.MouseWheelH -= 1;
        if (e.GetOffetY() > 0)
            io.MouseWheel += 1;
        if (e.GetOffetY() < 0)
            io.MouseWheel -= 1;
        return false;
    }

    bool ImguiLayer::OnTextInputRaised(Event::TextInputEvent& event) {
        ImGuiIO& io = ImGui::GetIO();
        for (unsigned char c : event.GetText())
            io.AddInputCharacter(c);
        return false;
    }

    bool ImguiLayer::OnWindowClosed(Event::WindowClosedEvent& event) {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowClosedEvent>(std::bind(&ZEngine::Window::CoreWindow::OnWindowClosed, GetAttachedWindow().get(), std::placeholders::_1));
        return true;
    }

    void ImguiLayer::StyleDarkTheme() {
        auto& colors              = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{0.1f, 0.105f, 0.11f, 1.0f};

        // Headers
        colors[ImGuiCol_Header]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_HeaderHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_HeaderActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Buttons
        colors[ImGuiCol_Button]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_ButtonHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_ButtonActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Frame BG
        colors[ImGuiCol_FrameBg]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_FrameBgHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_FrameBgActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Tabs
        colors[ImGuiCol_Tab]                = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TabHovered]         = ImVec4{0.38f, 0.3805f, 0.381f, 1.0f};
        colors[ImGuiCol_TabActive]          = ImVec4{0.28f, 0.2805f, 0.281f, 1.0f};
        colors[ImGuiCol_TabUnfocused]       = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};

        // Title
        colors[ImGuiCol_TitleBg]          = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TitleBgActive]    = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        colors[ImGuiCol_DockingPreview]   = ImVec4{0.2f, 0.205f, 0.21f, .5f};
        colors[ImGuiCol_SeparatorHovered] = ImVec4{1.f, 1.f, 1.0f, .5f};
        colors[ImGuiCol_SeparatorActive]  = ImVec4{1.f, 1.f, 1.0f, .5f};
        colors[ImGuiCol_CheckMark]        = ImVec4{1.0f, 1.f, 1.0f, 1.f};
    }

} // namespace ZEngine::Layers
