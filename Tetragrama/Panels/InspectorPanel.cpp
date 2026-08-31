#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Panels/InspectorPanel.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Reflection/ComponentMeta.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Helpers;
using namespace ZEngine::UI;

namespace Tetragrama::Panels
{
    static constexpr float kLabelW = 96.f;

    // Vec3Row
    // Three DragFloats with colored 3px left-edge bars (R=red, G=green, B=blue).
    // Matches develop Vec3Row() + ImDrawList colored bar approach.
    static bool            Vec3Row(ZUIContext* ctx, const char* row_key, const char* label, float* v, float speed, float pw)
    {
        // Axis bar colors — matches develop: R(215,90,80) G(100,200,110) B(90,140,230)
        static const float kBarR[4] = {0.84f, 0.35f, 0.31f, 1.f};
        static const float kBarG[4] = {0.39f, 0.78f, 0.43f, 1.f};
        static const float kBarB[4] = {0.35f, 0.55f, 0.90f, 1.f};

        const float        fh       = ZUIGetFrameHeight(ctx);
        // Layout: [8 lead][label][3 bar][w3][4 gap][3 bar][w3][4 gap][3 bar][w3][8 trail]
        // Fixed overhead (excl. label): 8+3+4+3+4+3+8 = 33px
        const float        w3       = fmaxf((pw - kLabelW - 33.f) / 3.f, 32.f);
        bool               any      = false;

        ZUIBeginRow(ctx, row_key, ZFill(), ZPx(fh));
        ZUISpacer(ctx, 8.f);

        // Label
        {
            ZUIBox* lbl       = ZUIPushBox(ctx, "##lbl", 5, ZUI_DrawText);
            lbl->Size[0]      = ZPx(kLabelW);
            lbl->Size[1]      = ZPx(fh);
            uint32_t llen     = (uint32_t) strlen(label);
            lbl->Label        = ZUIPushStr(&ctx->FrameArena, label, llen);
            lbl->TextColor[0] = ctx->Theme.TextDim[0];
            lbl->TextColor[1] = ctx->Theme.TextDim[1];
            lbl->TextColor[2] = ctx->Theme.TextDim[2];
            lbl->TextColor[3] = ctx->Theme.TextDim[3];
            ZUIPopBox(ctx);
        }

        // X — thin 3px colored bar + DragFloat
        {
            char bk[48], fk[48];
            snprintf(bk, sizeof(bk), "##bx_%s", row_key + 2);
            snprintf(fk, sizeof(fk), "##x_%s", row_key + 2);
            ZUIBox* bar  = ZUIPushBox(ctx, bk, (uint32_t) strlen(bk), ZUI_DrawBackground);
            bar->Size[0] = ZPx(3.f);
            bar->Size[1] = ZPx(fh);
            ZUIBoxSetColorArr(bar, kBarR);
            bar->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
            any |= ZUIDragFloat(ctx, fk, &v[0], speed, w3);
        }
        ZUISpacer(ctx, 4.f); // gap between X and Y groups

        // Y — thin 3px colored bar + DragFloat
        {
            char bk[48], fk[48];
            snprintf(bk, sizeof(bk), "##by_%s", row_key + 2);
            snprintf(fk, sizeof(fk), "##y_%s", row_key + 2);
            ZUIBox* bar  = ZUIPushBox(ctx, bk, (uint32_t) strlen(bk), ZUI_DrawBackground);
            bar->Size[0] = ZPx(3.f);
            bar->Size[1] = ZPx(fh);
            ZUIBoxSetColorArr(bar, kBarG);
            bar->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
            any |= ZUIDragFloat(ctx, fk, &v[1], speed, w3);
        }
        ZUISpacer(ctx, 4.f); // gap between Y and Z groups

        // Z — thin 3px colored bar + DragFloat
        {
            char bk[48], fk[48];
            snprintf(bk, sizeof(bk), "##bz_%s", row_key + 2);
            snprintf(fk, sizeof(fk), "##z_%s", row_key + 2);
            ZUIBox* bar  = ZUIPushBox(ctx, bk, (uint32_t) strlen(bk), ZUI_DrawBackground);
            bar->Size[0] = ZPx(3.f);
            bar->Size[1] = ZPx(fh);
            ZUIBoxSetColorArr(bar, kBarB);
            bar->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
            any |= ZUIDragFloat(ctx, fk, &v[2], speed, w3);
        }
        ZUISpacer(ctx, 8.f); // trailing

        ZUIEndRow(ctx);
        return any;
    }

