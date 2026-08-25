// clang-format off
#include <Tetragrama/Components/HierarchyViewUIComponent.h>
#include <ImGuizmo/ImGuizmo.h>
#include <Tetragrama/Editor.h>

#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <ZEngine/Windows/Inputs/Keyboard.h>
// clang-format on

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Windows::Inputs;

namespace Tetragrama::Components
{
    HierarchyViewUIComponent::HierarchyViewUIComponent()  = default;
    HierarchyViewUIComponent::~HierarchyViewUIComponent() = default;

    void HierarchyViewUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
        parent->LocalArena.CreateSubArena(ZKilo(512), &m_outliner_arena);
        m_collapsed.init(&m_outliner_arena, 64);
    }

    void HierarchyViewUIComponent::Update(ZEngine::Core::TimeStep /*dt*/) {}

    bool HierarchyViewUIComponent::IsCollapsed(EntityID eid) const
    {
        for (uint32_t i = 0; i < m_collapsed.size(); ++i)
            if (m_collapsed[i] == eid)
                return true;
        return false;
    }

    void HierarchyViewUIComponent::ToggleCollapsed(EntityID eid)
    {
        for (uint32_t i = 0; i < m_collapsed.size(); ++i)
        {
            if (m_collapsed[i] == eid)
            {
                m_collapsed[i] = INVALID_ENTITY; // sentinel slot; IsCollapsed skips it (valid entities have Generation!=0)
                return;
            }
        }
        m_collapsed.push(eid);
    }

    // Static helpers

    void HierarchyViewUIComponent::DrawArrow(ImDrawList* dl, ImVec2 pos, float row_h, bool expanded, ImU32 color)
    {
        float cx = pos.x + 8.f;
        float cy = pos.y + row_h * 0.5f;
        float sz = 5.f;
        if (expanded)
            dl->AddTriangleFilled({cx - sz, cy - sz * 0.5f}, {cx + sz, cy - sz * 0.5f}, {cx, cy + sz * 0.65f}, color);
        else
            dl->AddTriangleFilled({cx - sz * 0.5f, cy - sz}, {cx + sz * 0.65f, cy}, {cx - sz * 0.5f, cy + sz}, color);
    }

    void HierarchyViewUIComponent::DrawTypeIcon(ImDrawList* dl, ImVec2 pos, float sz, const char* type, bool is_collection)
    {
        float cx = pos.x + sz * 0.5f;
        float cy = pos.y + sz * 0.5f;

        if (is_collection)
        {
            ImU32 col = ImGui::ColorConvertFloat4ToU32({0.85f, 0.65f, 0.15f, 0.95f});
            float fw  = sz * 0.80f;
            float fh  = sz * 0.65f;
            float ix  = pos.x + (sz - fw) * 0.5f;
            float iy  = pos.y + sz - fh;
            dl->AddRectFilled({ix, iy + fh * 0.28f}, {ix + fw, iy + fh}, col, 1.5f);
            dl->AddRectFilled({ix, iy}, {ix + fw * 0.44f, iy + fh * 0.32f}, col, 1.5f, ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);
            return;
        }

        if (Helpers::secure_strcmp(type, "World") == 0)
        {
            ImU32 col = ImGui::ColorConvertFloat4ToU32({0.35f, 0.80f, 0.45f, 1.0f});
            float r   = sz * 0.40f;
            dl->AddCircle({cx, cy}, r, col, 16, 1.2f);
            dl->AddLine({cx - r, cy}, {cx + r, cy}, col, 1.0f);
            dl->AddLine({cx, cy - r}, {cx, cy + r}, col, 1.0f);
            return;
        }

        if (Helpers::secure_strcmp(type, "Light") == 0)
        {
            ImU32 col  = ImGui::ColorConvertFloat4ToU32({1.0f, 0.85f, 0.20f, 1.0f});
            float r    = sz * 0.20f;
            float ray  = sz * 0.40f;
            float diag = ray * 0.70f;
            dl->AddCircleFilled({cx, cy}, r, col, 8);
            dl->AddLine({cx, cy - ray}, {cx, cy - r}, col, 1.2f);
            dl->AddLine({cx, cy + r}, {cx, cy + ray}, col, 1.2f);
            dl->AddLine({cx - ray, cy}, {cx - r, cy}, col, 1.2f);
            dl->AddLine({cx + r, cy}, {cx + ray, cy}, col, 1.2f);
            dl->AddLine({cx - diag, cy - diag}, {cx - r * 0.7f, cy - r * 0.7f}, col, 1.0f);
            dl->AddLine({cx + r * 0.7f, cy - r * 0.7f}, {cx + diag, cy - diag}, col, 1.0f);
            dl->AddLine({cx - diag, cy + diag}, {cx - r * 0.7f, cy + r * 0.7f}, col, 1.0f);
            dl->AddLine({cx + r * 0.7f, cy + r * 0.7f}, {cx + diag, cy + diag}, col, 1.0f);
            return;
        }

        if (Helpers::secure_strcmp(type, "Static Mesh") == 0)
        {
            ImU32 col = ImGui::ColorConvertFloat4ToU32({0.55f, 0.75f, 0.90f, 1.0f});
            float hw = sz * 0.28f, hh = sz * 0.28f, d = sz * 0.16f;
            float bx = cx - hw, by = cy;
            // Front face
            dl->AddRect({bx, by}, {bx + hw * 2, by + hh * 2}, col, 0.f, 0, 1.0f);
            // Back face
            dl->AddRect({bx + d, by - d}, {bx + hw * 2 + d, by + hh * 2 - d}, col, 0.f, 0, 0.6f);
            dl->AddLine({bx, by}, {bx + d, by - d}, col, 0.6f);
            dl->AddLine({bx + hw * 2, by}, {bx + hw * 2 + d, by - d}, col, 0.6f);
            dl->AddLine({bx, by + hh * 2}, {bx + d, by + hh * 2 - d}, col, 0.6f);
            return;
        }

        if (Helpers::secure_strcmp(type, "Camera") == 0)
        {
            ImU32 col = ImGui::ColorConvertFloat4ToU32({0.45f, 0.85f, 0.55f, 1.0f});
            float bw = sz * 0.55f, bh = sz * 0.40f;
            float bx = pos.x + sz * 0.05f, by = cy - bh * 0.5f;
            dl->AddRectFilled({bx, by}, {bx + bw, by + bh}, col, 1.5f);
            dl->AddTriangleFilled({bx + bw, by + bh * 0.1f}, {bx + bw + sz * 0.25f, cy}, {bx + bw, by + bh * 0.9f}, col);
            return;
        }

        // Default — grey diamond
        {
            ImU32 col = ImGui::ColorConvertFloat4ToU32({0.55f, 0.55f, 0.60f, 1.0f});
            float r   = sz * 0.35f;
            dl->AddQuadFilled({cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}, col);
        }
    }

    // Render

    void HierarchyViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const /*renderer*/, ZEngine::Hardwares::CommandBuffer* const /*command_buffer*/)
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto* app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto* current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        auto* ctx           = Engine::GetContext();
        if (!current_scene || !ctx || !ctx->ActorManager)
            return;

        ImGui::Begin(Name, CanBeClosed ? &CanBeClosed : nullptr, ImGuiWindowFlags_NoCollapse);

        // Search + Buttons
        {
            const float btn_sz  = 22.f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float avail   = ImGui::GetContentRegionAvail().x;
            const float box_w   = avail - btn_sz * 2.f - spacing * 2.f;

            ImGui::SetNextItemWidth(box_w);
            ImGui::InputText("##filter", m_filter_buf, sizeof(m_filter_buf));
            ImGui::SameLine();

            // Folder+ — New Collection
            ImVec2 btn_pos     = ImGui::GetCursorScreenPos();
            bool   clicked_add = ImGui::InvisibleButton("##new_collection", {btn_sz, btn_sz});
            bool   col_hov     = ImGui::IsItemHovered();
            if (col_hov)
                ImGui::SetTooltip("New Collection");
            {
                ImDrawList* fdl = ImGui::GetWindowDrawList();
                ImU32       col = ImGui::ColorConvertFloat4ToU32(col_hov ? ImVec4(0.85f, 0.85f, 0.90f, 1.f) : ImVec4(0.55f, 0.58f, 0.65f, 1.f));
                float       x = btn_pos.x, y = btn_pos.y, s = btn_sz;
                float       m   = s * 0.12f;
                float       bx0 = x + m, by0 = y + s * 0.32f;
                float       bx1 = x + s - m, by1 = y + s - m;
                fdl->AddRectFilled({bx0, by0}, {bx1, by1}, col, 2.f);
                float tx0 = bx0, ty0 = by0 - s * 0.14f;
                float tx1 = bx0 + (bx1 - bx0) * 0.45f, ty1 = by0 + 1.f;
                fdl->AddRectFilled({tx0, ty0}, {tx1, ty1}, col, 2.f, ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);
                float cx = (bx0 + bx1) * 0.5f, cy = (by0 + by1) * 0.5f;
                float hl = s * 0.18f, thk = 1.5f;
                ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.14f, 0.15f, 0.18f, 1.f));
                fdl->AddLine({cx - hl, cy}, {cx + hl, cy}, bg, thk + 1.5f);
                fdl->AddLine({cx, cy - hl}, {cx, cy + hl}, bg, thk + 1.5f);
                fdl->AddLine({cx - hl, cy}, {cx + hl, cy}, col, thk);
                fdl->AddLine({cx, cy - hl}, {cx, cy + hl}, col, thk);
            }
            ImGui::SameLine();

            // Settings gear placeholder
            ImGui::Button("##gear", {btn_sz, btn_sz});
            {
                ImDrawList* gdl = ImGui::GetWindowDrawList();
                ImVec2      gp  = ImGui::GetItemRectMin();
                float       gs  = btn_sz * 0.45f;
                float       gcx = gp.x + btn_sz * 0.5f, gcy = gp.y + btn_sz * 0.5f;
                ImU32       gc = ImGui::ColorConvertFloat4ToU32({0.65f, 0.65f, 0.70f, 1.f});
                gdl->AddCircle({gcx, gcy}, gs * 0.5f, gc, 8, 1.5f);
                gdl->AddCircleFilled({gcx, gcy}, gs * 0.2f, gc);
            }

            if (clicked_add)
            {
                ActorHandle coll_h = ctx->ActorManager->Create();
                Actor*      coll_a = ctx->ActorManager->Access(coll_h);
                if (coll_a)
                {
                    NameComponent nc = {};
                    Helpers::secure_strncpy(nc.Value, sizeof(nc.Value), "Collection", 10);
                    coll_a->AddComponent<NameComponent>(nc);
                    coll_a->AddComponent<TransformComponent>({});
                    current_scene->SelectedActorHandle = coll_h;
                }
            }
        }
        ImGui::Spacing();

        // O(n) Build
        auto     scratch = ZGetScratch(&ParentLayer->LocalArena);
        uint32_t n       = ctx->ActorManager->Count();

        struct OutlinerNode
        {
            ActorHandle Handle;
            EntityID    EID;
            EntityID    Parent;
        };

        OutlinerNode* nodes       = static_cast<OutlinerNode*>(scratch.Arena->Allocate(n * sizeof(OutlinerNode), alignof(OutlinerNode)));
        uint32_t*     first_child = static_cast<uint32_t*>(scratch.Arena->Allocate(n * sizeof(uint32_t), alignof(uint32_t)));
        uint32_t*     next_sib    = static_cast<uint32_t*>(scratch.Arena->Allocate(n * sizeof(uint32_t), alignof(uint32_t)));
        uint32_t      nc          = 0;

        for (uint32_t i = 0; i < n; ++i)
        {
            first_child[i] = UINT32_MAX;
            next_sib[i]    = UINT32_MAX;
        }

        ctx->ActorManager->ForEach([&](ActorHandle h, Actor* actor) {
            auto* pc    = actor->GetComponent<ParentComponent>();
            nodes[nc++] = {h, actor->GetEntityID(), (pc && pc->Parent != INVALID_ENTITY) ? pc->Parent : INVALID_ENTITY};
        });

        ZEngine::Core::Containers::UnorderedHashMap<EntityID, uint32_t> eid_to_idx;
        eid_to_idx.init(scratch.Arena, nc * 2);
        for (uint32_t i = 0; i < nc; ++i)
            eid_to_idx.insert(nodes[i].EID, i);

        for (uint32_t i = 0; i < nc; ++i)
        {
            if (nodes[i].Parent == INVALID_ENTITY)
                continue;
            auto* pidx = eid_to_idx.find(nodes[i].Parent);
            if (!pidx)
                continue;
            next_sib[i]        = first_child[*pidx];
            first_child[*pidx] = i;
        }

        struct DFSEntry
        {
            uint32_t idx;
            int      depth;
        };
        DFSEntry* stk = static_cast<DFSEntry*>(scratch.Arena->Allocate(nc * 2 * sizeof(DFSEntry), alignof(DFSEntry)));
        int32_t   sp  = 0;
        for (int32_t i = (int32_t) nc - 1; i >= 0; --i)
            if (nodes[i].Parent == INVALID_ENTITY)
                stk[sp++] = {(uint32_t) i, 0};

        // Table
        ActorHandle            pending_delete               = {};
        ActorHandle            pending_reparent_child       = {};
        EntityID               pending_reparent_parent      = INVALID_ENTITY;
        ActorHandle            pending_remove_parent_handle = {};
        uint32_t               total_actors                 = nc;
        uint32_t               selected_count               = 0;

        constexpr float        ROW_H                        = 22.f;
        constexpr float        ICON_SZ                      = 13.f;
        constexpr float        INDENT_W                     = 16.f;
        constexpr float        ARROW_W                      = 16.f;
        const ImU32            COL_ARROW                    = ImGui::ColorConvertFloat4ToU32({0.65f, 0.65f, 0.70f, 1.f});
        const ImU32            COL_SEL                      = ImGui::ColorConvertFloat4ToU32({0.26f, 0.44f, 0.70f, 0.60f});
        const ImU32            COL_HOVER                    = ImGui::ColorConvertFloat4ToU32({0.26f, 0.44f, 0.70f, 0.25f});
        const ImU32            COL_TEXT                     = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Text));
        const ImU32            COL_DIMTEXT                  = ImGui::ColorConvertFloat4ToU32({0.55f, 0.55f, 0.60f, 1.f});

        static ImGuiTableFlags tbl_flags                    = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        float                  tbl_h                        = ImGui::GetContentRegionAvail().y - ROW_H - ImGui::GetStyle().ItemSpacing.y * 2.f;
        ImVec2                 tbl_sz                       = {0.f, tbl_h};

        if (ImGui::BeginTable("##outliner", 3, tbl_flags, tbl_sz))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch, 0.60f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 0.15f);

            // Column headers
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0);
            {
                // Eye icon in header
                ImDrawList* hdl = ImGui::GetWindowDrawList();
                ImVec2      hp  = ImGui::GetCursorScreenPos();
                float       hs  = ImGui::GetTextLineHeight();
                ImU32       hc  = COL_DIMTEXT;
                float       ex = hp.x + 7.f, ey = hp.y + hs * 0.5f;
                // Eye icon: two arcs (top + bottom) with a pupil dot
                hdl->AddCircle({ex, ey}, 3.5f, hc, 8, 1.2f);
                hdl->AddCircleFilled({ex, ey}, 1.5f, hc);
                ImGui::Dummy({14.f, hs});
                ImGui::SameLine(0.f, 2.f);
            }
            ImGui::TableHeader("Item Label");
            ImGui::TableSetColumnIndex(1);
            ImGui::TableHeader("Type");
            ImGui::TableSetColumnIndex(2);
            ImGui::TableHeader("Level");

            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Scene Root row
            {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, ROW_H);
                ImGui::TableSetColumnIndex(0);
                ImVec2 row_p          = ImGui::GetCursorScreenPos();

                bool   root_collapsed = m_scene_root_collapsed;
                bool   root_selected  = false; // scene root can't be selected

                // Hover / bg
                ImVec2 row_end        = {row_p.x + ImGui::GetContentRegionAvail().x + ImGui::GetScrollX() + 800.f, row_p.y + ROW_H};
                bool   hov            = ImGui::IsMouseHoveringRect(row_p, row_end);
                if (hov)
                    dl->AddRectFilled(row_p, row_end, COL_HOVER);

                // Arrow
                float ax = row_p.x + 2.f;
                ImGui::SetCursorScreenPos({ax, row_p.y});
                ImGui::PushID(0xFFFF0000);
                ImGui::InvisibleButton("##root_arrow", {ARROW_W, ROW_H});
                if (ImGui::IsItemClicked())
                    m_scene_root_collapsed = !m_scene_root_collapsed;
                ImGui::PopID();
                DrawArrow(dl, {ax, row_p.y}, ROW_H, !root_collapsed, COL_ARROW);

                // Icon
                float ix = ax + ARROW_W + 2.f;
                DrawTypeIcon(dl, {ix, row_p.y + (ROW_H - ICON_SZ) * 0.5f}, ICON_SZ, "World", false);

                // Label
                float lx = ix + ICON_SZ + 5.f;
                ImGui::SetCursorScreenPos({lx, row_p.y + (ROW_H - ImGui::GetTextLineHeight()) * 0.5f});
                const char* scene_name = (current_scene->Name && current_scene->Name[0]) ? current_scene->Name : "DefaultScene";
                ImGui::TextUnformatted(scene_name);

                ImGui::TableSetColumnIndex(1);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ROW_H - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::TextDisabled("World");
            }

            // Actor rows (DFS)
            bool scene_root_collapsed = m_scene_root_collapsed;
            if (!scene_root_collapsed)
            {
                while (sp > 0)
                {
                    DFSEntry e     = stk[--sp];
                    uint32_t ni    = e.idx;
                    Actor*   actor = ctx->ActorManager->Access(nodes[ni].Handle);
                    if (!actor)
                        continue;

                    auto*       nc_comp = actor->GetComponent<NameComponent>();
                    const char* label   = (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "Actor";

                    if (m_filter_buf[0] && !strstr(label, m_filter_buf))
                        continue;

                    bool        is_collection = !actor->HasComponent<MeshComponent>() && !actor->HasComponent<LightComponent>() && !actor->HasComponent<CameraComponent>();
                    const char* type_str      = is_collection ? "Collection" : actor->HasComponent<LightComponent>() ? "Light" : actor->HasComponent<CameraComponent>() ? "Camera" : actor->HasComponent<MeshComponent>() ? "Static Mesh" : "Actor";

                    bool        has_children  = (first_child[ni] != UINT32_MAX);
                    bool        collapsed     = has_children && IsCollapsed(nodes[ni].EID);
                    bool        selected      = (current_scene->SelectedActorHandle.Index == nodes[ni].Handle.Index && current_scene->SelectedActorHandle.Generation == nodes[ni].Handle.Generation);
                    if (selected)
                        ++selected_count;

                    bool renaming = (m_renaming_handle.Index == nodes[ni].Handle.Index && m_renaming_handle.Generation == nodes[ni].Handle.Generation);

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, ROW_H);
                    ImGui::TableSetColumnIndex(0);
                    ImVec2 row_p   = ImGui::GetCursorScreenPos();
                    ImVec2 row_end = {row_p.x + ImGui::GetContentRegionAvail().x + ImGui::GetScrollX() + 800.f, row_p.y + ROW_H};

                    // Row background
                    bool   hov     = ImGui::IsMouseHoveringRect(row_p, row_end);
                    if (selected)
                        dl->AddRectFilled(row_p, row_end, COL_SEL);
                    else if (hov)
                        dl->AddRectFilled(row_p, row_end, COL_HOVER);

                    // Row click
                    if (hov && ImGui::IsMouseClicked(0))
                        current_scene->SelectedActorHandle = nodes[ni].Handle;

                    // Double-click rename
                    if (hov && ImGui::IsMouseDoubleClicked(0) && !renaming)
                    {
                        m_renaming_handle = nodes[ni].Handle;
                        Helpers::secure_strncpy(m_rename_buf, sizeof(m_rename_buf), label, sizeof(m_rename_buf) - 1);
                    }

                    // Indent
                    float indent = (e.depth + 1) * INDENT_W; // +1 because everything is under scene root

                    // Arrow area
                    float ax     = row_p.x + indent;
                    ImGui::SetCursorScreenPos({ax, row_p.y});
                    ImGui::PushID((int) nodes[ni].Handle.Index);
                    ImGui::InvisibleButton("##arrow", {ARROW_W, ROW_H});
                    if (ImGui::IsItemClicked() && has_children)
                        ToggleCollapsed(nodes[ni].EID);
                    ImGui::PopID();
                    if (has_children)
                        DrawArrow(dl, {ax, row_p.y}, ROW_H, !collapsed, COL_ARROW);

                    // Icon
                    float ix = ax + ARROW_W + 2.f;
                    DrawTypeIcon(dl, {ix, row_p.y + (ROW_H - ICON_SZ) * 0.5f}, ICON_SZ, type_str, is_collection);

                    // Label / rename
                    float lx = ix + ICON_SZ + 5.f;
                    ImGui::SetCursorScreenPos({lx, row_p.y + (ROW_H - ImGui::GetFrameHeight()) * 0.5f});
                    ImGui::PushID((int) nodes[ni].Handle.Index + 1000);

                    if (renaming)
                    {
                        ImGui::SetNextItemWidth(120.f);
                        if (ImGui::InputText("##ren", m_rename_buf, sizeof(m_rename_buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                        {
                            if (nc_comp && m_rename_buf[0])
                                Helpers::secure_strncpy(nc_comp->Value, sizeof(nc_comp->Value), m_rename_buf, sizeof(m_rename_buf) - 1);
                            m_renaming_handle = {};
                        }
                        else if (!ImGui::IsItemActive() && ImGui::IsItemDeactivated())
                        {
                            if (nc_comp && m_rename_buf[0])
                                Helpers::secure_strncpy(nc_comp->Value, sizeof(nc_comp->Value), m_rename_buf, sizeof(m_rename_buf) - 1);
                            m_renaming_handle = {};
                        }
                        else
                            ImGui::SetKeyboardFocusHere(-1);
                    }
                    else
                    {
                        ImGui::SetCursorScreenPos({lx, row_p.y + (ROW_H - ImGui::GetTextLineHeight()) * 0.5f});
                        dl->AddText(ImGui::GetCursorScreenPos(), selected ? 0xFFFFFFFF : COL_TEXT, label);
                        ImGui::Dummy({ImGui::CalcTextSize(label).x, ImGui::GetTextLineHeight()});
                    }

                    // Drag source
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        ImGui::SetDragDropPayload("ACTOR_REPARENT", &nodes[ni].Handle, sizeof(ActorHandle));
                        ImGui::Text("Move: %s", label);
                        ImGui::EndDragDropSource();
                    }

                    // Drop target
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ACTOR_REPARENT"))
                        {
                            ActorHandle dragged = *reinterpret_cast<const ActorHandle*>(p->Data);
                            if (dragged.Index != nodes[ni].Handle.Index)
                            {
                                pending_reparent_child  = dragged;
                                pending_reparent_parent = nodes[ni].EID;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Context menu
                    if (ImGui::BeginPopupContextItem("##ctx"))
                    {
                        if (nodes[ni].Parent != INVALID_ENTITY && ImGui::MenuItem("Remove from Parent"))
                            pending_remove_parent_handle = nodes[ni].Handle;
                        if (ImGui::MenuItem("Delete"))
                        {
                            auto* mc = actor->GetComponent<MeshComponent>();
                            if (mc && mc->RenderInstanceId != UINT32_MAX)
                                current_scene->RemoveMeshInstance(mc->RenderInstanceId, ctx->RenderResourceManager);
                            if (selected)
                                current_scene->SelectedActorHandle = {};
                            pending_delete = nodes[ni].Handle;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();

                    // Other columns
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ROW_H - ImGui::GetTextLineHeight()) * 0.5f);
                    ImGui::TextDisabled("%s", type_str);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ROW_H - ImGui::GetTextLineHeight()) * 0.5f);
                    ImGui::TextDisabled("Default");

                    // Push children
                    if (has_children && !collapsed)
                    {
                        uint32_t tmp[256];
                        int      tc = 0;
                        uint32_t c  = first_child[ni];
                        while (c != UINT32_MAX && tc < 256)
                        {
                            tmp[tc++] = c;
                            c         = next_sib[c];
                        }
                        for (int ci = tc - 1; ci >= 0; --ci)
                            stk[sp++] = {tmp[ci], e.depth + 1};
                    }
                }
            }

            // Root-level drop zone
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::InvisibleButton("##root_drop_zone", {ImGui::GetContentRegionAvail().x, 8.f});
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ACTOR_REPARENT"))
                {
                    pending_reparent_child  = *reinterpret_cast<const ActorHandle*>(p->Data);
                    pending_reparent_parent = INVALID_ENTITY;
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::EndTable();
        }

        ZReleaseScratch(scratch);

        // Status bar
        ImGui::Separator();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
        if (selected_count > 0)
            ImGui::TextDisabled("%u actor%s (%u selected)", total_actors, total_actors == 1 ? "" : "s", selected_count);
        else
            ImGui::TextDisabled("%u actor%s", total_actors, total_actors == 1 ? "" : "s");

        // Deferred mutations
        if (pending_reparent_child.Valid())
        {
            Actor* child = ctx->ActorManager->Access(pending_reparent_child);
            if (child)
            {
                if (pending_reparent_parent == INVALID_ENTITY)
                    child->RemoveComponent<ParentComponent>();
                else
                {
                    ParentComponent pc_new = {pending_reparent_parent};
                    if (child->HasComponent<ParentComponent>())
                        child->GetComponent<ParentComponent>()->Parent = pending_reparent_parent;
                    else
                        child->AddComponent<ParentComponent>(pc_new);
                }
            }
        }
        if (pending_remove_parent_handle.Valid())
        {
            Actor* a = ctx->ActorManager->Access(pending_remove_parent_handle);
            if (a)
                a->RemoveComponent<ParentComponent>();
        }
        if (pending_delete.Valid())
            ctx->ActorManager->Destroy(pending_delete);

        RenderGuizmo(app, current_scene);
        ImGui::End();
    }

    // Gizmo

    void HierarchyViewUIComponent::RenderGuizmo(EditorPtr app, EditorScenePtr scene)
    {
        if (!app || !app->CameraController || !scene)
            return;

        auto* ctx = Engine::GetContext();
        if (!ctx || !ctx->ActorManager)
            return;

        ActorHandle h     = scene->SelectedActorHandle;
        Actor*      actor = ctx->ActorManager->Access(h);
        if (!actor)
            return;

        auto* tc = actor->GetComponent<TransformComponent>();
        if (!tc)
            return;

        auto camera = app->CameraController->GetCamera();
        if (!camera)
            return;

        const auto view       = camera->GetView();
        auto       projection = camera->GetProjection();
        projection[1][1]      = -projection[1][1];

        Mat4f transform       = tc->WorldTransform;

        int   gizmo_op        = app->Configuration->GizmoOperation;
        float snap_val        = 0.5f;
        bool  snapping        = app->CurrentWindow && IDevice::As<Keyboard>() && IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_CONTROL, app->CurrentWindow);
        if (snapping && static_cast<ImGuizmo::OPERATION>(gizmo_op) == ImGuizmo::ROTATE)
            snap_val = 45.0f;
        float snap_arr[3] = {snap_val, snap_val, snap_val};

        if (gizmo_op > 0)
            ImGuizmo::Manipulate(value_ptr(view), value_ptr(projection), static_cast<ImGuizmo::OPERATION>(gizmo_op), ImGuizmo::MODE::WORLD, value_ptr(transform), nullptr, snapping ? snap_arr : nullptr);

        if (ImGuizmo::IsUsing())
        {
            Mat4f local_mat = transform;
            auto* pc        = actor->GetComponent<ParentComponent>();
            if (pc && pc->Parent != INVALID_ENTITY)
            {
                auto* parent_tc = ctx->Scene->GetComponent<TransformComponent>(pc->Parent);
                if (parent_tc)
                    local_mat = parent_tc->WorldTransform.Inverse() * transform;
            }

            Vec3f new_pos, new_rot, new_scale;
            if (DecomposeTransformComponent(local_mat, new_pos, new_rot, new_scale))
            {
                tc->Position         = new_pos;
                tc->Rotation         = new_rot;
                tc->Scale            = new_scale;
                tc->PreviousPosition = new_pos;
            }
        }
    }
} // namespace Tetragrama::Components
