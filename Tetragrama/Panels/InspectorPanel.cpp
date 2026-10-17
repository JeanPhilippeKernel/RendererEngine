#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Panels/InspectorPanel.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Reflection/ComponentMeta.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>
#include <string>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Helpers;
using namespace ZEngine::UI;

namespace Tetragrama::Panels
{
    static constexpr float kLabelW    = 96.f;

    // Figma "Transform property": 27 px row, 10 px lead, 96 px label, three 22 px axis boxes with a
    // 2 x 15 colored indicator, 6 px gaps, then a 11 px reset icon.
    static constexpr float kRowLead   = 10.f;
    static constexpr float kRowGap    = 6.f;
    static constexpr float kResetSize = 11.f;

    static float           AxisWidth(float pw)
    {
        return fmaxf((pw - kRowLead - kLabelW - kRowGap - kRowGap * 2.f - kRowGap - kResetSize - kRowLead) / 3.f, 32.f);
    }

    static void RowLabel(ZUIContext* ctx, const char* label)
    {
        ZUIBox* lbl   = ZUIPushBox(ctx, "##lbl", 5, ZUI_DrawText);
        lbl->Size[0]  = ZPx(kLabelW);
        lbl->Size[1]  = ZPx(14.f);
        uint32_t llen = (uint32_t) ZEngine::Helpers::secure_strlen(label);
        lbl->Label    = ZUIPushStr(&ctx->FrameArena, label, llen);
        for (int k = 0; k < 4; ++k)
            lbl->TextColor[k] = ctx->Theme.TextDefault[k];
        ZUIPopBox(ctx);
    }

    static bool Vec3Row(ZUIContext* ctx, const char* row_key, const char* label, float* v, float speed, float pw, bool degrees)
    {
        static const float kAxis[3][4] = {ZUI_TOKEN(ZEngine::UI::Tokens::AccentRed), ZUI_TOKEN(ZEngine::UI::Tokens::AccentGreen), ZUI_TOKEN(ZEngine::UI::Tokens::AccentBlue)};
        static const char* kNames[3]   = {"x", "y", "z"};

        const float        w3          = AxisWidth(pw);
        bool               any         = false;

        ZUIBox*            row         = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(27.f));
        row->Padding[0]                = kRowLead;
        row->Padding[2]                = kRowLead;
        row->Gap                       = kRowGap;
        row->CrossAlign                = ZUIAlign::Center;

        RowLabel(ctx, label);

        const float saved_pad  = ctx->Style.FramePadding[0];
        ctx->FlatInputs        = true;
        ctx->DragFloatDecimals = 1;
        ctx->DragFloatSuffix   = degrees ? "\xB0" : nullptr;
        for (int a = 0; a < 3; ++a)
        {
            char ck[56] = {}, bk[56] = {}, fk[56] = {};
            snprintf(ck, sizeof(ck), "##ax%s_%s", kNames[a], row_key + 2);
            snprintf(bk, sizeof(bk), "##bx%s_%s", kNames[a], row_key + 2);
            snprintf(fk, sizeof(fk), "##%s_%s", kNames[a], row_key + 2);
            ZUIBox* cell       = ZUIBeginRow(ctx, ck, ZPx(w3), ZPx(22.f));
            cell->Flags        = cell->Flags | ZUI_DrawBackground;
            cell->Padding[0]   = 4.f;
            cell->CrossAlign   = ZUIAlign::Center;
            cell->EdgeSoftness = 0.5f;
            ZUIBoxSetColorArr(cell, ctx->Theme.InputBg);
            ZUIBoxSetCornerRadius(cell, 3.f);

            ZUIBox* bar  = ZUIPushBox(ctx, bk, (uint32_t) ZEngine::Helpers::secure_strlen(bk), ZUI_DrawBackground);
            bar->Size[0] = ZPx(2.f);
            bar->Size[1] = ZPx(15.f);
            ZUIBoxSetColorArr(bar, kAxis[a]);
            bar->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);