    // DrawZUIField
    // Widget dispatch by FieldType. Two-column row: dim label (kLabelW) + widget.
    // Matches develop DrawField() translated to ZUI widget calls.
    static void DrawZUIField(ZUIContext* ctx, const FieldDescriptor& fd, void* comp_data, float pw, uint32_t comp_idx, uint32_t field_idx)
    {
        if (fd.Hidden)
            return;

        void* ptr = static_cast<char*>(comp_data) + fd.Offset;
        float fh  = ZUIGetFrameHeight(ctx);
        float wid = fmaxf(pw - kLabelW - 24.f, 36.f); // 8px leading + 8px trailing

        // Vertical breathing room — same 3px gap above every field row,
        // current and future types all get it for free here.
        ZUISpacer(ctx, 3.f);

        char row_key[48];
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

            if (Vec3Row(ctx, row_key, fd.Name, display, is_radians ? 1.0f : 0.05f, pw) && is_radians)
            {
                fv[0] = display[0] * 0.01745329f; // deg → rad
                fv[1] = display[1] * 0.01745329f;
                fv[2] = display[2] * 0.01745329f;
            }
            else if (!is_radians)
            {
                fv[0] = display[0];
                fv[1] = display[1];
                fv[2] = display[2];
            }
            return;
        }

        // All other types: two-column row (label | widget)
        ZUIBeginRow(ctx, row_key, ZFill(), ZPx(fh));
        ZUISpacer(ctx, 8.f); // leading indent — aligns with Vec3Row

        {
            ZUIBox* lbl       = ZUIPushBox(ctx, "##lbl", 5, ZUI_DrawText);
            lbl->Size[0]      = ZPx(kLabelW);
            lbl->Size[1]      = ZPx(fh);
            uint32_t llen     = (uint32_t) strlen(fd.Name);
            lbl->Label        = ZUIPushStr(&ctx->FrameArena, fd.Name, llen);
            lbl->TextColor[0] = ctx->Theme.TextDim[0];
            lbl->TextColor[1] = ctx->Theme.TextDim[1];
            lbl->TextColor[2] = ctx->Theme.TextDim[2];
            lbl->TextColor[3] = ctx->Theme.TextDim[3];
            ZUIPopBox(ctx);
        }

        char wkey[48];
        snprintf(wkey, sizeof(wkey), "##fv_%u_%u", comp_idx, field_idx);

