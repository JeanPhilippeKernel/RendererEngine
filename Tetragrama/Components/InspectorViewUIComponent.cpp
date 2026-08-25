// clang-format off
#include <Tetragrama/Components/InspectorViewUIComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/Engine.h>
#include <uuid.h>
// clang-format on

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Maths;

namespace
{
    using namespace ZEngine::ECS;
    using ZEngine::Core::Maths::Vec3f;

    bool SectionHeader(const char* id, const char* label, bool* open)
    {
        ImGui::PushID(id);
        ImVec2      pos = ImGui::GetCursorScreenPos();
        float       w   = ImGui::GetContentRegionAvail().x;
        float       h   = 22.f;
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        bool        hov = ImGui::IsMouseHoveringRect(pos, {pos.x + w, pos.y + h});
        dl->AddRectFilled(pos, {pos.x + w, pos.y + h}, ImGui::GetColorU32(hov ? ImGuiCol_HeaderHovered : ImGuiCol_Header));
        ImU32 tri = ImGui::GetColorU32(ImGuiCol_Text);
        float tx = pos.x + 8.f, ty = pos.y + h * 0.5f;
        if (*open)
        {
            dl->AddTriangleFilled({tx, ty - 4.f}, {tx + 7.f, ty - 4.f}, {tx + 3.5f, ty + 4.f}, tri);
        }
        else
        {
            dl->AddTriangleFilled({tx, ty - 4.f}, {tx, ty + 4.f}, {tx + 7.f, ty}, tri);
        }
        dl->AddText({tx + 14.f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), label);
        ImGui::InvisibleButton("##hdr", {w, h});
        if (ImGui::IsItemClicked())
        {
            *open = !*open;
        }
        ImGui::PopID();
        return *open;
    }

    int64_t ReadEnum(const void* ptr, uint32_t size)
    {
        switch (size)
        {
            case 1:
                return *static_cast<const uint8_t*>(ptr);
            case 2:
                return *static_cast<const uint16_t*>(ptr);
            case 4:
                return *static_cast<const int32_t*>(ptr);
            case 8:
                return *static_cast<const int64_t*>(ptr);
            default:
                return 0;
        }
    }

    void WriteEnum(void* ptr, uint32_t size, int64_t value)
    {
        switch (size)
        {
            case 1:
                *static_cast<uint8_t*>(ptr) = static_cast<uint8_t>(value);
                break;
            case 2:
                *static_cast<uint16_t*>(ptr) = static_cast<uint16_t>(value);
                break;
            case 4:
                *static_cast<int32_t*>(ptr) = static_cast<int32_t>(value);
                break;
            case 8:
                *static_cast<int64_t*>(ptr) = value;
                break;
            default:
                break;
        }
    }