            ctx->Style.FramePadding[0]  = 4.f;
            any                        |= ZUIDragFloat(ctx, fk, &v[a], speed, w3 - 6.f);
            ctx->Style.FramePadding[0]  = saved_pad;
            ZUIEndRow(ctx);
        }
        ctx->FlatInputs        = false;
        ctx->DragFloatDecimals = 3;
        ctx->DragFloatSuffix   = nullptr;

        ZUIIconBox(ctx, ZUIIcon::RotateCcw, kResetSize, ctx->Theme.TextDim);
        ZUIEndRow(ctx);
        return any;
    }

    // Display name of a component type: the registry's "TransformComponent" reads as "Transform".
    static const char* DisplayName(ZUIContext* ctx, const char* type_name)
    {
        const size_t      len       = ZEngine::Helpers::secure_strlen(type_name);
        static const char kSuffix[] = "Component";
        if (len > sizeof(kSuffix) - 1 && ZEngine::Helpers::secure_strcmp(type_name + len - (sizeof(kSuffix) - 1), kSuffix) == 0)
        {
            char* out = (char*) ZUIPushStr(&ctx->FrameArena, type_name, (uint32_t) (len - (sizeof(kSuffix) - 1))).Ptr;
            return out;
        }
        return type_name;
    }

    // Figma "Property section heading": 28 px band, chevron, semibold title, ellipsis.
    static void SectionHeading(ZUIContext* ctx, const char* type_name, bool* open)
    {
        const char* title   = DisplayName(ctx, type_name);
        char        key[96] = {};
        snprintf(key, sizeof(key), "##sec_%s", type_name);
        ZUIBox* row     = ZUIBeginRow(ctx, key, ZFill(), ZPx(28.f));
        row->Flags      = row->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_DrawBorder | ZUI_KeyboardActivate;
        row->Padding[0] = row->Padding[2] = 10.f;
        row->Gap                          = 7.f;
        row->CrossAlign                   = ZUIAlign::Center;
        row->EdgeSoftness                 = 0.f;
        ZUIBoxSetColorArr(row, ctx->Theme.ButtonBg);
        for (int k = 0; k < 4; ++k)
            row->BorderColor[k] = ctx->Theme.PanelBorder[k];
        row->BorderThickness = 0.f;
        ZUIIconBox(ctx, *open ? ZUIIcon::ChevronDown : ZUIIcon::ChevronRight, 11.f, ctx->Theme.TextDefault);
        ZUILabelTight(ctx, title, ctx->Theme.TextDefault, ZUIFontSize::Heading);
        {
            ZUIBox* fill  = ZUIPushBox(ctx, "##sec_fill", 10, ZUI_None);
            fill->Size[0] = ZFill();
            fill->Size[1] = ZPx(1.f);
            ZUIPopBox(ctx);
        }
        ZUIIconBox(ctx, ZUIIcon::Ellipsis, 13.f, ctx->Theme.TextDim);
        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);
        if (sig.Flags & ZUI_SignalClicked)
            *open = !*open;
    }

    // DrawZUIField
    // Widget dispatch by FieldType. Two-column row: dim label (kLabelW) + widget.
    // Matches develop DrawField() translated to ZUI widget calls.
    static bool DrawZUIField(ZUIContext* ctx, const FieldDescriptor& fd, void* comp_data, float pw, uint32_t comp_idx, uint32_t field_idx)
    {
        if (fd.Hidden)
            return false;

        bool  changed     = false;

        void* ptr         = static_cast<char*>(comp_data) + fd.Offset;
        float fh          = ZUIGetFrameHeight(ctx);
        float wid         = fmaxf(pw - kRowLead - kLabelW - kRowGap - kRowGap - kResetSize - kRowLead, 36.f);

        char  row_key[48] = {};
        snprintf(row_key, sizeof(row_key), "##frow_%u_%u", comp_idx, field_idx);

        // Vec3f is handled by Vec3Row (full-row layout with colored bars)
        if (fd.Type == FieldType::Vec3f)
        {
            float* fv         = static_cast<float*>(ptr);

            // Rotation fields stored in radians — display in degrees
            bool   is_radians = (fd.Tooltip && strstr(fd.Tooltip, "radians") != nullptr);
            float  display[3] = {fv[0], fv[1], fv[2]};
            if (is_radians)
            {
                display[0] = fv[0] * 57.2957795f;
                display[1] = fv[1] * 57.2957795f;
                display[2] = fv[2] * 57.2957795f;
            }

            changed = Vec3Row(ctx, row_key, fd.Name, display, is_radians ? 1.0f : 0.05f, pw, is_radians);
            if (changed && is_radians)
            {
                fv[0] = display[0] * 0.01745329f; // deg → rad
                fv[1] = display[1] * 0.01745329f;
                fv[2] = display[2] * 0.01745329f;
            }
            else if (changed)
            {
                fv[0] = display[0];
                fv[1] = display[1];
                fv[2] = display[2];
            }
            return changed;
        }

        // All other types: two-column row (label | widget)
        (void) fh;
        ZUIBox* frow     = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(29.f));
        frow->Padding[0] = kRowLead;
        frow->Padding[2] = kRowLead;
        frow->Gap        = kRowGap;
        frow->CrossAlign = ZUIAlign::Center;
        RowLabel(ctx, fd.Name);
        ctx->FlatInputs = true;

        char wkey[48]   = {};
        snprintf(wkey, sizeof(wkey), "##fv_%u_%u", comp_idx, field_idx);

        switch (fd.Type)
        {
            case FieldType::Bool:
                changed = ZUICheckbox(ctx, wkey, static_cast<bool*>(ptr));
                break;

            case FieldType::Int8:
            case FieldType::Int16:
            case FieldType::Int32:
            {
                int32_t val = 0;
                if (fd.Type == FieldType::Int8)
                    val = *static_cast<int8_t*>(ptr);
                else if (fd.Type == FieldType::Int16)
                    val = *static_cast<int16_t*>(ptr);
                else
                    val = *static_cast<int32_t*>(ptr);
                if (ZUIDragInt(ctx, wkey, &val, 1.f, wid))
                {
                    changed = true;
                    if (fd.Type == FieldType::Int8)
                        *static_cast<int8_t*>(ptr) = (int8_t) val;
                    else if (fd.Type == FieldType::Int16)
                        *static_cast<int16_t*>(ptr) = (int16_t) val;
                    else
                        *static_cast<int32_t*>(ptr) = val;
                }
                break;
            }

            case FieldType::Int64:
            case FieldType::UInt8:
            case FieldType::UInt16:
            case FieldType::UInt32:
            case FieldType::UInt64:
            {
                // Display as read-only formatted string for now
                char buf[32] = {};
                snprintf(buf, sizeof(buf), "%lld", (long long) *(int64_t*) ptr);
                ZUILabel(ctx, buf, ctx->Theme.TextDefault);
                break;
            }

            case FieldType::Float:
            {
                changed = ZUIDragFloat(ctx, wkey, static_cast<float*>(ptr), 0.05f, wid);
                break;
            }

            case FieldType::Double:
            {
                float f = (float) *static_cast<double*>(ptr);
                if (ZUIDragFloat(ctx, wkey, &f, 0.05f, wid))
                {
                    changed                    = true;
                    *static_cast<double*>(ptr) = (double) f;
                }
                break;
            }

            case FieldType::Vec2f:
            {
                float* fv     = static_cast<float*>(ptr);
                float  w2     = fmaxf(wid * 0.5f, 36.f);
                char   xk[48] = {}, yk[48] = {};
                snprintf(xk, sizeof(xk), "##v2x_%u_%u", comp_idx, field_idx);
                snprintf(yk, sizeof(yk), "##v2y_%u_%u", comp_idx, field_idx);
                changed |= ZUIDragFloat(ctx, xk, &fv[0], 0.05f, w2);
                changed |= ZUIDragFloat(ctx, yk, &fv[1], 0.05f, w2);
                break;
            }

            case FieldType::Vec4f:
            {
                float* fv = static_cast<float*>(ptr);
                float  w4 = fmaxf(wid * 0.25f, 28.f);
                for (int ax = 0; ax < 4; ++ax)
                {
                    char ak[48] = {};
                    snprintf(ak, sizeof(ak), "##v4_%d_%u_%u", ax, comp_idx, field_idx);
                    changed |= ZUIDragFloat(ctx, ak, &fv[ax], 0.05f, w4);
                }
                break;
            }

            case FieldType::String:
            {
                uint32_t cap = fd.StringCap > 0 ? fd.StringCap : 256u;
                changed      = ZUITextField(ctx, wkey, static_cast<char*>(ptr), cap, wid);
                break;
            }

            case FieldType::AssetUUID:
            {
                // Read-only UUID — the field is a uuids::uuid (16 raw bytes), not a
                // string, so it must be formatted rather than reinterpreted as one.
                const auto* uuid     = static_cast<const uuids::uuid*>(ptr);
                std::string uuid_str = (uuid && !uuid->is_nil()) ? uuids::to_string(*uuid) : std::string();
                ZUILabel(ctx, uuid_str.empty() ? "(none)" : uuid_str.c_str(), ctx->Theme.TextDefault);
                break;
            }

            case FieldType::Enum:
            {
                // Read current value (size-dispatched)
                int64_t cur = 0;
                switch (fd.Size)
                {
                    case 1:
                        cur = (int64_t) *static_cast<uint8_t*>(ptr);
                        break;
                    case 2:
                        cur = (int64_t) *static_cast<uint16_t*>(ptr);
                        break;
                    case 4:
                        cur = (int64_t) *static_cast<uint32_t*>(ptr);
                        break;
                    case 8:
                        cur = *static_cast<int64_t*>(ptr);
                        break;
                }
                // Find preview label
                const char* preview = "?";
                for (uint32_t ei = 0; ei < fd.EnumCount; ++ei)
                    if (fd.EnumValues[ei].Value == cur)
                    {
                        preview = fd.EnumValues[ei].Name;
                        break;
                    }
                if (ZUIBeginCombo(ctx, wkey, preview, ZPx(wid)))
                {
                    for (uint32_t ei = 0; ei < fd.EnumCount; ++ei)
                    {
                        bool sel = (fd.EnumValues[ei].Value == cur);
                        if (ZUIComboItem(ctx, fd.EnumValues[ei].Name, sel))
                        {
                            int64_t nv  = fd.EnumValues[ei].Value;
                            changed    |= nv != cur;
                            switch (fd.Size)
                            {
                                case 1:
                                    *static_cast<uint8_t*>(ptr) = (uint8_t) nv;
                                    break;
                                case 2:
                                    *static_cast<uint16_t*>(ptr) = (uint16_t) nv;
                                    break;
                                case 4:
                                    *static_cast<uint32_t*>(ptr) = (uint32_t) nv;
                                    break;
                                case 8:
                                    *static_cast<int64_t*>(ptr) = nv;
                                    break;
                            }
                        }
                    }
                    ZUIEndCombo(ctx);
                }
                break;
            }

            default:
                ZUILabel(ctx, "(unsupported)", ctx->Theme.TextDim);
                break;
        }

        ctx->FlatInputs = false;
        ZUIIconBox(ctx, ZUIIcon::RotateCcw, kResetSize, ctx->Theme.TextDim);
        ZUIEndRow(ctx);
        return changed;
    }

    void InspectorPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        // Guard
        if (!m_layer || !m_layer->CurrentApp)
        {
            EmptyPanelBg(ctx, "##insp_empty", ctx->Theme.PanelBg, nullptr);
            return;
        }
        auto* app   = reinterpret_cast<Tetragrama::EditorPtr>(m_layer->CurrentApp);
        auto* scene = reinterpret_cast<Tetragrama::EditorScenePtr>(app->CurrentScene);
        auto* eng   = Engine::GetContext();
        if (!scene || !eng || !eng->ActorManager)
        {
            EmptyPanelBg(ctx, "##insp_empty", ctx->Theme.PanelBg, nullptr);
            return;
        }

        // Initialize section-open state to all-true on first call
        if (!m_sec_init)
        {
            for (int i = 0; i < 64; ++i)
                m_sec_open[i] = true;
            m_sec_init = true;
        }

        float   fh = ZUIGetFrameHeight(ctx);
        float   pw = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 220.f;

        ZUIBox* bg = ZUIBeginColumn(ctx, "##insp_bg", ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
        bg->EdgeSoftness = 0.f;

        // No-selection guard
        Actor* actor     = scene->SelectedActorHandle.Valid() ? eng->ActorManager->Access(scene->SelectedActorHandle) : nullptr;
        if (!actor)
        {
            ZUIBeginScrollRegion(ctx, "##insp_empty_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 12.f);
            {
                ZUIBox* lbl       = ZUIPushBox(ctx, "##no_sel", 8, ZUI_DrawText);
                lbl->Size[0]      = ZFill();
                lbl->Size[1]      = ZText();
                lbl->TextAlign    = ZUITextAlign::Center;
                uint32_t tlen     = 17;
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, "No actor selected", tlen);
                lbl->TextColor[0] = ctx->Theme.TextDim[0];
                lbl->TextColor[1] = ctx->Theme.TextDim[1];
                lbl->TextColor[2] = ctx->Theme.TextDim[2];
                lbl->TextColor[3] = ctx->Theme.TextDim[3];
                ZUIPopBox(ctx);
            }
            ZUIEndScrollRegion(ctx);
            ZUIEndColumn(ctx);
            return;
        }

        ArchetypeMask mask      = actor->GetComponentMask();
        const auto&   registry  = ComponentReflectionRegistry::Get();

        const float   accent[4] = ZUI_TOKEN(ZEngine::UI::Tokens::AccentBlue);

        // Figma "Actor identity": 45 px band, 22 px actor glyph, name over kind, lock icon.
        {
            const char* kind = "Actor";
            if (actor->HasComponent<LightComponent>())
                kind = "Light Actor";
            else if (actor->HasComponent<CameraComponent>())
                kind = "Camera Actor";
            else if (actor->HasComponent<MeshComponent>())
                kind = "Static Mesh Actor";

            ZUIBox* id     = ZUIBeginRow(ctx, "##insp_id", ZFill(), ZPx(45.f));
            id->Padding[0] = id->Padding[2] = 10.f;
            id->Gap                         = 9.f;
            id->CrossAlign                  = ZUIAlign::Center;
            ZUIIconBox(ctx, ZUIIcon::Box, 22.f, accent);

            ZUIBox* col              = ZUIBeginColumn(ctx, "##insp_id_col", ZFill(), ZFit());
            col->Gap                 = 3.f;
            auto*          nc_comp   = actor->GetComponent<NameComponent>();
            const uint64_t name_hash = ZUIHashStr("##actor_name", 12);
            if (m_renaming && nc_comp && ctx->FocusKey != name_hash && ctx->ActiveKey != name_hash && ctx->HotKey != name_hash)
            {
                // Focus left the field on a previous frame: leave edit mode.
                static int s_wait = 0;
                if (++s_wait > 2)
                {
                    m_renaming = false;
                    s_wait     = 0;
                }
            }
            if (m_renaming && nc_comp)
            {
                if (ZUITextField(ctx, "##actor_name", nc_comp->Value, sizeof(nc_comp->Value), fmaxf(pw - 10.f - 22.f - 9.f - 9.f - 13.f - 10.f, 80.f)))
                    scene->MarkDirty(true);
            }
            else
            {
                const char* nm  = (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "Actor";
                ZUIBox*     lbl = ZUILabelTight(ctx, nm, ctx->Theme.TextDefault, ZUIFontSize::Header);
                (void) lbl;
                ZUIBox* hit      = ZUIPushBox(ctx, "##actor_name_hit", 16, ZUI_Clickable | ZUI_FloatX | ZUI_FloatY | ZUI_KeyboardActivate);
                hit->Size[0]     = ZPct(1.f);
                hit->Size[1]     = ZPx(15.f);
                hit->FloatPos[0] = 0.f;
                hit->FloatPos[1] = 0.f;
                ZUISignal hs     = ZUISignalFromBox(ctx, hit);
                ZUIPopBox(ctx);
                if ((hs.Flags & ZUI_SignalClicked) && nc_comp)
                {
                    m_renaming    = true;
                    ctx->FocusKey = name_hash;
                }
            }
            ZUILabelTight(ctx, kind, ctx->Theme.TextDim, ZUIFontSize::Body);
            ZUIEndColumn(ctx);

            ZUIIconBox(ctx, ZUIIcon::LockOpen, 13.f, ctx->Theme.TextDim);
            ZUIEndRow(ctx);
        }

        // Figma "Component actions": Add Component (primary) and Blueprint / Add Script.
        {
            ZUIBox* ca     = ZUIBeginRow(ctx, "##insp_actions", ZFill(), ZPx(31.f));
            ca->Padding[0] = ca->Padding[2] = 10.f;
            ca->Gap                         = 7.f;
            ca->CrossAlign                  = ZUIAlign::Center;
            ZUISignal add                   = ZUIEditorControl(ctx, "##insp_add", ZUIIcon::Plus, "Add Component", false, true, nullptr, false, true);
            ZUIEditorControl(ctx, "##insp_script", ZUIIcon::Code, "Blueprint / Add Script", false);
            ZUIEndRow(ctx);
            if (add.Flags & ZUI_SignalClicked)
            {
                uint64_t            btn_key = ZUIHashStr("##insp_add", 10);
                ZUIPersistentState* ps      = ZUIStateGetOrInsert(&ctx->StateStore, btn_key);
                float               py      = (ps && ps->ScreenMaxY > 0.f) ? ps->ScreenMaxY : ctx->MousePos[1];
                float               px      = (ps && ps->ScreenMinX > 0.f) ? ps->ScreenMinX : ctx->MousePos[0];
                ZUIOpenPopup(ctx, "##add_component_popup", px, py);
            }

            if (ZUIBeginPopup(ctx, "##add_component_popup"))
            {
                bool any = false;
                registry.ForEach([&](const ComponentMeta& meta) {
                    if (MaskHas(mask, meta.TypeID))
                        return; // already on the actor
                    if (!meta.Add)
                        return; // display-only, cannot be constructed
                    any                 = true;
                    char add_label[160] = {};
                    snprintf(add_label, sizeof(add_label), "%s##add_%s", DisplayName(ctx, meta.TypeName), meta.TypeName);
                    if (ZUIMenuItemEx(ctx, add_label, nullptr))
                    {
                        eng->Scene->AddComponentRaw(actor->GetEntityID(), meta.TypeID);
                        if (MaskHas(actor->GetComponentMask(), meta.TypeID))
                            scene->MarkDirty(true);
                    }
                });

                if (!any)
                    ZUIMenuItem(ctx, "No components left to add", false);

                ZUIEndPopup(ctx);
            }
        }

        // Figma "Actor component tree": the actor and the components attached to it.
        {
            auto*       nc_comp   = actor->GetComponent<NameComponent>();
            const char* nm        = (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "Actor";
            char        self[160] = {};
            snprintf(self, sizeof(self), "%s (Self)", nm);

            ZUIBox* tree     = ZUIBeginColumn(ctx, "##insp_tree", ZFill(), ZFit());
            tree->Padding[0] = 12.f;
            tree->Padding[1] = 7.f;
            tree->Padding[3] = 7.f;
            tree->Gap        = 6.f;
            ZUIBox* r0       = ZUIBeginRow(ctx, "##insp_tree_self", ZFill(), ZPx(12.f));
            r0->Gap          = 7.f;
            r0->CrossAlign   = ZUIAlign::Center;
            ZUIIconBox(ctx, ZUIIcon::ChevronDown, 10.f, ctx->Theme.TextDim);
            ZUIIconBox(ctx, ZUIIcon::Box, 12.f, ctx->Theme.TextDim);
            ZUILabelTight(ctx, self, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUIEndRow(ctx);
            uint32_t tree_n = 0;
            registry.ForEach([&](const ComponentMeta& meta) {
                if (!MaskHas(mask, meta.TypeID) || tree_n >= 1 || ZEngine::Helpers::secure_strcmp(meta.TypeName, "NameComponent") == 0 || ZEngine::Helpers::secure_strcmp(meta.TypeName, "UUIDComponent") == 0 || ZEngine::Helpers::secure_strcmp(meta.TypeName, "TransformComponent") == 0)
                    return;
                char tk[48] = {};
                snprintf(tk, sizeof(tk), "##insp_tree_%u", tree_n++);
                ZUIBox* rc     = ZUIBeginRow(ctx, tk, ZFill(), ZPx(12.f));
                rc->Padding[0] = 22.f;
                rc->Gap        = 7.f;
                rc->CrossAlign = ZUIAlign::Center;
                ZUIIconBox(ctx, ZUIIcon::Box, 12.f, ctx->Theme.TextDim);
                ZUILabelTight(ctx, DisplayName(ctx, meta.TypeName), ctx->Theme.TextDefault, ZUIFontSize::Body);
                ZUIEndRow(ctx);
            });
            ZUIEndColumn(ctx);
        }

        // Figma "Details search": 35 px band; the filter icon picks the category (replaces the pill row).
        {
            ZUIBox* sr     = ZUIBeginRow(ctx, "##insp_search_row", ZFill(), ZPx(35.f));
            sr->Padding[0] = sr->Padding[2] = 10.f;
            sr->Gap                         = 8.f;
            sr->CrossAlign                  = ZUIAlign::Center;
            ZUISearchBox(ctx, "##insp_search", m_search, sizeof(m_search), "Search details", ZFill());
            ZUIBox* fl      = ZUIPushBox(ctx, "##insp_cat_btn", 13, ZUI_DrawIcon | ZUI_Clickable | ZUI_KeyboardActivate);
            fl->Size[0]     = ZPx(14.f);
            fl->Size[1]     = ZPx(14.f);
            fl->IconId      = (uint16_t) ZUIIcon::ListFilter;
            const float* fc = (ZEngine::Helpers::secure_strcmp(m_category, "All") == 0) ? ctx->Theme.TextDim : accent;
            for (int k = 0; k < 4; ++k)
                fl->TextColor[k] = fc[k];
            ZUISignal fs = ZUISignalFromBox(ctx, fl);
            ZUIPopBox(ctx);
            ZUIEndRow(ctx);
            if (fs.Flags & ZUI_SignalClicked)
                ZUIOpenPopup(ctx, "##insp_cat_popup");

            if (ZUIBeginPopup(ctx, "##insp_cat_popup"))
            {
                if (ZUIMenuItemEx(ctx, "All", nullptr, ZEngine::Helpers::secure_strcmp(m_category, "All") == 0))
                    ZEngine::Helpers::secure_strncpy(m_category, sizeof(m_category), "All", 3);
                static constexpr int kMaxCats       = 16;
                const char*          cats[kMaxCats] = {};
                int                  cat_n          = 0;
                registry.ForEach([&](const ComponentMeta& m) {
                    if (!MaskHas(mask, m.TypeID) || !m.Category)
                        return;
                    for (int ci = 0; ci < cat_n; ++ci)
                        if (cats[ci] && ZEngine::Helpers::secure_strcmp(cats[ci], m.Category) == 0)
                            return;
                    if (cat_n < kMaxCats)
                        cats[cat_n++] = m.Category;
                });
                for (int ci = 0; ci < cat_n; ++ci)
                    if (ZUIMenuItemEx(ctx, cats[ci], nullptr, ZEngine::Helpers::secure_strcmp(m_category, cats[ci]) == 0))
                        ZEngine::Helpers::secure_strncpy(m_category, sizeof(m_category), cats[ci], sizeof(m_category) - 1);
                ZUIEndPopup(ctx);
            }
        }

        // Scroll region
        ZUIBox* scroll = ZUIBeginScrollRegion(ctx, "##insp_scroll", ZFill(), ZFill());
        (void) scroll;

        // Reflection-driven component sections
        uint32_t comp_idx = 0;

        // HOT PATH — runs every frame, no heap allocation allowed.
        registry.ForEach([&](const ComponentMeta& meta) {
            if (!MaskHas(mask, meta.TypeID))
                return;

            // Category pill filter
            bool all_cats = (ZEngine::Helpers::secure_strcmp(m_category, "All") == 0);
            if (!all_cats && (!meta.Category || ZEngine::Helpers::secure_strcmp(meta.Category, m_category) != 0))
            {
                ++comp_idx;
                return;
            }

            // Search filter on component name
            if (m_search[0])
            {
                bool name_matches = (strstr(meta.TypeName, m_search) != nullptr);
                // Also check if any field name matches
                bool field_match  = false;
                for (uint32_t fi = 0; fi < meta.FieldCount && !field_match; ++fi)
                    if (!meta.Fields[fi].Hidden && strstr(meta.Fields[fi].Name, m_search))
                        field_match = true;
                if (!name_matches && !field_match)
                {
                    ++comp_idx;
                    return;
                }
            }

            void* comp_data = actor->GetComponentRaw(meta.TypeID);
            if (!comp_data)
            {
                ++comp_idx;
                return;
            }

            uint32_t open_idx = meta.TypeID < 64u ? meta.TypeID : 0u;
            SectionHeading(ctx, meta.TypeName, &m_sec_open[open_idx]);

            if (m_sec_open[open_idx])
            {
                for (uint32_t fi = 0; fi < meta.FieldCount; ++fi)
                    if (DrawZUIField(ctx, meta.Fields[fi], comp_data, pw, comp_idx, fi))
                        scene->MarkDirty(true);
            }

            ++comp_idx;
        });

        ZUIEndScrollRegion(ctx);
        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Panels
