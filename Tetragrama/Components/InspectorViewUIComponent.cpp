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
            ImGui::InputTextWithHint("##details_search", "Search Details...", s_filter, sizeof(s_filter));
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
            // Tint the child background slightly from the current Header color
            ImVec4 hdr = ImGui::GetStyleColorVec4(ImGuiCol_Header);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(hdr.x, hdr.y, hdr.z, 0.4f));
            ImGui::BeginChild("##actor_header", {-1.f, 42.f}, false, ImGuiWindowFlags_NoScrollbar);

            ImGui::SetCursorPos({10.f, 6.f});

            if (nc)
            {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 2.f));
                char buf[128] = {};
                snprintf(buf, sizeof(buf), "%s", nc->Value);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10.f);
                if (ImGui::InputText("##actor_name", buf, sizeof(buf)))
                    snprintf(nc->Value, sizeof(nc->Value), "%s", buf);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorPos({10.f, 24.f});
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted("Actor");
            ImGui::PopStyleColor();

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // ── Section header helper ──────────────────────────────────────────────
        auto SectionHeader = [](const char* id, const char* label, bool* open) -> bool {
            ImGui::PushID(id);

            ImVec2      pos = ImGui::GetCursorScreenPos();
            float       w   = ImGui::GetContentRegionAvail().x;
            float       h   = 22.f;
            ImDrawList* dl  = ImGui::GetWindowDrawList();
            bool        hov = ImGui::IsMouseHoveringRect(pos, {pos.x + w, pos.y + h});
            ImU32       bg  = ImGui::GetColorU32(hov ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
            dl->AddRectFilled(pos, {pos.x + w, pos.y + h}, bg);

            ImU32 tri = ImGui::GetColorU32(ImGuiCol_Text);
            float tx = pos.x + 8.f, ty = pos.y + h * 0.5f;
            if (*open)
                dl->AddTriangleFilled({tx, ty - 4.f}, {tx + 7.f, ty - 4.f}, {tx + 3.5f, ty + 4.f}, tri);
            else
                dl->AddTriangleFilled({tx, ty - 4.f}, {tx, ty + 4.f}, {tx + 7.f, ty}, tri);

            dl->AddText({tx + 14.f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), label);

            ImGui::InvisibleButton("##hdr", {w, h});
            if (ImGui::IsItemClicked())
                *open = !*open;

            ImGui::PopID();
            return *open;
        };

        // ── XYZ property row helper ────────────────────────────────────────────
        // UE-style: three equal drag fields with a colored 3px left border per axis.
        // No separate axis text — the border color IS the axis indicator.
        auto XYZRow = [](const char* label, Vec3f& v, Vec3f reset_vals, float speed, auto onChange) {
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(label);

            const float kGap      = 4.f;
            const float kReset    = 18.f;
            float       avail     = ImGui::GetContentRegionAvail().x - kReset - kGap;
            float       fw        = (avail - kGap * 2.f) / 3.f;

            ImU32       border[3] = {
                IM_COL32(215, 90, 80, 255),   // X red
                IM_COL32(100, 200, 110, 255), // Y green
                IM_COL32(90, 140, 230, 255),  // Z blue
            };
            const char* ids[3]  = {"##ax0", "##ax1", "##ax2"};
            float*      vals[3] = {&v.x, &v.y, &v.z};
            ImDrawList* dl      = ImGui::GetWindowDrawList();

            bool        changed = false;
            for (int i = 0; i < 3; ++i)
            {
                if (i > 0)
                    ImGui::SameLine(0.f, kGap);
                ImGui::SetNextItemWidth(fw);
                changed   |= ImGui::DragFloat(ids[i], vals[i], speed, 0.f, 0.f, "%.3f");
                // Colored left-border stripe overlay (drawn on top of the field)
                ImVec2 p0  = ImGui::GetItemRectMin();
                ImVec2 p1  = {p0.x + 3.f, ImGui::GetItemRectMax().y};
                dl->AddRectFilled(p0, p1, border[i]);
            }

            // Reset button
            ImGui::SameLine(0.f, kGap);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
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
            if (SectionHeader("transform_hdr", "Transform", &s_transform_open))
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
            if (SectionHeader("mesh_hdr", "Mesh", &s_mesh_open))
            {
                if (ImGui::BeginTable("##mesh_tbl", 2, kTableFlags))
                {
                    ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 72.f);
                    ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

                    ImGui::TableNextRow();
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextUnformatted("UUID");
                    ImGui::PopStyleColor();

                    ImGui::TableSetColumnIndex(1);
                    std::string uuid_str = uuids::to_string(mc->MeshUUID);
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputText("##uuid", const_cast<char*>(uuid_str.c_str()), uuid_str.size() + 1, ImGuiInputTextFlags_ReadOnly);

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

        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
        ImGui::Button("+ Add Component", {-1.f, 24.f});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::End();
    }
} // namespace Tetragrama::Components
