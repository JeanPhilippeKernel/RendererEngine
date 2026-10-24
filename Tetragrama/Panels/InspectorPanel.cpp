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
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Helpers;
using namespace ZEngine::UI;

namespace Tetragrama::Panels
{
    static constexpr float kLabelW       = 96.f;
    static constexpr float kPropertyRowH = 29.f;

    // Figma "Transform property": 27 px row, 10 px lead, 96 px label, three 22 px axis boxes with a
    // 2 x 15 colored indicator, 6 px gaps, then a 11 px reset icon.
    static constexpr float kRowLead      = 10.f;
    static constexpr float kRowGap       = 6.f;
    static constexpr float kResetSize    = 11.f;

    static float           AxisWidth(float pw)
    {
        return fmaxf((pw - kRowLead - kLabelW - kRowGap - kRowGap * 2.f - kRowGap - kResetSize - kRowLead) / 3.f, 32.f);
    }

    static void RowLabel(ZUIContext* ctx, const char* label, ZUISize width = ZPx(kLabelW))
    {
        ZUIBox* lbl     = ZUIPushBox(ctx, "##lbl", 5, ZUI_DrawText);
        lbl->Size[0]    = width;
        lbl->Size[1]    = ZPx(14.f);
        // Match the renderer's standard text inset, including it in text-sized labels.
        lbl->Padding[0] = ctx->Style.FramePadding[0];
        uint32_t llen   = (uint32_t) ZEngine::Helpers::secure_strlen(label);
        lbl->Label      = ZUIPushStr(&ctx->FrameArena, label, llen);
        for (int k = 0; k < 4; ++k)
        {
            lbl->TextColor[k] = ctx->Theme.TextDefault[k];
        }
        ZUIPopBox(ctx);
    }

    // Widgets own presentation/text buffers, but a session owns authored pointer
    // motion and termination. Never feed an ECS address to a numeric widget here.
    static bool NumericFloat(ZUIContext* ctx, const char* key, float* value, float speed, float width, EditorSession* session = nullptr, EntityID entity = {}, const EditorControlDrag* control = nullptr)
    {
        if (!session || !control)
        {
            return ZUIDragFloat(ctx, key, value, speed, width);
        }
        const uint64_t hash      = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
        const auto*    state     = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        const bool     text_mode = ctx->FocusKey == hash && state && state->UserData >= 0.f;
        float          candidate = *value;
        const bool     changed   = ZUIDragFloat(ctx, key, &candidate, speed, width);
        if (ctx->Disabled)
        {
            return false;
        }
        if (ctx->ActiveKey == hash && ctx->MouseDown[0] && !text_mode && !ctx->CtrlDown)
        {
            if (!session->Active())
            {
                auto drag          = *control;
                drag.Key           = hash;
                drag.PointerSerial = ctx->MousePressSerial[0];
                drag.StartX        = ctx->MousePressPos[0][0];
                if (!session->RequestControlDrag(entity, drag))
                    ZENGINE_CORE_WARN("[Inspector] {}", session->LastError())
            }
            return false;
        }
        // Pointer deltas are applied by the update loop, never a second time
        // by this widget. Confirmed text and keyboard steps are discrete edits.
        if (session->Active() || ctx->MouseDown[0])
        {
            return false;
        }
        if (changed)
        {
            *value = candidate;
        }
        return changed;
    }

    static bool Vec3Row(ZUIContext* ctx, const char* row_key, const char* label, float* v, float speed, float pw, bool degrees, EditorSession* session = nullptr, EntityID entity = {}, const EditorControlDrag* control = nullptr)
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

        const bool  flat_inputs = ctx->FlatInputs;
        const int   decimals    = ctx->DragFloatDecimals;
        const char* suffix      = ctx->DragFloatSuffix;
        ctx->FlatInputs         = true;
        ctx->DragFloatDecimals  = 1;
        ctx->DragFloatSuffix    = degrees ? "\xB0" : nullptr;
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