        switch (fd.Type)
        {
            case FieldType::Bool:
                ZUICheckbox(ctx, wkey, static_cast<bool*>(ptr));
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
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long) *(int64_t*) ptr);
                ZUILabel(ctx, buf, ctx->Theme.TextDefault);
                break;
            }

            case FieldType::Float:
            {
                ZUIDragFloat(ctx, wkey, static_cast<float*>(ptr), 0.05f, wid);
                break;
            }

            case FieldType::Double:
            {
                float f = (float) *static_cast<double*>(ptr);
                if (ZUIDragFloat(ctx, wkey, &f, 0.05f, wid))
                    *static_cast<double*>(ptr) = (double) f;
                break;
            }

            case FieldType::Vec2f:
            {
                float* fv = static_cast<float*>(ptr);
                float  w2 = fmaxf(wid * 0.5f, 36.f);
                char   xk[48], yk[48];
                snprintf(xk, sizeof(xk), "##v2x_%u_%u", comp_idx, field_idx);
                snprintf(yk, sizeof(yk), "##v2y_%u_%u", comp_idx, field_idx);
                ZUIDragFloat(ctx, xk, &fv[0], 0.05f, w2);
                ZUIDragFloat(ctx, yk, &fv[1], 0.05f, w2);
                break;
            }

            case FieldType::Vec4f:
            {
                float* fv = static_cast<float*>(ptr);
                float  w4 = fmaxf(wid * 0.25f, 28.f);
                for (int ax = 0; ax < 4; ++ax)
                {
                    char ak[48];
                    snprintf(ak, sizeof(ak), "##v4_%d_%u_%u", ax, comp_idx, field_idx);
                    ZUIDragFloat(ctx, ak, &fv[ax], 0.05f, w4);
                }
                break;
            }

            case FieldType::String:
            {
                uint32_t cap = fd.StringCap > 0 ? fd.StringCap : 256u;
                ZUITextField(ctx, wkey, static_cast<char*>(ptr), cap, wid);
                break;
            }

            case FieldType::AssetUUID:
            {
                // Read-only UUID string
                const char* uuid_str = static_cast<const char*>(ptr);
                ZUILabel(ctx, uuid_str && uuid_str[0] ? uuid_str : "(none)", ctx->Theme.TextDefault);
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
                            int64_t nv = fd.EnumValues[ei].Value;
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

        ZUISpacer(ctx, 8.f); // trailing — matches leading
        ZUIEndRow(ctx);
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

        // Actor header — name editing
        {
            ZUIBox* hdr = ZUIBeginColumn(ctx, "##insp_hdr", ZFill(), ZPx(fh + 16.f));
            hdr->Flags  = hdr->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(hdr, 0.18f, 0.18f, 0.22f, 1.f);
            hdr->EdgeSoftness = 0.f;
            hdr->Padding[1]   = 6.f; // top padding — vertically centers the field
            hdr->Padding[3]   = 6.f; // bottom padding

            auto* nc_comp     = actor->GetComponent<NameComponent>();
            ZUIBeginRow(ctx, "##insp_hdr_r", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            if (nc_comp)
                ZUITextField(ctx, "##actor_name", nc_comp->Value, sizeof(nc_comp->Value), fmaxf(pw - 24.f, 80.f));
            else
                ZUILabel(ctx, "Actor", ctx->Theme.TextDefault);
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);

            ZUIEndColumn(ctx);
        }

        // Search bar
        ZUISpacer(ctx, 4.f);
        ZUIBeginRow(ctx, "##insp_search_row", ZFill(), ZPx(fh));
        ZUISpacer(ctx, 8.f);
        ZUISearchBox(ctx, "##insp_search", m_search, sizeof(m_search), "Search Details...", ZFill());
        ZUISpacer(ctx, 8.f);
        ZUIEndRow(ctx);
        ZUISpacer(ctx, 4.f);

        // Category pill buttons (UE5-style filter row)
        // Collect unique categories for components on this actor
        {
            static constexpr int kMaxCats       = 16;
            const char*          cats[kMaxCats] = {};
            int                  cat_n          = 0;
            const ArchetypeMask  cat_mask       = actor->GetComponentMask();
            const auto&          cat_reg        = ComponentReflectionRegistry::Get();

            cat_reg.ForEach([&](const ComponentMeta& m) {
                if (!MaskHas(cat_mask, m.TypeID) || !m.Category)
                    return;
                // Check not already in list
                for (int ci = 0; ci < cat_n; ++ci)
                    if (cats[ci] && strcmp(cats[ci], m.Category) == 0)
                        return;
                if (cat_n < kMaxCats)
                    cats[cat_n++] = m.Category;
            });

            if (cat_n > 0)
            {
                ZUISpacer(ctx, 4.f);
                // Pill button colors
                static const float kPillAct[4]  = {0.22f, 0.63f, 0.69f, 1.f}; // teal — active
                static const float kPillHov[4]  = {0.28f, 0.28f, 0.33f, 1.f}; // slightly brighter — hover
                static const float kPillRest[4] = {0.20f, 0.20f, 0.24f, 1.f}; // dark — inactive

                ZUIBeginRow(ctx, "##cat_pills", ZFill(), ZPx(fh + 4.f));
                ZUISpacer(ctx, 6.f);

                // "All" pill
                {
                    bool    is_all = (strcmp(m_category, "All") == 0);
                    ZUIBox* btn    = ZUIPushBox(ctx, "##cpAll", 7, ZUI_Clickable | ZUI_DrawBackground | ZUI_DrawText);
                    float   tw     = 28.f;
                    btn->Size[0]   = ZPx(tw);
                    btn->Size[1]   = ZPx(fh);
                    btn->Label     = ZUIPushStr(&ctx->FrameArena, "All", 3);
                    btn->TextAlign = ZUITextAlign::Center;
                    bool pill_hov  = !is_all && (ctx->HotKey == btn->Key);
                    ZUIBoxSetColorArr(btn, is_all ? kPillAct : pill_hov ? kPillHov : kPillRest);
                    ZUIBoxSetCornerRadius(btn, 3.f);
                    btn->TextColor[0] = btn->TextColor[1] = btn->TextColor[2] = 1.f;
                    btn->TextColor[3]                                         = 1.f;
                    ZUISignal sig                                             = ZUISignalFromBox(ctx, btn);
                    ZUIPopBox(ctx);
                    if (sig.Flags & ZUI_SignalClicked)
                        ZEngine::Helpers::secure_strncpy(m_category, sizeof(m_category), "All", 3);
                    ZUISpacer(ctx, 4.f);
                }

                // One pill per category
                for (int ci = 0; ci < cat_n; ++ci)
                {
                    if (!cats[ci])
                        continue;
                    bool is_active = (strcmp(m_category, cats[ci]) == 0);
                    char pill_key[48];
                    snprintf(pill_key, sizeof(pill_key), "##cp_%d", ci);
                    uint32_t clen   = (uint32_t) strlen(cats[ci]);
                    // Approximate pill width from name length
                    float    pill_w = fmaxf((float) clen * 7.f + 10.f, 40.f);

                    ZUIBox*  btn    = ZUIPushBox(ctx, pill_key, (uint32_t) strlen(pill_key), ZUI_Clickable | ZUI_DrawBackground | ZUI_DrawText);
                    btn->Size[0]    = ZPx(pill_w);
                    btn->Size[1]    = ZPx(fh);
                    btn->Label      = ZUIPushStr(&ctx->FrameArena, cats[ci], clen);
                    btn->TextAlign  = ZUITextAlign::Center;
                    bool pill_hov   = !is_active && (ctx->HotKey == btn->Key);
                    ZUIBoxSetColorArr(btn, is_active ? kPillAct : pill_hov ? kPillHov : kPillRest);
                    ZUIBoxSetCornerRadius(btn, 3.f);
                    btn->TextColor[0] = btn->TextColor[1] = btn->TextColor[2] = 1.f;
                    btn->TextColor[3]                                         = 1.f;
                    ZUISignal sig                                             = ZUISignalFromBox(ctx, btn);
                    ZUIPopBox(ctx);
                    if (sig.Flags & ZUI_SignalClicked)
                        ZEngine::Helpers::secure_strncpy(m_category, sizeof(m_category), cats[ci], sizeof(m_category) - 1);
                    ZUISpacer(ctx, 4.f);
                }
                ZUIEndRow(ctx);
                ZUISpacer(ctx, 4.f);
            }
        }

        ZUISeparator(ctx);

        // Scroll region
        ZUIBox* scroll = ZUIBeginScrollRegion(ctx, "##insp_scroll", ZFill(), ZFill());
        ZUIPaddingXY(scroll, 0.f, 4.f); // 4px top/bottom breathing room

        // Reflection-driven component sections
        ArchetypeMask mask     = actor->GetComponentMask();
        uint32_t      comp_idx = 0;
        const auto&   registry = ComponentReflectionRegistry::Get();

        // HOT PATH — runs every frame, no heap allocation allowed.
        registry.ForEach([&](const ComponentMeta& meta) {
            if (!MaskHas(mask, meta.TypeID))
                return;

            // Category pill filter
            bool all_cats = (strcmp(m_category, "All") == 0);
            if (!all_cats && (!meta.Category || strcmp(meta.Category, m_category) != 0))
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
            ZUICollapsingHeader(ctx, meta.TypeName, &m_sec_open[open_idx]);

            if (m_sec_open[open_idx])
            {
                ZUISpacer(ctx, 6.f);
                for (uint32_t fi = 0; fi < meta.FieldCount; ++fi)
                    DrawZUIField(ctx, meta.Fields[fi], comp_data, pw, comp_idx, fi);
                ZUISpacer(ctx, 6.f);
                ZUISeparator(ctx);
            }
            ZUISpacer(ctx, 2.f); // small gap between sections

            ++comp_idx;
        });

        ZUIEndScrollRegion(ctx);
        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Panels
