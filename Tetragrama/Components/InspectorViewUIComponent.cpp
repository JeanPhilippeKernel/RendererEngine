// clang-format off
#include <Tetragrama/Components/InspectorViewUIComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
// clang-format on

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Maths;

namespace Tetragrama::Components
{
    InspectorViewUIComponent::InspectorViewUIComponent()  = default;
    InspectorViewUIComponent::~InspectorViewUIComponent() = default;

    void InspectorViewUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
    }

    void InspectorViewUIComponent::Update(ZEngine::Core::TimeStep /*dt*/) {}

    void InspectorViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const /*renderer*/, ZEngine::Hardwares::CommandBuffer* const /*command_buffer*/)
    {
        ImGui::Begin(Name, CanBeClosed ? &CanBeClosed : nullptr, ImGuiWindowFlags_NoCollapse);

        auto* ctx = ZEngine::Engine::GetContext();
        if (!ctx || !ctx->ActorManager || !ParentLayer || !ParentLayer->CurrentApp)
        {
            ImGui::End();
            return;
        }

        auto* app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto* current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        if (!current_scene)
        {
            ImGui::End();
            return;
        }

        // ── Search bar ─────────────────────────────────────────────────────────
        {
            static char s_filter[128] = {};
            ImGui::SetNextItemWidth(-1.f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.15f, 0.18f, 1.f));
            ImGui::InputTextWithHint("##details_search", "Search Details...", s_filter, sizeof(s_filter));
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        ActorHandle h     = current_scene->SelectedActorHandle;
        Actor*      actor = ctx->ActorManager->Access(h);
        if (!actor)
        {
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("No actor selected").x) * 0.5f);
            ImGui::TextDisabled("No actor selected");
            ImGui::End();
            return;
        }

        // ── Actor header card ──────────────────────────────────────────────────
        auto* nc = actor->GetComponent<NameComponent>();
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.18f, 0.22f, 1.f));
            ImGui::BeginChild("##actor_header", {-1.f, 42.f}, false, ImGuiWindowFlags_NoScrollbar);

            ImGui::SetCursorPos({10.f, 6.f});

            if (nc)
            {
                // Editable name — borderless inside the card
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 2.f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.97f, 1.f));

                char buf[128] = {};
                snprintf(buf, sizeof(buf), "%s", nc->Value);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10.f);
                if (ImGui::InputText("##actor_name", buf, sizeof(buf)))
                    snprintf(nc->Value, sizeof(nc->Value), "%s", buf);

                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorPos({10.f, 24.f});
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.50f, 0.60f, 1.f));
            ImGui::TextUnformatted("Actor");
            ImGui::PopStyleColor();

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // ── Section header helper ──────────────────────────────────────────────
        // Draws a collapsible UE-style section bar and returns whether it's open.
        auto SectionHeader = [](const char* id, const char* label, bool* open) -> bool {
            ImGui::PushID(id);

            // Background
            ImVec2      pos = ImGui::GetCursorScreenPos();
            float       w   = ImGui::GetContentRegionAvail().x;
            float       h   = 22.f;
            ImDrawList* dl  = ImGui::GetWindowDrawList();
            bool        hov = ImGui::IsMouseHoveringRect(pos, {pos.x + w, pos.y + h});
            ImU32       bg  = hov ? IM_COL32(44, 48, 58, 255) : IM_COL32(36, 39, 48, 255);
            dl->AddRectFilled(pos, {pos.x + w, pos.y + h}, bg);

            // Triangle
            float tx = pos.x + 8.f, ty = pos.y + h * 0.5f;
            if (*open)
                dl->AddTriangleFilled({tx, ty - 4.f}, {tx + 7.f, ty - 4.f}, {tx + 3.5f, ty + 4.f}, IM_COL32(170, 175, 190, 255));
            else
                dl->AddTriangleFilled({tx, ty - 4.f}, {tx, ty + 4.f}, {tx + 7.f, ty}, IM_COL32(130, 135, 150, 255));

            // Label
            dl->AddText({tx + 14.f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, IM_COL32(200, 205, 215, 255), label);

            ImGui::InvisibleButton("##hdr", {w, h});
            if (ImGui::IsItemClicked())
                *open = !*open;

            ImGui::PopID();
            return *open;
        };

        // ── XYZ property row helper ────────────────────────────────────────────
        // Draws:  [label]  X[___] Y[___] Z[___]  [↺]
        // inside a two-cell table row (caller manages BeginTable / EndTable).
        static constexpr float kXCol  = IM_COL32(215, 90, 80, 255);
        static constexpr float kYCol  = IM_COL32(100, 200, 110, 255);
        static constexpr float kZCol  = IM_COL32(90, 140, 230, 255);

        auto                   XYZRow = [](const char* label, Vec3f& v, Vec3f reset_vals, float speed, auto onChange) {
            // Label cell
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.65f, 0.72f, 1.f));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();

            // Value cell: X Y Z + reset
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(label);

            float avail   = ImGui::GetContentRegionAvail().x - 22.f; // 22 = reset btn
            float fw      = (avail - 6.f * 2.f) / 3.f;               // 6 = gap between fields

            bool  changed = false;
            struct
            {
                const char* lbl;
                float*      val;
                ImU32       col;
            } axes[3] = {
                {"X", &v.x, IM_COL32(215,  90,  80, 255)},
                {"Y", &v.y, IM_COL32(100, 200, 110, 255)},
                {"Z", &v.z,  IM_COL32(90, 140, 230, 255)},
            };

            for (int i = 0; i < 3; ++i)
            {
                if (i > 0)
                    ImGui::SameLine(0.f, 6.f);
                // Colored axis label
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(((axes[i].col >> 0) & 0xFF) / 255.f, ((axes[i].col >> 8) & 0xFF) / 255.f, ((axes[i].col >> 16) & 0xFF) / 255.f, 1.f));
                ImGui::TextUnformatted(axes[i].lbl);
                ImGui::PopStyleColor();
                ImGui::SameLine(0.f, 2.f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.13f, 0.16f, 1.f));
                ImGui::SetNextItemWidth(fw - ImGui::CalcTextSize(axes[i].lbl).x - 2.f);
                changed |= ImGui::DragFloat(axes[i].lbl, axes[i].val, speed, 0.f, 0.f, "%.3f");
                ImGui::PopStyleColor();
            }

            // Reset button
            ImGui::SameLine(0.f, 6.f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, 1.f, 1.f, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.f, 1.f, 1.f, 0.15f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.55f, 1.f));
            if (ImGui::SmallButton("↺"))
            {
                v       = reset_vals;
                changed = true;
            }
            ImGui::PopStyleColor(4);

            ImGui::PopID();
            if (changed)
                onChange(v);
        };

        static ImGuiTableFlags kTableFlags = ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX;

        // ── Transform section ──────────────────────────────────────────────────
        auto*                  tc          = actor->GetComponent<TransformComponent>();
        if (tc)
        {
            static bool s_transform_open = true;
            if (SectionHeader("transform_hdr", "TRANSFORM", &s_transform_open))
            {
                if (ImGui::BeginTable("##transform_tbl", 2, kTableFlags))
                {
                    ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 72.f);
                    ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

                    constexpr float kDeg = 180.f / 3.14159265f;
                    constexpr float kRad = 3.14159265f / 180.f;

                    ImGui::TableNextRow();
                    ImGui::TableNextRow();
                    XYZRow("Location", tc->Position, {0.f, 0.f, 0.f}, 0.05f, [tc](Vec3f& v) { tc->Position = v; });

                    ImGui::TableNextRow();
                    Vec3f rot_deg = {tc->Rotation.x * kDeg, tc->Rotation.y * kDeg, tc->Rotation.z * kDeg};
                    XYZRow("Rotation", rot_deg, {0.f, 0.f, 0.f}, 0.5f, [tc, kRad](Vec3f& v) { tc->Rotation = {v.x * kRad, v.y * kRad, v.z * kRad}; });

                    ImGui::TableNextRow();
                    XYZRow("Scale", tc->Scale, {1.f, 1.f, 1.f}, 0.01f, [tc](Vec3f& v) { tc->Scale = v; });

                    ImGui::TableNextRow();
                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }
        }

        // ── Mesh section ───────────────────────────────────────────────────────
        auto* mc = actor->GetComponent<MeshComponent>();
        if (mc)
        {
            static bool s_mesh_open = true;
            if (SectionHeader("mesh_hdr", "MESH", &s_mesh_open))
            {
                if (ImGui::BeginTable("##mesh_tbl", 2, kTableFlags))
                {
                    ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 72.f);
                    ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

                    ImGui::TableNextRow();
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.65f, 0.72f, 1.f));
                    ImGui::TextUnformatted("UUID");
                    ImGui::PopStyleColor();

                    ImGui::TableSetColumnIndex(1);
                    std::string uuid_str = uuids::to_string(mc->MeshUUID);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.13f, 0.16f, 1.f));
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("##uuid", const_cast<char*>(uuid_str.c_str()), uuid_str.size() + 1, ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleColor();

                    ImGui::TableNextRow();
                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }
        }

        // ── Add Component ──────────────────────────────────────────────────────
        float remaining = ImGui::GetContentRegionAvail().y;
        if (remaining > 30.f)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + remaining - 30.f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.24f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.26f, 0.36f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.30f, 0.44f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
        ImGui::Button("+ Add Component", {-1.f, 24.f});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::End();
    }
} // namespace Tetragrama::Components