            ZUIStylePushFloat(ctx, ZUIStyleVar_FramePaddingX, 4.f);
            EditorControlDrag axis_control  = control ? *control : EditorControlDrag{};
            axis_control.Axis               = static_cast<uint8_t>(a);
            any                            |= NumericFloat(ctx, fk, &v[a], speed, w3 - 6.f, session, entity, control ? &axis_control : nullptr);
            ZUIStylePop(ctx);
            ZUIEndRow(ctx);
        }
        ctx->FlatInputs        = flat_inputs;
        ctx->DragFloatDecimals = decimals;
        ctx->DragFloatSuffix   = suffix;

        if (!ctx->Disabled)
        {
            ZUIIconBox(ctx, ZUIIcon::RotateCcw, kResetSize, ctx->Theme.TextDim);
        }
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
        {
            row->BorderColor[k] = ctx->Theme.PanelBorder[k];
        }
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
        {
            *open = !*open;
        }
    }

    // DrawField
    // Widget dispatch by FieldType. Two-column row: property label + widget.
    // Matches develop DrawField() translated to ZUI widget calls.
    bool InspectorPanel::DrawField(ZUIContext* ctx, const FieldDescriptor& fd, void* comp_data, float pw, uint32_t comp_idx, uint32_t field_idx, EditorSession* session, EntityID entity, const EditorControlDrag* control, uintptr_t color_binding)
    {
        if (fd.Hidden || !comp_data)
        {
            return false;
        }

        bool       changed  = false;
        const bool disabled = ctx->Disabled;
        ctx->Disabled       = disabled || fd.ReadOnly;

        void* ptr           = static_cast<char*>(comp_data) + fd.Offset;
        float wid           = fmaxf(pw - kRowLead - kLabelW - kRowGap - kRowGap - kResetSize - kRowLead, 36.f);

        char  row_key[48]   = {};
        snprintf(row_key, sizeof(row_key), "##frow_%u_%u", comp_idx, field_idx);
        char wkey[48] = {};
        snprintf(wkey, sizeof(wkey), "##fv_%u_%u", comp_idx, field_idx);

        if (fd.Type == FieldType::Color3f)
        {
            // Match inspector label styling, but keep its width compact so RGB
            // fields share the remaining space even in narrow panels.
            // Presentation storage may have an explicit semantic binding identity.
            auto* row       = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(kPropertyRowH));
            row->Padding[0] = row->Padding[2] = kRowLead;
            row->Gap                          = kRowGap;
            row->CrossAlign                   = ZUIAlign::Center;
            RowLabel(ctx, fd.Name, ZText());
            changed       = ZUIColorEdit3(ctx, wkey, static_cast<float*>(ptr), ZUIColorSpace::Linear, ZFill(), color_binding);
            ctx->Disabled = disabled;
            ZUIEndRow(ctx);
            return changed;
        }

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

            const float before_display[3] = {display[0], display[1], display[2]};
            changed                       = Vec3Row(ctx, row_key, fd.Name, display, is_radians ? 1.0f : 0.05f, pw, is_radians, session, entity, control);
            // Do not round-trip untouched axes through degree conversion. The
            // authored value must change only where the user edited the control.
            if (changed)
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    if (display[axis] != before_display[axis])
                    {
                        fv[axis] = is_radians ? display[axis] * 0.01745329f : display[axis];
                    }
                }
            }
            ctx->Disabled = disabled;
            return changed;
        }

        // All other types: two-column row (label | widget)
        ZUIBox* frow     = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(kPropertyRowH));
        frow->Padding[0] = kRowLead;
        frow->Padding[2] = kRowLead;
        frow->Gap        = kRowGap;
        frow->CrossAlign = ZUIAlign::Center;
        RowLabel(ctx, fd.Name);
        const bool flat_inputs = ctx->FlatInputs;
        ctx->FlatInputs        = true;

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
                {
                    val = *static_cast<int8_t*>(ptr);
                }
                else if (fd.Type == FieldType::Int16)
                {
                    val = *static_cast<int16_t*>(ptr);
                }
                else
                {
                    val = *static_cast<int32_t*>(ptr);
                }
                const int32_t original = val;
                if (ZUIDragInt(ctx, wkey, &val, 1.f, wid))
                {
                    if (fd.Type == FieldType::Int8)
                    {
                        val                        = std::clamp(val, static_cast<int32_t>(std::numeric_limits<int8_t>::min()), static_cast<int32_t>(std::numeric_limits<int8_t>::max()));
                        *static_cast<int8_t*>(ptr) = static_cast<int8_t>(val);
                    }
                    else if (fd.Type == FieldType::Int16)
                    {
                        val                         = std::clamp(val, static_cast<int32_t>(std::numeric_limits<int16_t>::min()), static_cast<int32_t>(std::numeric_limits<int16_t>::max()));
                        *static_cast<int16_t*>(ptr) = static_cast<int16_t>(val);
                    }
                    else
                    {
                        *static_cast<int32_t*>(ptr) = val;
                    }
                    changed = val != original;
                }
                break;
            }

            case FieldType::Int64:
            {
                char buf[32] = {};
                snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(*static_cast<const int64_t*>(ptr)));
                ZUILabel(ctx, buf, ctx->Theme.TextDefault);
                break;
            }

            case FieldType::UInt8:
            case FieldType::UInt16:
            case FieldType::UInt32:
            case FieldType::UInt64:
            {
                // Match the actual storage width. Reading a small property as
                // int64_t overreads adjacent fields and misformats UInt64.
                uint64_t value = 0;
                switch (fd.Type)
                {
                    case FieldType::UInt8:
                        value = *static_cast<const uint8_t*>(ptr);
                        break;
                    case FieldType::UInt16:
                        value = *static_cast<const uint16_t*>(ptr);
                        break;
                    case FieldType::UInt32:
                        value = *static_cast<const uint32_t*>(ptr);
                        break;
                    default:
                        value = *static_cast<const uint64_t*>(ptr);
                        break;
                }
                char buf[32] = {};
                snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
                ZUILabel(ctx, buf, ctx->Theme.TextDefault);
                break;
            }

            case FieldType::Float:
            {
                changed = NumericFloat(ctx, wkey, static_cast<float*>(ptr), 0.05f, wid, session, entity, control);
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
                // Never infer more writable space than the reflected storage.
                // Malformed unterminated data must not reach strlen in a widget.
                const uint32_t cap = fd.StringCap > 0 ? std::min(fd.StringCap, fd.Size) : fd.Size;
                if (cap && std::memchr(ptr, '\0', cap))
                {
                    changed = ZUITextField(ctx, wkey, static_cast<char*>(ptr), cap, wid);
                }
                else
                {
                    ZUILabel(ctx, "(invalid string)", ctx->Theme.TextDim);
                }
                break;
            }

            case FieldType::AssetUUID:
            {
                // Read-only UUID — the field is a uuids::uuid (16 raw bytes), not a
                // string, so it must be formatted rather than reinterpreted as one.
                const auto& uuid     = *static_cast<const uuids::uuid*>(ptr);
                char        text[37] = {}; // Canonical UUID is 36 bytes plus terminator.
                if (!uuid.is_nil())
                {
                    uuids::to_string(uuid, text);
                }
                ZUILabel(ctx, text[0] ? text : "(none)", ctx->Theme.TextDefault);
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
                {
                    if (fd.EnumValues[ei].Value == cur)
                    {
                        preview = fd.EnumValues[ei].Name;
                        break;
                    }
                }
                if (fd.ReadOnly)
                {
                    ZUILabelTight(ctx, preview, ctx->Theme.TextDefault, ZUIFontSize::Body);
                }
                else if (ZUIBeginCombo(ctx, wkey, preview, ZPx(wid)))
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

        ctx->FlatInputs = flat_inputs;
        ctx->Disabled   = disabled;
        if (!fd.ReadOnly)
        {
            ZUIIconBox(ctx, ZUIIcon::RotateCcw, kResetSize, ctx->Theme.TextDim);
        }
        ZUIEndRow(ctx);
        return changed;
    }

    bool InspectorPanel::DrawSessionField(ZUIContext* ctx, const ComponentMeta& meta, const FieldDescriptor& field, EditorSession& session, EntityID entity, float pw, uint32_t comp_idx, uint32_t field_idx)
    {
        if (!session.Scene())
        {
            return false;
        }
        EditorControlDrag control = {};
        if (meta.TypeID == ComponentTypeOf<TransformComponent>() && field.Type == FieldType::Vec3f)
        {
            if (field.Offset == offsetof(TransformComponent, Position))
            {
                control.Property = TransformProperty::Position;
            }
            else if (field.Offset == offsetof(TransformComponent, Rotation))
            {
                control.Property      = TransformProperty::Rotation;
                control.UnitsPerPixel = 0.01745329f; // Inspector degrees -> authored radians
            }
            else if (field.Offset == offsetof(TransformComponent, Scale))
            {
                control.Property = TransformProperty::Scale;
            }
            else
            {
                return false;
            }
            const auto* component = session.Scene()->GetComponent<TransformComponent>(entity);
            if (!component)
            {
                return true;
            }
            auto       copy     = *component;
            const bool disabled = ctx->Disabled;
            ctx->Disabled       = disabled || !session.Editable();
            const bool changed  = DrawField(ctx, field, &copy, pw, comp_idx, field_idx, &session, entity, &control);
            ctx->Disabled       = disabled;
            if (changed && !session.RequestTransformValue(entity, CaptureLocalTransform(copy)))
                ZENGINE_CORE_WARN("[Inspector] A transform value request was rejected.")
            return true;
        }

        if (field.Type != FieldType::Float)
        {
            return false;
        }
        control.IsTransform  = false;
        bool       changed   = false;
        float      candidate = 0.f;
        const bool disabled  = ctx->Disabled;
        if (meta.TypeID == ComponentTypeOf<LightComponent>())
        {
            if (field.Offset == offsetof(LightComponent, Range))
            {
                control.Field = AuthoredFloatField::LightRange;
            }
            else if (field.Offset == offsetof(LightComponent, SpotAngle))
            {
                control.Field = AuthoredFloatField::LightSpotAngle;
            }
            else if (field.Offset == offsetof(LightComponent, Intensity))
            {
                control.Field = AuthoredFloatField::LightIntensity;
            }
            else
            {
                return false;
            }
            const auto* component = session.Scene()->GetComponent<LightComponent>(entity);
            if (!component)
            {
                return true;
            }
            auto copy     = *component;
            ctx->Disabled = disabled || !session.Editable() || !ReadAuthoredFloatField(*session.Scene(), entity, control.Field, candidate);
            changed       = DrawField(ctx, field, &copy, pw, comp_idx, field_idx, &session, entity, &control);
            candidate     = control.Field == AuthoredFloatField::LightRange ? copy.Range : control.Field == AuthoredFloatField::LightSpotAngle ? copy.SpotAngle : copy.Intensity;
        }
        else if (meta.TypeID == ComponentTypeOf<CameraComponent>())
        {
            if (field.Offset == offsetof(CameraComponent, Far))
            {
                control.Field = AuthoredFloatField::CameraFar;
            }
            else if (field.Offset == offsetof(CameraComponent, FovY))
            {
                control.Field = AuthoredFloatField::CameraFovY;
            }
            else if (field.Offset == offsetof(CameraComponent, Near))
            {
                control.Field = AuthoredFloatField::CameraNear;
            }
            else if (field.Offset == offsetof(CameraComponent, AspectRatio))
            {
                control.Field = AuthoredFloatField::CameraAspectRatio;
            }
            else
            {
                return false;
            }
            const auto* component = session.Scene()->GetComponent<CameraComponent>(entity);
            if (!component)
            {
                return true;
            }
            auto copy     = *component;
            ctx->Disabled = disabled || !session.Editable();
            changed       = DrawField(ctx, field, &copy, pw, comp_idx, field_idx, &session, entity, &control);
            candidate     = control.Field == AuthoredFloatField::CameraFar ? copy.Far : control.Field == AuthoredFloatField::CameraFovY ? copy.FovY : control.Field == AuthoredFloatField::CameraNear ? copy.Near : copy.AspectRatio;
        }
        else if (meta.TypeID == ComponentTypeOf<RigidBodyComponent>())
        {
            if (field.Offset == offsetof(RigidBodyComponent, Mass))
            {
                control.Field = AuthoredFloatField::RigidBodyMass;
            }
            else if (field.Offset == offsetof(RigidBodyComponent, Friction))
            {
                control.Field = AuthoredFloatField::RigidBodyFriction;
            }
            else if (field.Offset == offsetof(RigidBodyComponent, Restitution))
            {
                control.Field = AuthoredFloatField::RigidBodyRestitution;
            }
            else
            {
                return false;
            }
            const auto* component = session.Scene()->GetComponent<RigidBodyComponent>(entity);
            if (!component)
            {
                return true;
            }
            auto copy     = *component;
            ctx->Disabled = disabled || !session.Editable();
            changed       = DrawField(ctx, field, &copy, pw, comp_idx, field_idx, &session, entity, &control);
            candidate     = control.Field == AuthoredFloatField::RigidBodyMass ? copy.Mass : control.Field == AuthoredFloatField::RigidBodyFriction ? copy.Friction : copy.Restitution;
        }
        else
        {
            return false;
        }
        ctx->Disabled = disabled;
        if (changed && !session.RequestFieldValue(entity, control.Field, candidate))
            ZENGINE_CORE_WARN("[Inspector] A component field request was rejected.")
        return true;
    }

    bool InspectorPanel::DrawUntrackedField(ZUIContext* ctx, const ComponentMeta& meta, const FieldDescriptor& field, EditorSession& session, EntityID entity, float pw, uint32_t comp_idx, uint32_t field_idx)
    {
        if (!session.Scene())
        {
            return false;
        }
        EditorUntrackedProperty property = EditorUntrackedProperty::LightColor;
        EditorUntrackedValue    value    = {};
        FieldDescriptor         widget   = field;
        widget.Offset                    = 0;
        bool       changed               = false;
        const bool disabled              = ctx->Disabled;
        ctx->Disabled                    = disabled || !session.Editable();
        const auto bind_control          = [&](uint32_t index, bool combo) {
            auto&      binding = m_property_bindings[index];
            const auto target  = session.TargetFor(entity);
            char       key[48] = {}, control_key[80] = {};
            snprintf(key, sizeof(key), "##fv_%u_%u", comp_idx, field_idx);
            snprintf(control_key, sizeof(control_key), combo ? "##combo_btn_%s" : "##cb_%s", key);
            const auto control_hash = ZUIHashStr(control_key, static_cast<uint32_t>(secure_strlen(control_key)));
            const auto popup_hash   = combo ? ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key))) : 0;
            if (binding.ControlKey && (binding.ControlKey != control_hash || binding.Target.UUID != target.UUID || binding.Target.SceneEpoch != target.SceneEpoch || binding.Target.CachedEntity != target.CachedEntity))
            {
                const auto retire = [&](uint64_t hash) {
                    if (ctx->FocusKey == hash)
                    {
                        ctx->FocusKey = 0;
                    }
                    if (ctx->ActiveKey == hash)
                    {
                        ctx->ActiveKey = 0;
                    }
                    if (ctx->HotKey == hash)
                    {
                        ctx->HotKey = 0;
                    }
                };
                retire(binding.ControlKey);
                if (binding.PopupKey)
                {
                    bool owned_popup = false;
                    for (uint32_t depth = 0; depth < ctx->PopupStackSize; ++depth)
                    {
                        if (ctx->PopupStack[depth].Key == binding.PopupKey)
                        {
                            ctx->PopupStackSize = depth;
                            owned_popup         = true;
                            break;
                        }
                    }
                    if (ctx->PendingPopupKey == binding.PopupKey)
                    {
                        ctx->PendingPopupKey = 0;
                    }
                    if (owned_popup)
                    {
                        for (uint32_t i = 0; i < field.EnumCount; ++i)
                        {
                            if (field.EnumValues[i].Name)
                            {
                                retire(ZUIHashStr(field.EnumValues[i].Name, static_cast<uint32_t>(secure_strlen(field.EnumValues[i].Name))));
                            }
                        }
                    }
                }
            }
            binding = {target, control_hash, popup_hash};
        };
        if (meta.TypeID == ComponentTypeOf<LightComponent>() && field.Type == FieldType::Color3f && field.Offset == offsetof(LightComponent, Color))
        {
            const auto* component = session.Scene()->GetComponent<LightComponent>(entity);
            if (component)
            {
                const auto target  = session.TargetFor(entity);
                char       key[48] = {};
                snprintf(key, sizeof(key), "##fv_%u_%u", comp_idx, field_idx);
                const auto hash = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
                if (m_color_key != hash || m_color_target.UUID != target.UUID || m_color_target.SceneEpoch != target.SceneEpoch || m_color_target.CachedEntity != target.CachedEntity)
                {
                    m_color_target = target;
                    m_color_key    = hash;
                    if (++m_color_binding == 0)
                    {
                        ++m_color_binding;
                    }
                }
                m_color_value = {component->Color[0], component->Color[1], component->Color[2]};
                changed       = DrawField(ctx, widget, m_color_value.data(), pw, comp_idx, field_idx, nullptr, {}, nullptr, m_color_binding);
                value.Color   = m_color_value;
            }
        }
        else if (meta.TypeID == ComponentTypeOf<CameraComponent>() && field.Type == FieldType::Bool && field.Offset == offsetof(CameraComponent, IsMain))
        {
            bind_control(0, false);
            property = EditorUntrackedProperty::CameraMain;
            if (const auto* component = session.Scene()->GetComponent<CameraComponent>(entity))
            {
                value.MainCamera = component->IsMain;
                changed          = DrawField(ctx, widget, &value.MainCamera, pw, comp_idx, field_idx);
            }
        }
        else if (meta.TypeID == ComponentTypeOf<RigidBodyComponent>() && field.Type == FieldType::Enum && field.Offset == offsetof(RigidBodyComponent, MotionKind))
        {
            bind_control(1, true);
            property = EditorUntrackedProperty::RigidBodyMotion;
            if (const auto* component = session.Scene()->GetComponent<RigidBodyComponent>(entity))
            {
                value.Motion = component->MotionKind;
                changed      = DrawField(ctx, widget, &value.Motion, pw, comp_idx, field_idx);
            }
        }
        else
        {
            ctx->Disabled = disabled;
            return false;
        }
        ctx->Disabled = disabled;
        if (changed && !session.RequestUntrackedProperty(entity, property, value))
            ZENGINE_CORE_WARN("[Inspector] A component property request was rejected.")
        return true;
    }

    void InspectorPanel::BeginNameEdit(ZUIContext* ctx, EditorSession& session, EntityID entity, uint64_t key)
    {
        if (!session.Editable() || !key)
        {
            return;
        }
        const auto  target = session.TargetFor(entity);
        const auto* name   = session.Scene()->GetComponent<NameComponent>(entity);
        if (target.UUID.is_nil() || !name || std::find(std::begin(name->Value), std::end(name->Value), '\0') == std::end(name->Value))
        {
            return;
        }
        if (m_name_key)
        {
            FinishNameEdit(ctx, session, true);
        }
        secure_strcpy(m_name_buffer, sizeof(m_name_buffer), name->Value);
        secure_strcpy(m_name_original, sizeof(m_name_original), name->Value);
        m_name_target      = target;
        m_name_key         = key;
        m_name_drawn_frame = ctx->FrameIndex;
        m_renaming         = key == ZUIHashStr("##actor_name", 12);
        ctx->FocusKey      = key;
        if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, key))
        {
            state->UserData    = static_cast<float>(secure_strlen(m_name_buffer));
            state->SelectStart = 0;
        }
        ctx->UndoFieldKey = key;
        ctx->UndoTop = ctx->RedoTop = 0;
    }

    void InspectorPanel::FinishNameEdit(ZUIContext* ctx, EditorSession& session, bool commit)
    {
        if (m_name_key && commit && m_name_buffer[0] && secure_strcmp(m_name_original, m_name_buffer) != 0 && !session.RequestRename(m_name_target, m_name_original, m_name_buffer))
            ZENGINE_CORE_WARN("[Inspector] The rename request could not be queued.")
        if (m_name_key && ctx->FocusKey == m_name_key)
        {
            ctx->FocusKey = 0;
        }
        m_name_key    = 0;
        m_name_target = {};
        m_renaming    = false;
    }

    void InspectorPanel::DrawNameEdit(ZUIContext* ctx, EditorSession& session, EntityID entity, const char* key, float width)
    {
        const uint64_t hash     = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
        const bool     disabled = ctx->Disabled;
        ctx->Disabled           = disabled || !session.Editable();
        if (m_name_key && (m_name_target.SceneEpoch != session.SceneEpoch() || m_name_target.CachedEntity != entity || session.Resolve(m_name_target) != entity))
        {
            FinishNameEdit(ctx, session, false);
        }
        if (ctx->FocusKey == hash && m_name_key != hash && !ctx->Disabled)
        {
            BeginNameEdit(ctx, session, entity, hash);
        }

        if (m_name_key == hash)
        {
            m_name_drawn_frame  = ctx->FrameIndex;
            const bool  focused = ctx->FocusKey == hash;
            const auto* state   = ZUIStateGetOrInsert(&ctx->StateStore, hash);
            const bool  outside = (ctx->MousePressed[0] || ctx->MousePressed[1]) && (!state || ctx->MousePos[0] < state->ScreenMinX || ctx->MousePos[0] >= state->ScreenMaxX || ctx->MousePos[1] < state->ScreenMinY || ctx->MousePos[1] >= state->ScreenMaxY);
            if ((focused && ctx->EscapePressed) || ctx->Disabled)
            {
                FinishNameEdit(ctx, session, false);
                if (focused)
                {
                    ctx->EscapePressed = false;
                }
            }
            else
            {
                ZUITextField(ctx, key, m_name_buffer, sizeof(m_name_buffer), width);
                if (!focused || outside || ctx->EnterPressed)
                {
                    FinishNameEdit(ctx, session, true);
                    if (focused)
                    {
                        ctx->EnterPressed = false;
                    }
                }
                ctx->Disabled = disabled;
                return;
            }
        }
        const auto*   name = session.Scene() ? session.Scene()->GetComponent<NameComponent>(entity) : nullptr;
        // Idle widgets may build alongside the active identity editor. They must
        // not overwrite its draft, nor ever bind a text widget to authored storage.
        NameComponent copy = {};
        if (name && std::find(std::begin(name->Value), std::end(name->Value), '\0') != std::end(name->Value))
        {
            copy = *name;
        }
        ZUITextField(ctx, key, copy.Value, sizeof(copy.Value), width);
        ctx->Disabled = disabled;
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
        if (!scene || !eng || !eng->Scene)
        {
            EmptyPanelBg(ctx, "##insp_empty", ctx->Theme.PanelBg, nullptr);
            return;
        }

        // Initialize section-open state to all-true on first call
        if (!m_sec_init)
        {
            for (int i = 0; i < 64; ++i)
            {
                m_sec_open[i] = true;
            }
            m_sec_init = true;
        }

        float   fh = ZUIGetFrameHeight(ctx);
        float   pw = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 220.f;

        ZUIBox* bg = ZUIBeginColumn(ctx, "##insp_bg", ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
        bg->EdgeSoftness  = 0.f;

        // No-selection guard
        const auto entity = scene->SelectedEntity(eng->ActorManager);
        if (scene->Session && m_name_key && (m_name_target.SceneEpoch != scene->Session->SceneEpoch() || m_name_target.CachedEntity != entity || scene->Session->Resolve(m_name_target) != entity))
        {
            FinishNameEdit(ctx, *scene->Session, false);
        }
        if (!eng->Scene->IsAlive(entity))
        {
            ZUIBeginScrollRegion(ctx, "##insp_empty_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 12.f);
            {
                ZUIBox* lbl       = ZUIPushBox(ctx, "##no_sel", 8, ZUI_DrawText);
                lbl->Size[0]      = ZFill();
                lbl->Size[1]      = ZText();
                lbl->TextAlign    = ZUITextAlign::Center;
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, "No entity selected", 18);
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

        ArchetypeMask mask      = eng->Scene->GetMask(entity);
        const auto&   registry  = ComponentReflectionRegistry::Get();

        const float   accent[4] = ZUI_TOKEN(ZEngine::UI::Tokens::AccentBlue);

        // Figma "Actor identity": 45 px band, 22 px actor glyph, name over kind, lock icon.
        {
            const char* kind = "Actor";
            if (eng->Scene->HasComponent<LightComponent>(entity))
            {
                kind = "Light Actor";
            }
            else if (eng->Scene->HasComponent<CameraComponent>(entity))
            {
                kind = "Camera Actor";
            }
            else if (eng->Scene->HasComponent<MeshComponent>(entity))
            {
                kind = "Static Mesh Actor";
            }

            ZUIBox* id     = ZUIBeginRow(ctx, "##insp_id", ZFill(), ZPx(45.f));
            id->Padding[0] = id->Padding[2] = 10.f;
            id->Gap                         = 9.f;
            id->CrossAlign                  = ZUIAlign::Center;
            ZUIIconBox(ctx, ZUIIcon::Box, 22.f, accent);

            ZUIBox* col              = ZUIBeginColumn(ctx, "##insp_id_col", ZFill(), ZFit());
            col->Gap                 = 3.f;
            auto*          nc_comp   = eng->Scene->GetComponent<NameComponent>(entity);
            const uint64_t name_hash = ZUIHashStr("##actor_name", 12);
            if (m_renaming && !scene->Session && nc_comp && ctx->FocusKey != name_hash && ctx->ActiveKey != name_hash && ctx->HotKey != name_hash)
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
                if (scene->Session)
                {
                    DrawNameEdit(ctx, *scene->Session, entity, "##actor_name", fmaxf(pw - 73.f, 80.f));
                }
                else if (ZUITextField(ctx, "##actor_name", nc_comp->Value, sizeof(nc_comp->Value), fmaxf(pw - 73.f, 80.f)))
                {
                    scene->MarkDirty(true);
                }
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
                    if (scene->Session)
                    {
                        BeginNameEdit(ctx, *scene->Session, entity, name_hash);
                    }
                    else
                    {
                        m_renaming    = true;
                        ctx->FocusKey = name_hash;
                    }
                }
            }
            ZUILabelTight(ctx, kind, ctx->Theme.TextDim, ZUIFontSize::Body);
            ZUIEndColumn(ctx);

            ZUIIconBox(ctx, ZUIIcon::LockOpen, 13.f, ctx->Theme.TextDim);
            ZUIEndRow(ctx);
        }

        // Figma "Component actions": Add Component (primary) and Blueprint / Add Script.
        {
            const bool disabled = ctx->Disabled;
            ctx->Disabled       = disabled || (scene->Session && !scene->Session->Editable());
            ZUIBox* ca          = ZUIBeginRow(ctx, "##insp_actions", ZFill(), ZPx(31.f));
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
                    {
                        return; // already on the actor
                    }
                    if (!meta.Add)
                    {
                        return; // display-only, cannot be constructed
                    }
                    any                 = true;
                    char add_label[160] = {};
                    snprintf(add_label, sizeof(add_label), "%s##add_%s", DisplayName(ctx, meta.TypeName), meta.TypeName);
                    if (ZUIMenuItemEx(ctx, add_label, nullptr))
                    {
                        if (scene->Session)
                        {
                            if (!scene->Session->RequestAddComponent(entity, meta.TypeName))
                                ZENGINE_CORE_WARN("[Inspector] The add-component request could not be queued.")
                        }
                        else
                        {
                            eng->Scene->AddComponentRaw(entity, meta.TypeID);
                            if (MaskHas(eng->Scene->GetMask(entity), meta.TypeID))
                            {
                                scene->MarkDirty(true);
                            }
                        }
                    }
                });

                if (!any)
                {
                    ZUIMenuItem(ctx, "No components left to add", false);
                }

                ZUIEndPopup(ctx);
            }
            ctx->Disabled = disabled;
        }

        // Figma "Actor component tree": the actor and the components attached to it.
        {
            auto*       nc_comp   = eng->Scene->GetComponent<NameComponent>(entity);
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
                {
                    return;
                }
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
            {
                fl->TextColor[k] = fc[k];
            }
            ZUISignal fs = ZUISignalFromBox(ctx, fl);
            ZUIPopBox(ctx);
            ZUIEndRow(ctx);
            if (fs.Flags & ZUI_SignalClicked)
            {
                ZUIOpenPopup(ctx, "##insp_cat_popup");
            }

            if (ZUIBeginPopup(ctx, "##insp_cat_popup"))
            {
                if (ZUIMenuItemEx(ctx, "All", nullptr, ZEngine::Helpers::secure_strcmp(m_category, "All") == 0))
                {
                    ZEngine::Helpers::secure_strncpy(m_category, sizeof(m_category), "All", 3);
                }
                static constexpr int kMaxCats       = 16;
                const char*          cats[kMaxCats] = {};
                int                  cat_n          = 0;
                registry.ForEach([&](const ComponentMeta& m) {
                    if (!MaskHas(mask, m.TypeID) || !m.Category)
                    {
                        return;
                    }
                    for (int ci = 0; ci < cat_n; ++ci)
                    {
                        if (cats[ci] && ZEngine::Helpers::secure_strcmp(cats[ci], m.Category) == 0)
                        {
                            return;
                        }
                    }
                    if (cat_n < kMaxCats)
                    {
                        cats[cat_n++] = m.Category;
                    }
                });
                for (int ci = 0; ci < cat_n; ++ci)
                {
                    if (ZUIMenuItemEx(ctx, cats[ci], nullptr, ZEngine::Helpers::secure_strcmp(m_category, cats[ci]) == 0))
                    {
                        ZEngine::Helpers::secure_strncpy(m_category, sizeof(m_category), cats[ci], sizeof(m_category) - 1);
                    }
                }
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
            {
                return;
            }

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
                {
                    if (!meta.Fields[fi].Hidden && strstr(meta.Fields[fi].Name, m_search))
                    {
                        field_match = true;
                    }
                }
                if (!name_matches && !field_match)
                {
                    ++comp_idx;
                    return;
                }
            }

            void* comp_data = eng->Scene->GetComponentRaw(entity, meta.TypeID);
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
                {
                    if (scene->Session && meta.TypeID == ComponentTypeOf<NameComponent>() && meta.Fields[fi].Type == FieldType::String && meta.Fields[fi].Offset == offsetof(NameComponent, Value) && !meta.Fields[fi].Hidden)
                    {
                        char row_key[48] = {}, key[48] = {};
                        snprintf(row_key, sizeof(row_key), "##frow_%u_%u", comp_idx, fi);
                        snprintf(key, sizeof(key), "##fv_%u_%u", comp_idx, fi);
                        auto* row       = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(kPropertyRowH));
                        row->Padding[0] = row->Padding[2] = kRowLead;
                        row->Gap                          = kRowGap;
                        row->CrossAlign                   = ZUIAlign::Center;
                        RowLabel(ctx, meta.Fields[fi].Name);
                        const bool disabled    = ctx->Disabled;
                        const bool flat_inputs = ctx->FlatInputs;
                        ctx->Disabled          = disabled || meta.Fields[fi].ReadOnly;
                        ctx->FlatInputs        = true;
                        DrawNameEdit(ctx, *scene->Session, entity, key, fmaxf(pw - kLabelW - 2.f * kRowLead - 2.f * kRowGap - kResetSize, 36.f));
                        ctx->Disabled   = disabled;
                        ctx->FlatInputs = flat_inputs;
                        if (!meta.Fields[fi].ReadOnly)
                        {
                            ZUIIconBox(ctx, ZUIIcon::RotateCcw, kResetSize, ctx->Theme.TextDim);
                        }
                        ZUIEndRow(ctx);
                        continue;
                    }
                    if (scene->Session && DrawSessionField(ctx, meta, meta.Fields[fi], *scene->Session, entity, pw, comp_idx, fi))
                    {
                        continue;
                    }
                    if (scene->Session && DrawUntrackedField(ctx, meta, meta.Fields[fi], *scene->Session, entity, pw, comp_idx, fi))
                    {
                        continue;
                    }
                    const bool disabled = ctx->Disabled;
                    // Unknown/custom fields stay visible, but cannot bypass the
                    // session until a typed mutation adapter exists for them.
                    ctx->Disabled       = disabled || scene->Session != nullptr;
                    if (DrawField(ctx, meta.Fields[fi], comp_data, pw, comp_idx, fi))
                    {
                        scene->MarkDirty(true);
                    }
                    ctx->Disabled = disabled;
                }
            }

            ++comp_idx;
        });

        ZUIEndScrollRegion(ctx);
        if (scene->Session && m_name_key && m_name_drawn_frame != ctx->FrameIndex)
        {
            FinishNameEdit(ctx, *scene->Session, ctx->FocusKey != m_name_key);
        }
        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Panels