    // Draws the three components of a Vec3f with the R/G/B axis bars.
    void Vec3Row(float* v)
    {
        constexpr float kGap      = 4.f;
        float           avail     = ImGui::GetContentRegionAvail().x;
        float           fw        = (avail - kGap * 2.f) / 3.f;
        ImU32           border[3] = {
            IM_COL32(215, 90, 80, 255),
            IM_COL32(100, 200, 110, 255),
            IM_COL32(90, 140, 230, 255),
        };
        const char* ids[3] = {"##ax0", "##ax1", "##ax2"};
        ImDrawList* dl     = ImGui::GetWindowDrawList();

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.f, kGap);
            }
            ImGui::SetNextItemWidth(fw);
            ImGui::DragFloat(ids[i], &v[i], 0.05f, 0.f, 0.f, "%.3f");
            ImVec2 p0 = ImGui::GetItemRectMin();
            dl->AddRectFilled(p0, {p0.x + 3.f, ImGui::GetItemRectMax().y}, border[i]);
        }
    }

    void DrawField(const FieldDescriptor& field, void* ptr)
    {
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(field.Name);
        ImGui::PopStyleColor();
        if (field.Tooltip && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", field.Tooltip);
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(field.Name);
        ImGui::SetNextItemWidth(-1.f);

        switch (field.Type)
        {
            case FieldType::Bool:
                ImGui::Checkbox("##v", static_cast<bool*>(ptr));
                break;
            case FieldType::Int32:
                ImGui::DragInt("##v", static_cast<int32_t*>(ptr));
                break;
            case FieldType::UInt32:
                ImGui::DragScalar("##v", ImGuiDataType_U32, ptr);
                break;
            case FieldType::Float:
                ImGui::DragFloat("##v", static_cast<float*>(ptr), 0.05f, field.Min, field.Max);
                break;
            case FieldType::Vec3f:
                Vec3Row(static_cast<float*>(ptr));
                break;
            case FieldType::String:
                ImGui::InputText("##v", static_cast<char*>(ptr), field.StringCap);
                break;
            case FieldType::AssetUUID:
            {
                char text[37] = {};
                uuids::to_string<char>(*static_cast<uuids::uuid*>(ptr), text);
                ImGui::InputText("##v", text, sizeof(text), ImGuiInputTextFlags_ReadOnly);
                break;
            }
            case FieldType::Enum:
            {
                int64_t     value   = ReadEnum(ptr, field.Size);
                const char* current = "unknown";
                for (uint32_t i = 0; i < field.EnumCount; ++i)
                {
                    if (field.EnumValues[i].Value == value)
                    {
                        current = field.EnumValues[i].Name;
                    }
                }
                if (ImGui::BeginCombo("##v", current))
                {
                    for (uint32_t i = 0; i < field.EnumCount; ++i)
                    {
                        bool selected = (field.EnumValues[i].Value == value);
                        if (ImGui::Selectable(field.EnumValues[i].Name, selected))
                        {
                            WriteEnum(ptr, field.Size, field.EnumValues[i].Value);
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            default:
                ImGui::TextDisabled("(unsupported type)");
                break;
        }

        ImGui::PopID();
    }

    void DrawEntityComponents(Scene& scene, EntityID id)
    {
        static constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX;

        const ArchetypeMask              mask        = scene.GetMask(id);

        ComponentReflectionRegistry::Get().ForEach([&](const ComponentMeta& meta) {
            if (!MaskHas(mask, meta.TypeID))
            {
                return;
            }

            void* raw = scene.GetComponentRaw(id, meta.TypeID);
            if (!raw)
            {
                return;
            }

            // Section open state is per component type, not per entity.
            static bool s_open[ARCHETYPE_MASK_CAPACITY] = {};
            static bool s_init                          = [] {
                for (bool& b : s_open)
                {
                    b = true;
                }
                return true;
            }();
            (void) s_init;

            if (!SectionHeader(meta.TypeName, meta.TypeName, &s_open[meta.TypeID]))
                return;

            ImGui::PushID(static_cast<int>(meta.TypeID));
            if (ImGui::BeginTable("##fields", 2, kTableFlags))
            {
                ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 96.f);
                ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                for (uint32_t i = 0; i < meta.FieldCount; ++i)
                {
                    const FieldDescriptor& field = meta.Fields[i];
                    if (field.Hidden)
                    {
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::BeginDisabled(field.ReadOnly);
                    DrawField(field, static_cast<uint8_t*>(raw) + field.Offset);
                    ImGui::EndDisabled();
                }

                ImGui::TableNextRow();
                ImGui::EndTable();
            }
            ImGui::PopID();
            ImGui::Spacing();
        });
    }
} // namespace

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

        {
            constexpr float kBtnW   = 112.f;
            constexpr float kMargin = 10.f;

            ImVec4          hdr     = ImGui::GetStyleColorVec4(ImGuiCol_Header);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(hdr.x, hdr.y, hdr.z, 0.4f));
            ImGui::BeginChild("##actor_header", {-1.f, 42.f}, false, ImGuiWindowFlags_NoScrollbar);

            float card_w = ImGui::GetContentRegionAvail().x;

            ImGui::SetCursorPos({kMargin, 6.f});

            ImGui::SetCursorPos({kMargin, 24.f});
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted("Actor");
            ImGui::PopStyleColor();

            constexpr float kBtnH = 22.f;
            ImGui::SetCursorPos({card_w - kBtnW - 4.f, (42.f - kBtnH) * 0.5f});
            bool        add_clicked = ImGui::InvisibleButton("##add_comp", {kBtnW, kBtnH});
            ImVec2      btn_scr     = ImGui::GetItemRectMin();
            ImDrawList* cdl         = ImGui::GetWindowDrawList();
            ImVec4      add_bg      = ImGui::IsItemHovered() ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered) : ImGui::GetStyleColorVec4(ImGuiCol_Header);
            cdl->AddRectFilled(btn_scr, {btn_scr.x + kBtnW, btn_scr.y + kBtnH}, ImGui::ColorConvertFloat4ToU32(add_bg), 3.f);
            float th = ImGui::GetTextLineHeight();
            float ty = btn_scr.y + (kBtnH - th) * 0.5f;
            float tx = btn_scr.x + 8.f;
            cdl->AddText({tx, ty}, IM_COL32(90, 210, 120, 255), "+");
            cdl->AddText({tx + ImGui::CalcTextSize("+").x + 5.f, ty}, ImGui::GetColorU32(ImGuiCol_Text), "Add");
            (void) add_clicked;

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        DrawEntityComponents(*ctx->Scene, actor->GetEntityID());

        ImGui::End();
    }
} // namespace Tetragrama::Components
