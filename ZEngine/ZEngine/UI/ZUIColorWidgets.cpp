#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace ZEngine::UI
{
    namespace
    {
        using namespace ColorMath;

        constexpr float BgPanel[4]         = ZUI_TOKEN(Tokens::BgPanel);
        constexpr float BgControl[4]       = ZUI_TOKEN(Tokens::BgControl);
        constexpr float BgHover[4]         = ZUI_TOKEN(Tokens::BgHover);
        constexpr float BorderSubtle[4]    = ZUI_TOKEN(Tokens::BorderSubtle);
        constexpr float BorderStrong[4]    = ZUI_TOKEN(Tokens::BorderStrong);
        constexpr float BorderField[4]     = ZUI_TOKEN(Tokens::BorderField);
        constexpr float TextDisabled[4]    = ZUI_TOKEN(Tokens::TextDisabled);
        constexpr float AccentSelection[4] = ZUI_TOKEN(Tokens::AccentSelection);
        constexpr float AccentBlue[4]      = ZUI_TOKEN(Tokens::AccentBlue);
        constexpr float AccentRed[4]       = ZUI_TOKEN(Tokens::AccentRed);
        constexpr float AccentGreen[4]     = ZUI_TOKEN(Tokens::AccentGreen);

        void            CopyColor(float destination[4], const float source[4])
        {
            Helpers::secure_memcpy(destination, sizeof(float) * 4, source, sizeof(float) * 4);
        }

        // Derive bounded internal IDs from the complete caller key, not a truncated prefix.
        void Key(char (&out)[64], uint64_t owner, const char* part, int index = 0)
        {
            snprintf(out, sizeof(out), "##color_%016llx_%s%d", static_cast<unsigned long long>(owner), part, index);
        }

        ZUIColorWidgetState* State(ZUIContext* ctx, uint64_t owner)
        {
            auto* slot = ZUIStateGetOrInsert(&ctx->StateStore, owner);
            if (!slot)
                return nullptr;
            if (!slot->ColorState)
                slot->ColorState = ZPushStructCtor(&ctx->PersistentArena, ZUIColorWidgetState);
            return slot->ColorState;
        }

        RGBA Bytes(const float color[4])
        {
            return {ToByte(color[0]), ToByte(color[1]), ToByte(color[2]), ToByte(color[3])};
        }

        uint32_t Pack(RGB rgb, int alpha = 255)
        {
            return ZUIPackColor(rgb.R / 255.f, rgb.G / 255.f, rgb.B / 255.f, alpha / 255.f);
        }

        void Frame(ZUIBox* box, const float background[4], const float border[4], float radius = Metrics::Radius3)
        {
            ZUIBoxSetColorArr(box, background);
            CopyColor(box->BorderColor, border);
            box->BorderThickness = 1.f;
            ZUIBoxSetCornerRadius(box, radius);
        }

        ZUIBox* At(ZUIContext* ctx, uint64_t owner, const char* part, int index, float x, float y, float w, float h, ZUIBoxFlags flags = ZUI_None)
        {
            char key[64] = {};
            Key(key, owner, part, index);
            auto* box        = ZUIPushBox(ctx, key, static_cast<uint32_t>(Helpers::secure_strlen(key)), flags | ZUI_FloatX | ZUI_FloatY);
            box->FloatPos[0] = x;
            box->FloatPos[1] = y;
            box->Size[0]     = ZPx(w);
            box->Size[1]     = ZPx(h);
            return box;
        }

        ZUIBox* Text(ZUIContext* ctx, uint64_t owner, const char* part, int index, const char* label, float x, float y, float w, float h, ZUIFontSize role = ZUIFontSize::Body, bool secondary = true)
        {
            auto* box     = At(ctx, owner, part, index, x, y, w, h, ZUI_DrawText);
            box->Label    = ZUIPushStr(&ctx->FrameArena, label, static_cast<uint32_t>(Helpers::secure_strlen(label)));
            box->FontSize = role;
            CopyColor(box->TextColor, secondary ? ctx->Theme.TextDim : ctx->Theme.TextDefault);
            ZUIPopBox(ctx);
            return box;
        }

        struct SwatchData
        {
            RGBA  Color    = {};
            RGBA  OldColor = {};
            bool  Split    = false;
            float Opacity  = 1.f;
        };

        void DrawSwatch(ZUIDrawList* draw, const ZUIBox* box, const void* raw)
        {
            const auto&    data = *static_cast<const SwatchData*>(raw);
            const float    x = box->ScreenMin[0] + 1.f, y = box->ScreenMin[1] + 1.f;
            const float    w = box->ComputedSize[0] - 2.f, h = box->ComputedSize[1] - 2.f;
            const auto     pack     = [&](RGBA color) { return Pack({color.R, color.G, color.B}, static_cast<int>(std::round(color.A * data.Opacity))); };
            const uint32_t colors[] = {pack(data.Split ? data.OldColor : data.Color), pack(data.Color)};
            ZUIDrawListAddColorRect(draw, x, y, x + w, y + h, colors, 2, 2.f, data.Color.A < 255 || (data.Split && data.OldColor.A < 255), false, data.Split);
        }

        void SetSwatch(ZUIContext* ctx, ZUIBox* box, RGBA color, const RGBA* old = nullptr)
        {
            auto* data    = ZPushStructCtor(&ctx->FrameArena, SwatchData);
            data->Color   = color;
            data->Opacity = ctx->Disabled ? 0.4f : 1.f;
            data->Split   = old != nullptr;
            if (old)
                data->OldColor = *old;
            box->CustomDraw     = DrawSwatch;
            box->CustomDrawData = data;
            // Keep the display color inspectable without drawing it twice over the checker.
            ZUIBoxSetColor(box, color.R / 255.f, color.G / 255.f, color.B / 255.f, color.A / 255.f);
        }

        bool ParseNumber(const char* text, int maximum, int& result, bool fractional = false)
        {
            // Validate the entire bounded string. Saturating accumulation accepts
            // huge integers safely but never partially parses junk, floats or NaN.
            bool   negative = false;
            size_t pos      = 0;
            if (text[pos] == '-' || text[pos] == '+')
                negative = text[pos++] == '-';
            if (!text[pos])
                return false;
            int  value        = 0;
            bool digit        = false;
            bool decimal      = false;
            int  firstDecimal = -1;
            for (; text[pos]; ++pos)
            {
                if (fractional && text[pos] == '.' && !decimal)
                {
                    decimal = true;
                    continue;
                }
                if (text[pos] < '0' || text[pos] > '9')
                    return false;
                digit = true;
                if (!decimal)
                    value = std::min(maximum, value * 10 + text[pos] - '0');
                else if (firstDecimal < 0)
                    firstDecimal = text[pos] - '0';
            }
            result = negative ? 0 : std::min(maximum, value + (firstDecimal >= 5 ? 1 : 0));
            return digit;
        }

        void DrawAxis(ZUIDrawList* draw, const ZUIBox* box, const void* raw)
        {
            const auto* color = static_cast<const float*>(raw);
            ZUIDrawListAddRectFilledNoAA(draw, box->ScreenMin[0] + 1.f, box->ScreenMin[1] + 1.f, box->ScreenMin[0] + 4.f, box->ScreenMax[1] - 1.f, ZUIPackColor(color));
        }

        bool Number(ZUIContext* ctx, uint64_t owner, int index, ZUIColorNumberState& state, int& value, int maximum = -1, ZUISize width = ZPx(72.f), const char* part = "number", bool axisStrip = true)
        {
            char key[64] = {};
            Key(key, owner, part, index);
            const auto hash = ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key)));
            if (maximum < 0)
                maximum = index == 3 ? 100 : 255;
            const int  previous = value;
            const bool focused  = !ctx->Disabled && ctx->FocusKey == hash;
            const bool blurred  = !focused || (ctx->MousePressed[0] && ctx->HotKey != hash);
            if (ctx->Disabled)
                state = {};
            const bool cancel = state.Editing && focused && ctx->EscapePressed;
            const bool finish = state.Editing && !cancel && (blurred || ctx->EnterPressed || ctx->TabPressed || ctx->ShiftTabPressed);
            if (cancel)
            {
                if (axisStrip)
                    ctx->EscapePressed = false; // Closed control cancels only its edit; the popup still closes.
                ctx->FocusKey = 0;
                state.Editing = false;
            }
            // Tab focus starts a select-all text edit, not a scrubbing gesture.
            else if (!finish && focused && ctx->KeyboardFocus && !state.Editing && !state.Pressed)
            {
                state.Editing = true;
                snprintf(state.Buffer, sizeof(state.Buffer), "%d", value);
                if (auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, hash))
                {
                    ps->UserData    = static_cast<float>(Helpers::secure_strlen(state.Buffer));
                    ps->SelectStart = 0;
                }
            }

            auto*      parent    = ctx->Current;
            ZUIBox*    field     = nullptr;
            const bool textField = state.Editing;
            if (state.Editing)
            {
                ZUIStylePushFloat(ctx, ZUIStyleVar_FramePaddingX, 9.f);
                ZUITextField(ctx, key, state.Buffer, sizeof(state.Buffer), width.Value);
                ZUIStylePop(ctx);
                field = parent->LastChild;
                if (!axisStrip && focused)
                {
                    ctx->ArrowLeftPressed = ctx->ArrowRightPressed = ctx->ArrowUpPressed = ctx->ArrowDownPressed = false;
                    ctx->HomePressed = ctx->EndPressed = false;
                }
            }
            else
            {
                field             = ZUIPushBox(ctx, key, static_cast<uint32_t>(Helpers::secure_strlen(key)), ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText | ZUI_Clickable);
                field->Size[0]    = width;
                field->Size[1]    = ZPx(22.f);
                field->Padding[0] = 9.f;
                auto signal       = ZUISignalFromBox(ctx, field);
                if (signal.Flags & ZUI_SignalPressed)
                {
                    state.Pressed    = true;
                    state.Dragging   = false;
                    state.PressX     = ctx->MousePos[0];
                    state.PressValue = value;
                }
                if (state.Pressed && ((signal.Flags & ZUI_SignalHeld) || ctx->MouseReleased[0]))
                {
                    const double delta = static_cast<double>(ctx->MousePos[0]) - state.PressX;
                    if (std::isfinite(delta) && std::abs(delta) >= 2.0)
                        state.Dragging = true;
                    if (state.Dragging && std::isfinite(delta))
                        value = static_cast<int>(std::clamp(std::round(state.PressValue + delta), 0.0, static_cast<double>(maximum)));
                }
                if (ctx->MouseReleased[0] || (!ctx->MouseDown[0] && !ctx->MousePressed[0]))
                {
                    if (state.Pressed && !state.Dragging && (signal.Flags & ZUI_SignalClicked))
                    {
                        state.Editing = true;
                        ctx->FocusKey = hash;
                        snprintf(state.Buffer, sizeof(state.Buffer), "%d", value);
                        if (auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, hash))
                        {
                            ps->UserData    = static_cast<float>(Helpers::secure_strlen(state.Buffer));
                            ps->SelectStart = 0;
                        }
                    }
                    state.Pressed  = false;
                    state.Dragging = false;
                }
                ZUIPopBox(ctx);
            }
            if (finish)
            {
                // Process queued characters through the existing text editor
                // BEFORE Enter/Tab commits, even when they arrive in one frame.
                ParseNumber(state.Buffer, maximum, value, !axisStrip);
                state.Editing     = false;
                field->FirstChild = field->LastChild = nullptr; // No stale selection/caret on a completed edit.
                if (ctx->EnterPressed && focused)
                    ctx->FocusKey = 0;
            }
            if (!textField || finish)
            {
                char label[16] = {};
                snprintf(label, sizeof(label), axisStrip && index == 3 ? "%d%%" : "%d", value);
                field->Label = ZUIPushStr(&ctx->FrameArena, label, static_cast<uint32_t>(Helpers::secure_strlen(label)));
            }
            field->Flags   = field->Flags | ZUI_ClipChildren;
            field->Size[0] = width;
            field->Size[1] = ZPx(22.f);
            Frame(field, ctx->Disabled ? BgPanel : ctx->Theme.InputBg, ctx->Disabled ? BorderSubtle : (state.Editing || state.Dragging ? AccentBlue : (ctx->HotKey == hash ? BorderStrong : BorderField)));
            CopyColor(field->TextColor, ctx->Disabled ? TextDisabled : ctx->Theme.TextDefault);
            if (!axisStrip)
            {
                field->TextAlign = state.Editing ? ZUITextAlign::Left : ZUITextAlign::Center;
                return value != previous;
            }
            auto*        axis   = ZPushArray(&ctx->FrameArena, float, 4);
            const float* axes[] = {AccentRed, AccentGreen, AccentBlue, BorderStrong};
            CopyColor(axis, axes[index]);
            if (ctx->Disabled)
                axis[3] *= 0.4f;
            field->CustomDraw     = DrawAxis;
            field->CustomDrawData = axis;
            return value != previous;
        }

        struct StageData
        {
            RGB      Color         = {};
            HSB      Hsb           = {};
            HSL      Hsl           = {};
            bool     LightnessMode = false;
            bool     Dragging      = false;
            uint32_t Border        = 0;
            uint32_t TextColor     = 0;
            int      ActiveHandle  = -1;
            int      HoverHandle   = -1;
        };

        // Fixed R/G/B order, matching BuildChain's projection of the RGB cube.
        constexpr Point ChainDirections[] = {
            { 1.0,                     0.0},
            {-0.5, -0.86602540378443864676},
            {-0.5,  0.86602540378443864676}
        };

        int HitChain(RGB rgb, Point pointer)
        {
            const auto points = BuildChain(rgb);
            // Rings win over legs; the final colour wins when rings coincide.
            for (int i = 2; i >= 0; --i)
            {
                const double dx = pointer.X - points[i + 1].X, dy = pointer.Y - points[i + 1].Y;
                if (dx * dx + dy * dy <= 81.0)
                    return i;
            }
            for (int i = 2; i >= 0; --i)
            {
                const double dx = points[i + 1].X - points[i].X, dy = points[i + 1].Y - points[i].Y;
                const double length2 = dx * dx + dy * dy;
                if (length2 < 1.0)
                    continue;
                const double t  = std::clamp(((pointer.X - points[i].X) * dx + (pointer.Y - points[i].Y) * dy) / length2, 0.0, 1.0);
                const double px = pointer.X - points[i].X - t * dx, py = pointer.Y - points[i].Y - t * dy;
                if (px * px + py * py <= 25.0)
                    return i + 3;
            }
            return -1;
        }

        RGB DragChain(const ZUIColorWidgetState& state, Point pointer)
        {
            RGB              result = state.ChainStart;
            const auto       byte   = [](double value) { return static_cast<int>(std::round(std::clamp(value, 0.0, 255.0))); };
            constexpr double scale  = 96.0 / 255.0;
            if (state.ChainTarget >= 3)
            {
                const int    i         = state.ChainTarget - 3;
                const auto   direction = ChainDirections[i];
                const double delta     = ((pointer.X - state.ChainPress.X) * direction.X + (pointer.Y - state.ChainPress.Y) * direction.Y) / scale;
                const int    values[]  = {result.R, result.G, result.B};
                const int    value     = byte(values[i] + delta);
                if (i == 0)
                    result.R = value;
                else if (i == 1)
                    result.G = value;
                else
                    result.B = value;
            }
            else if (state.ChainTarget == 0)
                result.R = byte((pointer.X - 120.0) / scale);
            else if (state.ChainTarget == 1)
            {
                const double green = (116.0 - pointer.Y) / (0.86602540378443864676 * scale);
                result.R           = byte((pointer.X - 120.0) / scale + green * 0.5);
                result.G           = byte(green);
            }
            return result;
        }

        void Gradient(ZUIDrawList* draw, float x, float y, float w, float h, uint32_t first, uint32_t last, bool vertical = false)
        {
            const uint32_t stops[] = {first, last};
            ZUIDrawListAddColorRect(draw, x, y, x + w, y + h, stops, 2, 2.f, false, vertical);
        }

        void DrawStage(ZUIDrawList* draw, const ZUIBox* box, const void* raw)
        {
            const auto& data = *static_cast<const StageData*>(raw);
            const float x = box->ScreenMin[0], y = box->ScreenMin[1];
            const float cx = x + 120.f, cy = y + 116.f;
            ZUIDrawListAddCircle(draw, cx, cy, 96.f, data.Border, 96);
            ZUIDrawListAddColorField(draw, cx, cy, 96.f, data.Hsb.B / 100.f, data.Hsl.L / 100.f, data.LightnessMode);
            const double angle = data.Hsb.H * std::numbers::pi / 180.0;
            const float  dx = static_cast<float>(std::cos(angle)), dy = static_cast<float>(-std::sin(angle));
            // Hue ray is dashed in the Cursor specimen.
            for (float t = 0.f; t < 96.f; t += 6.f)
                ZUIDrawListAddLine(draw, cx + dx * t, cy + dy * t, cx + dx * std::min(96.f, t + 3.f), cy + dy * std::min(96.f, t + 3.f), data.Border);
            const auto     chain    = BuildChain(data.Color);
            const uint32_t colors[] = {ZUIPackColor(AccentRed), ZUIPackColor(AccentGreen), ZUIPackColor(AccentBlue)};
            for (int i = 0; i < 3; ++i)
            {
                ZUIDrawListAddLine(draw, x + static_cast<float>(chain[i].X), y + static_cast<float>(chain[i].Y), x + static_cast<float>(chain[i + 1].X), y + static_cast<float>(chain[i + 1].Y), colors[i], 2.f);
            }
            ZUIDrawListAddCircleFilled(draw, cx, cy, 2.5f, data.TextColor, 16);
            for (int i = 0; i < 3; ++i)
            {
                const float hx = x + static_cast<float>(chain[i + 1].X), hy = y + static_cast<float>(chain[i + 1].Y);
                const bool  active = data.Dragging && (data.ActiveHandle < 0 ? i == 2 : data.ActiveHandle % 3 == i);
                if (active)
                    ZUIDrawListAddCircleFilled(draw, hx, hy, 12.f, ZUIPackColor(Tokens::AccentBlue.R, Tokens::AccentBlue.G, Tokens::AccentBlue.B, 0.2f), 32);
                if (active || (data.HoverHandle >= 0 && data.HoverHandle % 3 == i))
                    ZUIDrawListAddCircleFilled(draw, hx, hy, 6.f, colors[i], 24);
                ZUIDrawListAddCircle(draw, hx, hy, 6.f, colors[i], 24, 2.f);
            }

            const RGB top = data.LightnessMode ? RGB{255, 255, 255} : HsbToRgb(data.Hsb.H, data.Hsb.S, 100);
            if (data.LightnessMode)
            {
                const uint32_t stops[] = {Pack(top), Pack(HslToRgb(data.Hsl.H, data.Hsl.S, 50)), Pack({})};
                ZUIDrawListAddColorRect(draw, x + 244.f, y + 20.f, x + 258.f, y + 212.f, stops, 3, 2.f, false, true);
            }
            else
                Gradient(draw, x + 244.f, y + 20.f, 14.f, 192.f, Pack(top), Pack({}), true);
            ZUIDrawListAddRect(draw, x + 244.f, y + 20.f, x + 258.f, y + 212.f, data.Border, 2.f);
            const RGB sat0 = data.LightnessMode ? HslToRgb(data.Hsl.H, 0, data.Hsl.L) : HsbToRgb(data.Hsb.H, 0, data.Hsb.B);
            const RGB sat1 = data.LightnessMode ? HslToRgb(data.Hsl.H, 100, data.Hsl.L) : HsbToRgb(data.Hsb.H, 100, data.Hsb.B);
            Gradient(draw, x + 24.f, y + 244.f, 192.f, 14.f, Pack(sat0), Pack(sat1));
            ZUIDrawListAddRect(draw, x + 24.f, y + 244.f, x + 216.f, y + 258.f, data.Border, 2.f);
            for (int i = 0; i < 3; ++i)
            {
                ZUIDrawListAddLine(draw, x + 258.f, y + 20.f + i * 96.f, x + 262.f, y + 20.f + i * 96.f, data.Border);
                ZUIDrawListAddLine(draw, x + 24.f + i * 96.f, y + 258.f, x + 24.f + i * 96.f, y + 262.f, data.Border);
            }
        }

        struct TrackData
        {
            RGB      Stops[7]  = {};
            int      Count     = 2;
            float    Value     = 0.f;
            bool     Alpha     = false;
            bool     Highlight = false;
            uint32_t Border    = 0;
            uint32_t Thumb     = 0;
            uint32_t Window    = 0;
        };

        void DrawTrack(ZUIDrawList* draw, const ZUIBox* box, const void* raw)
        {
            const auto& data = *static_cast<const TrackData*>(raw);
            const float x = box->ScreenMin[0], y = box->ScreenMin[1] + 6.f, w = box->ComputedSize[0];
            uint32_t    stops[7] = {};
            for (int i = 0; i < data.Count; ++i)
                stops[i] = Pack(data.Stops[i], data.Alpha && i == 0 ? 0 : 255);
            ZUIDrawListAddColorRect(draw, x, y, x + w, y + 10.f, stops, data.Count, 5.f, data.Alpha);
            ZUIDrawListAddRect(draw, x, y, x + w, y + 10.f, data.Border, 5.f);
            const float thumb = x + 6.f + (w - 12.f) * data.Value;
            ZUIDrawListAddCircleFilled(draw, thumb, y + 5.f, 6.f, data.Thumb, 24);
            ZUIDrawListAddCircle(draw, thumb, y + 5.f, 7.f, data.Window, 24);
            if (data.Highlight)
                ZUIDrawListAddCircle(draw, thumb, y + 5.f, 8.f, ZUIPackColor(AccentBlue), 24, 2.f);
        }

        void ConfigureTrack(ZUIContext* ctx, TrackData& data, const ZUIColorWidgetState& state, const float color[4], int channel)
        {
            const auto bytes     = Bytes(color);
            const RGB  rgb       = {bytes.R, bytes.G, bytes.B};
            const bool lightness = ctx->ColorModel == ZUIColorModel::HSL;
            data.Border          = ZUIPackColor(BorderField);
            data.Thumb           = ZUIPackColor(ctx->Theme.TextDefault);
            data.Window          = ZUIPackColor(ctx->Theme.WindowBg);
            data.Alpha           = channel == 3;
            const auto& hsb      = state.Hsb;
            const auto& hsl      = state.Hsl;
            for (int stop = 0; stop < 2; ++stop)
            {
                RGB endpoint = rgb;
                if (channel == 3)
                {
                }
                else if (ctx->ColorModel == ZUIColorModel::RGB)
                {
                    if (channel == 0)
                        endpoint.R = stop * 255;
                    if (channel == 1)
                        endpoint.G = stop * 255;
                    if (channel == 2)
                        endpoint.B = stop * 255;
                }
                else if (channel == 1)
                    endpoint = lightness ? HslToRgb(hsl.H, stop * 100, hsl.L) : HsbToRgb(hsb.H, stop * 100, hsb.B);
                else if (channel == 2)
                    endpoint = lightness ? HslToRgb(hsl.H, hsl.S, stop * 100) : HsbToRgb(hsb.H, hsb.S, stop * 100);
                data.Stops[stop] = endpoint;
            }
            if (channel == 0 && ctx->ColorModel != ZUIColorModel::RGB)
            {
                data.Count = 7;
                for (int stop = 0; stop < 7; ++stop)
                    data.Stops[stop] = lightness ? HslToRgb(stop * 60, hsl.S, hsl.L) : HsbToRgb(stop * 60, hsb.S, hsb.B);
            }
            else if (channel == 2 && lightness)
            {
                data.Count    = 3;
                data.Stops[2] = data.Stops[1];
                data.Stops[1] = HslToRgb(hsl.H, hsl.S, 50);
            }
        }

        using ColorValue = std::array<float, 4>;

        ColorValue Snapshot(const float* color, bool alpha = true)
        {
            return {color[0], color[1], color[2], alpha ? color[3] : 1.f};
        }

        bool Same(const ColorValue& a, const ColorValue& b)
        {
            for (size_t i = 0; i < a.size(); ++i)
                if (std::bit_cast<uint32_t>(a[i]) != std::bit_cast<uint32_t>(b[i]))
                    return false;
            return true;
        }

        bool SameRgb(const ColorValue& a, const float b[4])
        {
            for (int i = 0; i < 3; ++i)
                if (ToByte(a[i]) != ToByte(b[i]))
                    return false;
            return true;
        }

        void Sync(ZUIColorWidgetState& state, const float color[4])
        {
            const auto rgb    = Bytes(color);
            state.Hsb         = RgbToHsb({rgb.R, rgb.G, rgb.B}, state.Hue);
            state.Hsl         = RgbToHsl({rgb.R, rgb.G, rgb.B}, state.Hue);
            state.Hue         = state.Hsb.H;
            state.LastDisplay = Snapshot(color);
            state.Initialized = true;
        }

        void SetRgb(float color[4], RGB rgb)
        {
            const int values[] = {rgb.R, rgb.G, rgb.B};
            // An unchanged byte is not permission to quantize an exact float.
            for (int i = 0; i < 3; ++i)
                if (ToByte(color[i]) != values[i])
                    color[i] = values[i] / 255.f;
        }

        void SetHsb(ZUIColorWidgetState& state, float color[4], HSB hsb)
        {
            state.Hsb      = hsb;
            state.Hue      = hsb.H;
            const auto rgb = HsbToRgb(hsb.H, hsb.S, hsb.B);
            SetRgb(color, rgb);
            state.Hsl = RgbToHsl(rgb, hsb.H);
        }

        int ChannelMaximum(ZUIColorModel model, int index)
        {
            return index == 3 ? 100 : model == ZUIColorModel::RGB ? 255 : index == 0 ? 360 : 100;
        }

        std::array<int, 4> ChannelValues(const ZUIColorWidgetState& state, const float color[4], ZUIColorModel model)
        {
            const auto bytes = Bytes(color);
            const int  a     = static_cast<int>(std::round(color[3] * 100.f));
            if (model == ZUIColorModel::RGB)
                return {bytes.R, bytes.G, bytes.B, a};
            if (model == ZUIColorModel::HSL)
                return {state.Hsl.H, state.Hsl.S, state.Hsl.L, a};
            return {state.Hsb.H, state.Hsb.S, state.Hsb.B, a};
        }

        void SetChannel(ZUIColorWidgetState& state, float color[4], ZUIColorModel model, int index, int value)
        {
            auto values = ChannelValues(state, color, model);
            value       = std::clamp(value, 0, ChannelMaximum(model, index));
            if (values[index] == value)
                return;
            if (index == 3)
            {
                color[3] = value / 100.f;
                return;
            }
            values[index] = value;
            if (model == ZUIColorModel::RGB)
            {
                color[index] = value / 255.f;
                Sync(state, color);
            }
            else if (model == ZUIColorModel::HSB)
                SetHsb(state, color, {values[0], values[1], values[2]});
            else
            {
                state.Hsl      = {values[0], values[1], values[2]};
                state.Hue      = values[0];
                const auto rgb = HslToRgb(values[0], values[1], values[2]);
                SetRgb(color, rgb);
                state.Hsb = RgbToHsb(rgb, state.Hue);
            }
        }

        void AddRecent(ZUIContext* ctx, RGBA color)
        {
            uint32_t found = 0;
            while (found < ctx->RecentColorCount && ctx->RecentColors[found] != color)
                ++found;
            if (found == ctx->RecentColorCount)
                ctx->RecentColorCount = std::min(24u, ctx->RecentColorCount + 1);
            for (uint32_t i = std::min(found, ctx->RecentColorCount - 1); i > 0; --i)
                ctx->RecentColors[i] = ctx->RecentColors[i - 1];
            ctx->RecentColors[0] = color;
        }

        bool Nudge(ZUIContext* ctx, uint64_t key, int& value, int maximum)
        {
            if (ctx->Disabled || ctx->FocusKey != key)
                return false;
            const int old  = value;
            const int step = ctx->ShiftDown ? 10 : 1;
            if (ctx->HomePressed)
                value = 0;
            else if (ctx->EndPressed)
                value = maximum;
            else
                value = std::clamp(value + step * ((ctx->ArrowRightPressed || ctx->ArrowUpPressed) - (ctx->ArrowLeftPressed || ctx->ArrowDownPressed)), 0, maximum);
            // A focused slider owns arrows even at a limit; do not move menu focus.
            ctx->ArrowLeftPressed = ctx->ArrowRightPressed = ctx->ArrowUpPressed = ctx->ArrowDownPressed = false;
            ctx->HomePressed = ctx->EndPressed = false;
            return old != value;
        }

        bool Drag(ZUIContext* ctx, ZUIColorWidgetState& state, ZUIBox* box, bool allowPress = true)
        {
            const auto signal = ZUISignalFromBox(ctx, box);
            if (!ctx->Disabled && allowPress && (signal.Flags & ZUI_SignalPressed))
                state.DragKey = box->Key;
            if (ctx->Disabled || state.DragKey != box->Key)
                return false;
            if (ctx->EscapePressed || (!ctx->MouseDown[0] && !ctx->MousePressed[0]))
            {
                state.DragKey = 0;
            }
            return !ctx->EscapePressed; // Include release position, even outside the window.
        }

        void FinishHistory(ZUIContext* ctx, ZUIColorWidgetState& state, uint64_t owner, const ColorValue& before, const float* color, bool alpha, ZUIColorSpace space, bool active)
        {
            const auto after = Snapshot(color, alpha);
            if (!state.TransactionActive && (!Same(before, after) || active))
            {
                state.TransactionBefore = before;
                state.TransactionActive = true;
            }
            if (state.TransactionActive && !active)
            {
                if (!Same(state.TransactionBefore, after))
                {
                    auto& count  = ctx->ColorUndoCount;
                    auto& cursor = ctx->ColorUndoCursor;
                    count        = cursor; // A new edit branches history, dropping redo.
                    if (count == ZUIContext::ColorUndoCapacity)
                    {
                        for (uint32_t i = 1; i < count; ++i)
                            ctx->ColorUndo[i - 1] = ctx->ColorUndo[i];
                        --count;
                    }
                    ctx->ColorUndo[count++] = {owner, state.TransactionBefore, after, space, alpha, state.BindingIdentity};
                    cursor                  = count;
                    AddRecent(ctx, Bytes(state.LastDisplay.data()));
                }
                state.TransactionActive = false;
            }
        }

        void RetireBindingInput(ZUIContext* ctx, uint64_t owner)
        {
            struct Part
            {
                const char* Name  = nullptr;
                int         Count = 0;
            };
            constexpr Part parts[] = {
                {      "swatch",  1},
                {      "number",  4},
                {       "model",  3},
                {       "field",  1},
                {         "bar",  2},
                {"channelTrack",  4},
                {"channelValue",  4},
                {         "hex",  1},
                {      "recent", 24},
                {         "hue",  1},
                {      "vertex",  6}
            };
            for (const auto& part : parts)
                for (int i = 0; i < part.Count; ++i)
                {
                    char key[64] = {};
                    Key(key, owner, part.Name, i);
                    const auto hash = ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key)));
                    if (ctx->FocusKey == hash)
                        ctx->FocusKey = 0;
                    if (ctx->ActiveKey == hash)
                        ctx->ActiveKey = 0;
                    if (ctx->HotKey == hash)
                        ctx->HotKey = 0;
                }
            // An explicit open queued for this build belongs to the new binding;
            // retire an inherited popup, not a newly requested one.
            if (ctx->PendingPopupKey != owner)
                for (uint32_t depth = 0; depth < ctx->PopupStackSize; ++depth)
                    if (ctx->PopupStack[depth].Key == owner)
                    {
                        ctx->PopupStackSize = depth;
                        break;
                    }
        }

        void PrepareBinding(ZUIContext* ctx, ZUIColorWidgetState& state, uint64_t owner, const float* color, bool alpha, ZUIColorSpace space, uintptr_t binding_identity = 0)
        {
            const auto identity = binding_identity ? binding_identity : reinterpret_cast<uintptr_t>(color);
            const bool rebound  = state.BoundAlpha != alpha || state.BoundSpace != space || (state.LastStorageFrame && state.BindingIdentity != identity);
            if (rebound && state.LastStorageFrame)
                RetireBindingInput(ctx, owner);
            const bool frameGap = state.LastStorageFrame && state.LastStorageFrame + 1 != ctx->FrameIndex;
            const bool external = state.LastStorageFrame && !Same(state.LastStored, Snapshot(color, alpha));
            // A hidden widget can miss the release event. Its live value is
            // already applied; commit that gesture when the unchanged binding
            // returns, without ever following an old pointer or stale value.
            if (frameGap && !rebound && !external && state.TransactionActive)
                FinishHistory(ctx, state, owner, state.LastStored, color, alpha, space, false);
            if (rebound || frameGap || external)
            {
                state.DragKey           = 0;
                state.TransactionActive = false;
                state.HexEditing        = false;
                state.Initialized       = false;
                for (auto& number : state.Numbers)
                    number = {};
                for (auto& number : state.Channels)
                    number = {};
                if (rebound)
                    state.LastPickerFrame = 0;
            }
            state.BoundAlpha      = alpha;
            state.BoundSpace      = space;
            state.BindingIdentity = identity;
        }

        bool Undo(ZUIContext* ctx, ZUIColorWidgetState& state, uint64_t owner, float* color, bool alpha, ZUIColorSpace space)
        {
            if (ctx->Disabled || state.TransactionActive || ctx->TextInputActive || (!ctx->CtrlZPressed && !ctx->CtrlYPressed))
                return false;
            enum class Editor
            {
                None,
                Number,
                Channel,
                Hex
            };
            struct Part
            {
                const char* Name    = nullptr;
                int         Count   = 0;
                Editor      Editing = Editor::None;
            };
            constexpr Part parts[] = {
                {"swatch", 1},
                {"number", 4, Editor::Number},
                {"model", 3},
                {"field", 1},
                {"bar", 2},
                {"channelTrack", 4},
                {"channelValue", 4, Editor::Channel},
                {"hex", 1, Editor::Hex},
                {"recent", 24},
                {"hue", 1},
                {"vertex", 6}
            };
            bool ownsFocus = false;
            for (const auto& part : parts)
            {
                for (int i = 0; i < part.Count; ++i)
                {
                    char key[64] = {};
                    Key(key, owner, part.Name, i);
                    if (ctx->FocusKey != ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key))))
                        continue;
                    if ((part.Editing == Editor::Number && state.Numbers[i].Editing) || (part.Editing == Editor::Channel && state.Channels[i].Editing) || (part.Editing == Editor::Hex && state.HexEditing))
                        return false; // Text editor owns its own undo until it commits.
                    ownsFocus = true;
                }
            }
            if (!ownsFocus)
                return false;
            const bool redo   = ctx->CtrlYPressed;
            auto&      cursor = ctx->ColorUndoCursor;
            if ((!redo && !cursor) || (redo && cursor == ctx->ColorUndoCount))
                return false;
            const auto& entry = ctx->ColorUndo[redo ? cursor : cursor - 1];
            if (entry.Owner != owner || entry.Space != space || entry.Alpha != alpha || entry.BindingIdentity != state.BindingIdentity)
                return false;
            const auto& expected = redo ? entry.Before : entry.After;
            if (!Same(Snapshot(color, alpha), expected))
                return false; // External mutation must never be overwritten by stale history.
            const auto& restored = redo ? entry.After : entry.Before;
            for (int i = 0; i < (alpha ? 4 : 3); ++i)
                color[i] = restored[i];
            if (redo)
                ++cursor;
            else
                --cursor;
            ctx->CtrlZPressed = ctx->CtrlYPressed = false;
            return true;
        }

        struct PickerTextBoxes
        {
            ZUIBox* Channels[4] = {};
            ZUIBox* Hex         = nullptr;
        };

        PickerTextBoxes PickerTexts(ZUIContext* ctx, ZUIBox* canvas, uint64_t owner, ZUIColorWidgetState& state, float color[4], bool alpha)
        {
            PickerTextBoxes boxes = {};
            // Resolve blur/commit before pointer or keyboard edits elsewhere in
            // the picker. Otherwise a stale text buffer can overwrite that edit.
            for (int i = 0; i < (alpha ? 4 : 3); ++i)
            {
                const auto inputModel = state.Channels[i].Editing || state.Channels[i].Pressed ? state.ChannelModel : ctx->ColorModel;
                int        value      = ChannelValues(state, color, inputModel)[i];
                if (Number(ctx, owner, i, state.Channels[i], value, ChannelMaximum(inputModel, i), ZPx(52.f), "channelValue", false))
                    SetChannel(state, color, inputModel, i, value);
                auto* number        = canvas->LastChild;
                number->Flags       = number->Flags | ZUI_FloatX | ZUI_FloatY;
                number->FloatPos[0] = 255.f;
                number->FloatPos[1] = 339.f + i * 28.f;
                boxes.Channels[i]   = number;
            }
            state.ChannelModel = ctx->ColorModel;
            char hexKey[64]    = {};
            Key(hexKey, owner, "hex");
            const auto hexHash = ZUIHashStr(hexKey, static_cast<uint32_t>(Helpers::secure_strlen(hexKey)));
            const bool focused = !ctx->Disabled && ctx->FocusKey == hexHash;
            const bool finish  = state.HexEditing && (!focused || (ctx->MousePressed[0] && ctx->HotKey != hexHash) || ctx->EnterPressed || ctx->TabPressed || ctx->ShiftTabPressed);
            if (!state.HexEditing)
                FormatHex(Bytes(color), state.HexBuffer, sizeof(state.HexBuffer));
            state.HexEditing |= focused;
            ZUITextField(ctx, hexKey, state.HexBuffer, sizeof(state.HexBuffer), 246.f);
            if (focused)
            {
                ctx->ArrowLeftPressed = ctx->ArrowRightPressed = ctx->ArrowUpPressed = ctx->ArrowDownPressed = false;
                ctx->HomePressed = ctx->EndPressed = false;
            }
            auto* box        = canvas->LastChild;
            boxes.Hex        = box;
            box->Flags       = box->Flags | ZUI_FloatX | ZUI_FloatY;
            box->FloatPos[0] = 61.f;
            box->FloatPos[1] = 457.f;
            box->Size[1]     = ZPx(22.f);
            box->Padding[0]  = 8.f;
            if (ctx->EscapePressed || ctx->Disabled || finish || (focused && ctx->EnterPressed))
            {
                if (!ctx->EscapePressed && !ctx->Disabled && (finish || ctx->EnterPressed))
                    if (const auto parsed = ParseHex(state.HexBuffer))
                    {
                        SetRgb(color, {parsed->R, parsed->G, parsed->B});
                        if (alpha && ToByte(color[3]) != parsed->A)
                            color[3] = parsed->A / 255.f;
                        Sync(state, color);
                    }
                FormatHex(Bytes(color), state.HexBuffer, sizeof(state.HexBuffer));
                box->Label      = ZUIPushStr(&ctx->FrameArena, state.HexBuffer, static_cast<uint32_t>(Helpers::secure_strlen(state.HexBuffer)));
                box->FirstChild = box->LastChild = nullptr;
                state.HexEditing                 = false;
                if (focused && ctx->EnterPressed)
                {
                    ctx->FocusKey = 0;
                    if (ctx->PopupBuildDepth)
                        ZUIClosePopup(ctx);
                }
            }
            return boxes;
        }

        void PickerContent(ZUIContext* ctx, ZUIBox* canvas, uint64_t owner, float color[4], bool alpha)
        {
            auto* state = State(ctx, owner);
            if (!state)
                return;
            if (!state->Initialized || !SameRgb(state->LastDisplay, color))
                Sync(*state, color);
            auto bytes = Bytes(color);
            RGB  rgb   = {bytes.R, bytes.G, bytes.B};
            if (!state->LastPickerFrame || state->LastPickerFrame + 1 != ctx->FrameIndex)
            {
                state->OldColor   = bytes;
                state->OldDisplay = Snapshot(color);
                state->DragKey    = 0;
                state->HexEditing = false;
                for (auto& channel : state->Channels)
                    channel = {};
            }
            state->LastPickerFrame = ctx->FrameIndex;
            if (ctx->Disabled)
                state->DragKey = 0;
            HSB hsb              = state->Hsb;
            HSL hsl              = state->Hsl;
            canvas->Size[0]      = ZPx(320.f);
            canvas->Size[1]      = ZPx(567.f);
            const auto textBoxes = PickerTexts(ctx, canvas, owner, *state, color, alpha);
            auto*      hexBox    = textBoxes.Hex;

            Text(ctx, owner, "heading", 0, "Color", 13, 13, 100, 26, ZUIFontSize::Heading, false);
            const char* models[]        = {"RGB", "HSB", "HSL"};
            ZUIBox*     modelButtons[3] = {};
            for (int i = 0; i < 3; ++i)
            {
                auto* button      = At(ctx, owner, "model", i, 171.f + i * 46.f, 13, 44, 26, ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText | ZUI_Clickable | ZUI_KeyboardActivate);
                modelButtons[i]   = button;
                button->Label     = ZUIPushStr(&ctx->FrameArena, models[i], 3);
                button->TextAlign = ZUITextAlign::Center;
                CopyColor(button->TextColor, ctx->Theme.TextDefault);
                const auto signal = ZUISignalFromBox(ctx, button);
                if (signal.Flags & ZUI_SignalClicked)
                {
                    if (ctx->ColorModel != static_cast<ZUIColorModel>(i))
                    {
                        state->DragKey = 0;
                    }
                    ctx->ColorModel = static_cast<ZUIColorModel>(i);
                }
                ZUIPopBox(ctx);
            }
            // Resolve all three after input, so no stale selected button appears
            // for one frame. Direct frame-local references avoid rescanning all
            // children and repeatedly formatting/hashing the same IDs.
            for (int i = 0; i < 3; ++i)
            {
                auto* b = modelButtons[i];
                Frame(b, ctx->ColorModel == static_cast<ZUIColorModel>(i) ? AccentSelection : (ctx->HotKey == b->Key ? BgHover : BgControl), ctx->ColorModel == static_cast<ZUIColorModel>(i) ? AccentBlue : BorderField, Metrics::Radius4);
            }

            const bool   lightness = ctx->ColorModel == ZUIColorModel::HSL;
            auto*        stage     = At(ctx, owner, "stage", 0, 13, 51, 294, 276);
            auto*        field     = At(ctx, owner, "field", 0, 0, 0, 240, 232, ZUI_Clickable | ZUI_DrawBackground);
            const auto*  bounds    = ZUIStateGetOrInsert(&ctx->StateStore, field->Key);
            const Point  point     = {ctx->MousePos[0] - bounds->ScreenMinX, ctx->MousePos[1] - bounds->ScreenMinY};
            const double dx = point.X - 120.0, dy = point.Y - 116.0;
            const bool   inside = std::hypot(dx, dy) <= HexEdgeDist(std::atan2(-dy, dx), 96.0);
            if (ctx->MousePressed[0] && ctx->HotKey == field->Key && !ctx->Disabled)
            {
                const auto bytes   = Bytes(color);
                state->ChainStart  = {bytes.R, bytes.G, bytes.B};
                state->ChainPress  = point;
                state->ChainTarget = HitChain(state->ChainStart, point);
                state->Origin      = {static_cast<double>(state->Hsb.B), static_cast<double>(state->Hsl.L), static_cast<double>(state->Hsb.H), static_cast<double>(state->Hsl.S)};
            }
            if (Drag(ctx, *state, field, inside || state->ChainTarget >= 0))
            {
                if (state->ChainTarget != -1 && state->ChainTarget != 2)
                {
                    if (std::isfinite(point.X) && std::isfinite(point.Y))
                    {
                        SetRgb(color, DragChain(*state, point));
                        Sync(*state, color);
                    }
                }
                else
                {
                    FieldInput input = {};
                    input.Mode       = lightness ? FieldMode::Lightness : FieldMode::Brightness;
                    input.Origin     = state->Origin;
                    input.ClampOnly  = true;
                    if (const auto picked = HsbFromField(point, input))
                    {
                        SetHsb(*state, color, *picked);
                    }
                }
            }
            if (!ctx->Disabled && ctx->FocusKey == field->Key)
            {
                const int step     = ctx->ShiftDown ? 10 : 1;
                const int hueDelta = step * (ctx->ArrowRightPressed - ctx->ArrowLeftPressed);
                const int satDelta = step * (ctx->ArrowUpPressed - ctx->ArrowDownPressed);
                if (hueDelta || satDelta)
                {
                    auto picked = state->Hsb;
                    picked.H    = (picked.H + hueDelta + 360) % 360;
                    picked.S    = std::clamp(picked.S + satDelta, 0, 100);
                    SetHsb(*state, color, picked);
                }
                ctx->ArrowLeftPressed = ctx->ArrowRightPressed = ctx->ArrowUpPressed = ctx->ArrowDownPressed = false;
            }
            ZUIPopBox(ctx);
            for (int i = 0; i < 2; ++i)
            {
                auto*     bar     = At(ctx, owner, "bar", i, i == 0 ? 236.f : 18.f, i == 0 ? 14.f : 236.f, i == 0 ? 28.f : 204.f, i == 0 ? 204.f : 28.f, ZUI_Clickable);
                auto      values  = ChannelValues(*state, color, lightness ? ZUIColorModel::HSL : ZUIColorModel::HSB);
                const int channel = i == 0 ? 2 : 1;
                int       value   = values[channel];
                if (Drag(ctx, *state, bar))
                {
                    const auto* b        = ZUIStateGetOrInsert(&ctx->StateStore, bar->Key);
                    const float position = ctx->MousePos[i == 0 ? 1 : 0] - (i == 0 ? b->ScreenMinY : b->ScreenMinX) - 6.f;
                    if (std::isfinite(position))
                        value = static_cast<int>(std::round(std::clamp(i == 0 ? 1.f - position / 192.f : position / 192.f, 0.f, 1.f) * 100.f));
                }
                Nudge(ctx, bar->Key, value, 100);
                SetChannel(*state, color, lightness ? ZUIColorModel::HSL : ZUIColorModel::HSB, channel, value);
                ZUIPopBox(ctx);
            }
            auto* data                      = ZPushStructCtor(&ctx->FrameArena, StageData);
            data->Color                     = rgb;
            data->Hsb                       = hsb;
            data->Hsl                       = hsl;
            data->LightnessMode             = lightness;
            data->Border                    = ZUIPackColor(BorderStrong);
            data->TextColor                 = ZUIPackColor(ctx->Theme.TextDefault);
            stage->CustomDraw               = DrawStage;
            stage->CustomDrawData           = data;
            const char*     letters[]       = {"R", "Y", "G", "C", "B", "M"};
            constexpr float positions[6][2] = {
                {   225,  110.5f},
                {   171,  16.97f},
                { 62.5f,  16.97f},
                {  8.5f,  110.5f},
                {    63, 204.03f},
                {169.5f, 204.03f}
            };
            // These are overlays of the field, not sibling click surfaces.
            // A descendant hit takes precedence over its field-sized parent.
            ctx->Current = field;
            for (int i = 0; i < 6; ++i)
            {
                // Transparent background opts out of the generic rectangular
                // hover overlay; these labels only brighten their text.
                auto* letter      = At(ctx, owner, "vertex", i, positions[i][0] - 6.f, positions[i][1] - 5.5f, 22, 22, ZUI_DrawText | ZUI_DrawBackground | ZUI_Clickable | ZUI_NoKeyboardFocus);
                letter->Label     = ZUIPushStr(&ctx->FrameArena, letters[i], 1);
                letter->FontSize  = ZUIFontSize::Tiny;
                letter->TextAlign = ZUITextAlign::Center;
                CopyColor(letter->TextColor, ctx->HotKey == letter->Key ? ctx->Theme.TextDefault : ctx->Theme.TextDim);
                if (ZUISignalFromBox(ctx, letter).Flags & ZUI_SignalClicked)
                {
                    SetHsb(*state, color, {i * 60, 100, 100}); // Also HSL(h,100,50), with alpha untouched.
                    ctx->FocusKey = field->Key;                // Undo remains available; CP11 adds no Tab stops.
                }
                ZUIPopBox(ctx);
            }
            ctx->Current = stage;
            for (int i = 0; i < 3; ++i)
            {
                for (int axis = 0; axis < 2; ++axis)
                {
                    const int value    = axis == 0 ? 100 - 50 * i : 50 * i;
                    char      label[8] = {};
                    snprintf(label, sizeof(label), "%d", value);
                    auto* tick     = At(ctx, owner, axis == 0 ? "verticaltick" : "horizontaltick", i, axis == 0 ? 266.f : 21.f + i * 96.f - (i == 0 ? 0.f : (i == 1 ? 3.f : 5.f)), axis == 0 ? 14.5f + i * 96.f : 264.f, 24, 11, ZUI_DrawText | ZUI_Clickable | ZUI_NoKeyboardFocus);
                    tick->Label    = ZUIPushStr(&ctx->FrameArena, label, static_cast<uint32_t>(Helpers::secure_strlen(label)));
                    tick->FontSize = ZUIFontSize::Tiny;
                    CopyColor(tick->TextColor, ctx->Theme.TextDim);
                    if (ZUISignalFromBox(ctx, tick).Flags & ZUI_SignalClicked)
                    {
                        SetChannel(*state, color, lightness ? ZUIColorModel::HSL : ZUIColorModel::HSB, axis == 0 ? 2 : 1, value);
                    }
                    ZUIPopBox(ctx);
                }
            }
            // Reuse the existing filled triangle glyph; its silhouette matches the exported bar arrows.
            auto* vArrow           = At(ctx, owner, "brightnessarrow", 0, 236, 16.f + 192.f * (1.f - (lightness ? hsl.L : hsb.B) / 100.f), 6, 8, ZUI_DrawTriArrow);
            vArrow->ArrowFitBounds = true;
            CopyColor(vArrow->TextColor, ctx->Theme.TextDefault);
            ZUIStateGetOrInsert(&ctx->StateStore, vArrow->Key)->UserData = 0.f;
            ZUIPopBox(ctx);
            auto* hArrow           = At(ctx, owner, "saturationarrow", 0, 20.f + 192.f * (lightness ? hsl.S : hsb.S) / 100.f, 237, 8, 6, ZUI_DrawTriArrow);
            hArrow->ArrowFitBounds = true;
            CopyColor(hArrow->TextColor, ctx->Theme.TextDefault);
            ZUIStateGetOrInsert(&ctx->StateStore, hArrow->Key)->UserData = 1.f;
            ZUIPopBox(ctx);
            char hue[16] = {};
            snprintf(hue, sizeof(hue), "%d" ZUI_GLYPH_DEGREE, hsb.H);
            float measured[2] = {25.f, 11.f};
            if (auto* font = ctx->GetFont(ZUIFontSize::Body))
                ZUIMeasureText(font, hue, static_cast<uint32_t>(Helpers::secure_strlen(hue)), measured);
            const float  badgeW = measured[0] + 16.f;
            const double angle  = hsb.H * std::numbers::pi / 180.0;
            ctx->Current        = field;
            auto* badge         = At(ctx, owner, "hue", 0, 120.f + 103.f * static_cast<float>(std::cos(angle)) - badgeW * 0.5f, 116.f - 103.f * static_cast<float>(std::sin(angle)) - 10.f, badgeW, 20, ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText | ZUI_Clickable | ZUI_NoKeyboardFocus);
            if (!ctx->Disabled && ctx->MousePressed[0] && ctx->HotKey == badge->Key)
                state->HueOrigin = state->Hsb;
            if (Drag(ctx, *state, badge) && std::isfinite(point.X) && std::isfinite(point.Y) && std::hypot(dx, dy) > 0.0001)
            {
                auto value = state->HueOrigin;
                value.H    = (static_cast<int>(std::round(std::atan2(-dy, dx) * 180.0 / std::numbers::pi)) + 360) % 360;
                SetHsb(*state, color, value);
                if (ctx->MouseReleased[0])
                    ctx->FocusKey = field->Key;
            }
            Frame(badge, AccentSelection, AccentBlue, 10.f);
            badge->Label = ZUIPushStr(&ctx->FrameArena, hue, static_cast<uint32_t>(Helpers::secure_strlen(hue)));
            CopyColor(badge->TextColor, ctx->Theme.TextDefault);
            badge->TextAlign = ZUITextAlign::Center;
            ZUIPopBox(ctx);
            ctx->Current = stage;
            ZUIPopBox(ctx); // Stage

            const char* names            = ctx->ColorModel == ZUIColorModel::RGB ? "RGBA" : (lightness ? "HSLA" : "HSBA");
            ZUIBox*     channelTracks[4] = {};
            TrackData*  trackDatas[4]    = {};
            for (int i = 0; i < (alpha ? 4 : 3); ++i)
            {
                char name[2] = {names[i], '\0'};
                Text(ctx, owner, "channelLabel", i, name, 13, 339.f + i * 28.f, 14, 22);
                auto* track       = At(ctx, owner, "channelTrack", i, 35, 339.f + i * 28.f, 212, 22, ZUI_Clickable);
                channelTracks[i]  = track;
                int       value   = ChannelValues(*state, color, ctx->ColorModel)[i];
                const int maximum = ChannelMaximum(ctx->ColorModel, i);
                if (Drag(ctx, *state, track))
                {
                    const auto* b        = ZUIStateGetOrInsert(&ctx->StateStore, track->Key);
                    const float position = ctx->MousePos[0] - b->ScreenMinX - 6.f;
                    if (std::isfinite(position))
                        value = static_cast<int>(std::round(std::clamp(position / 200.f, 0.f, 1.f) * maximum));
                }
                Nudge(ctx, track->Key, value, maximum);
                SetChannel(*state, color, ctx->ColorModel, i, value);
                auto* td              = ZPushStructCtor(&ctx->FrameArena, TrackData);
                trackDatas[i]         = td;
                track->CustomDraw     = DrawTrack;
                track->CustomDrawData = td;
                ZUIPopBox(ctx);
            }
            auto* preview = At(ctx, owner, "preview", 0, 13, 457, 40, 22, ZUI_DrawBorder | ZUI_Clickable | ZUI_NoKeyboardFocus);
            Frame(preview, ctx->Theme.PanelBg, BorderField);
            if ((ZUISignalFromBox(ctx, preview).Flags & ZUI_SignalClicked) && ctx->MousePos[0] < ZUIStateGetOrInsert(&ctx->StateStore, preview->Key)->ScreenMinX + 20.f)
            {
                CopyColor(color, state->OldDisplay.data());
                Sync(*state, color);
                state->RestoreOld = true;
            }
            ZUIPopBox(ctx);
            Text(ctx, owner, "recentLabel", 0, "Recent", 13, 491, 294, 11, ZUIFontSize::Tiny);
            for (int i = 0; i < 24; ++i)
            {
                const bool filled = static_cast<uint32_t>(i) < ctx->RecentColorCount;
                auto*      recent = At(ctx, owner, "recent", i, 13.f + (i % 12) * 24.f, 508.f + (i / 12) * 26.f, 20, 20, filled ? ZUI_DrawBorder | ZUI_Clickable | ZUI_KeyboardActivate : ZUI_DrawBorder | ZUI_DashedBorder);
                Frame(recent, ctx->Theme.PanelBg, filled ? BorderField : BorderSubtle);
                if (filled)
                {
                    const auto selected = ctx->RecentColors[i];
                    SetSwatch(ctx, recent, selected);
                    if (ZUISignalFromBox(ctx, recent).Flags & ZUI_SignalClicked)
                    {
                        SetRgb(color, {selected.R, selected.G, selected.B});
                        if (alpha && ToByte(color[3]) != selected.A)
                            color[3] = selected.A / 255.f;
                        Sync(*state, color);
                    }
                    if (Bytes(color) == selected)
                    {
                        CopyColor(recent->BorderColor, AccentBlue);
                        recent->BorderThickness = 2.f;
                    }
                    else if (ctx->HotKey == recent->Key)
                        CopyColor(recent->BorderColor, ctx->Theme.TextDim);
                }
                ZUIPopBox(ctx);
            }
            // All views are refreshed after input, including a number/hex commit
            // or Recent click late in the builder. No one-frame stale colors.
            bytes               = Bytes(color);
            hsb                 = state->Hsb;
            hsl                 = state->Hsl;
            data->Color         = {bytes.R, bytes.G, bytes.B};
            data->Hsb           = hsb;
            data->Hsl           = hsl;
            data->Dragging      = state->DragKey == field->Key;
            data->ActiveHandle  = state->ChainTarget;
            data->HoverHandle   = ctx->HotKey == field->Key ? HitChain(data->Color, point) : -1;
            vArrow->FloatPos[1] = 16.f + 192.f * (1.f - (lightness ? hsl.L : hsb.B) / 100.f);
            hArrow->FloatPos[0] = 20.f + 192.f * (lightness ? hsl.S : hsb.S) / 100.f;
            snprintf(hue, sizeof(hue), "%d" ZUI_GLYPH_DEGREE, hsb.H);
            if (auto* font = ctx->GetFont(ZUIFontSize::Body))
                ZUIMeasureText(font, hue, static_cast<uint32_t>(Helpers::secure_strlen(hue)), measured);
            const float  finalBadgeW = measured[0] + 16.f;
            const double finalAngle  = hsb.H * std::numbers::pi / 180.0;
            badge->Size[0]           = ZPx(finalBadgeW);
            badge->FloatPos[0]       = 120.f + 103.f * static_cast<float>(std::cos(finalAngle)) - finalBadgeW * 0.5f;
            badge->FloatPos[1]       = 116.f - 103.f * static_cast<float>(std::sin(finalAngle)) - 10.f;
            badge->Label             = ZUIPushStr(&ctx->FrameArena, hue, static_cast<uint32_t>(Helpers::secure_strlen(hue)));
            const auto finalValues   = ChannelValues(*state, color, ctx->ColorModel);
            for (int i = 0; i < (alpha ? 4 : 3); ++i)
            {
                if (!state->Channels[i].Editing)
                {
                    char value[16] = {};
                    snprintf(value, sizeof(value), "%d", finalValues[i]);
                    textBoxes.Channels[i]->Label = ZUIPushStr(&ctx->FrameArena, value, static_cast<uint32_t>(Helpers::secure_strlen(value)));
                }
                auto& trackData = *trackDatas[i];
                ConfigureTrack(ctx, trackData, *state, color, i);
                trackData.Value     = i == 3 ? color[3] : static_cast<float>(finalValues[i]) / ChannelMaximum(ctx->ColorModel, i);
                trackData.Highlight = ctx->HotKey == channelTracks[i]->Key || state->DragKey == channelTracks[i]->Key;
            }
            SetSwatch(ctx, preview, bytes, &state->OldColor);
            if (!state->HexEditing)
            {
                FormatHex(bytes, state->HexBuffer, sizeof(state->HexBuffer));
                hexBox->Label = ZUIPushStr(&ctx->FrameArena, state->HexBuffer, static_cast<uint32_t>(Helpers::secure_strlen(state->HexBuffer)));
            }
            state->LastDisplay = Snapshot(color);
        }

        bool Edit(ZUIContext* ctx, const char* key, float* color, ZUIColorSpace space, bool alpha, ZUISize width, uintptr_t binding_identity = 0)
        {
            if (!color || !key)
                return false;
            const uint64_t owner = ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key)));
            auto*          state = State(ctx, owner);
            if (!state)
                return false;
            PrepareBinding(ctx, *state, owner, color, alpha, space, binding_identity);
            const bool undone = Undo(ctx, *state, owner, color, alpha, space);
            if (undone)
                state->Initialized = false;
            const auto before = Snapshot(color, alpha);
            if (state->LastEditFrame + 1 != ctx->FrameIndex)
                for (auto& number : state->Numbers)
                    number = {};
            state->LastEditFrame = ctx->FrameIndex;
            float display[4]     = {0.f, 0.f, 0.f, 1.f};
            for (int i = 0; i < (alpha ? 4 : 3); ++i)
                display[i] = static_cast<float>(ToDisplay(color[i], i == 3 ? ZUIColorSpace::Srgb : space));
            if (!state->Initialized || !SameRgb(state->LastDisplay, display))
                Sync(*state, display);
            const auto initialDisplay = Snapshot(display);
            char       rowKey[64]     = {};
            Key(rowKey, owner, "edit");
            auto* row      = ZUIBeginRow(ctx, rowKey, width, ZPx(22.f));
            row->Gap       = 8.f;
            char swKey[64] = {};
            Key(swKey, owner, "swatch");
            auto* swatch    = ZUIPushBox(ctx, swKey, static_cast<uint32_t>(Helpers::secure_strlen(swKey)), ZUI_DrawBorder | ZUI_Clickable | ZUI_KeyboardActivate);
            swatch->Size[0] = swatch->Size[1] = ZPx(22.f);
            const auto signal                 = ZUISignalFromBox(ctx, swatch);
            ZUIPopBox(ctx);
            for (int i = 0; i < (alpha ? 4 : 3); ++i)
            {
                int value = i == 3 ? static_cast<int>(std::round(display[3] * 100.f)) : ToByte(display[i]);
                if (Number(ctx, owner, i, state->Numbers[i], value, -1, ZFill()) && !ctx->Disabled)
                {
                    display[i] = value / (i == 3 ? 100.f : 255.f);
                }
            }
            ZUIEndRow(ctx);
            if (!SameRgb(state->LastDisplay, display))
                Sync(*state, display);
            Frame(swatch, ctx->Theme.PanelBg, ctx->Disabled ? BorderSubtle : (ctx->HotKey == swatch->Key ? BorderStrong : BorderField));
            if (!ctx->Disabled && (signal.Flags & ZUI_SignalClicked))
            {
                const auto* bounds = ZUIStateGetOrInsert(&ctx->StateStore, swatch->Key);
                const float x      = std::clamp(bounds->ScreenMinX, 0.f, std::max(0.f, static_cast<float>(ctx->ScreenW) - 320.f));
                float       y      = bounds->ScreenMaxY;
                if (y + 567.f > ctx->ScreenH)
                    y = bounds->ScreenMinY - 567.f;
                y = std::clamp(y, 0.f, std::max(0.f, static_cast<float>(ctx->ScreenH) - 567.f));
                ZUIOpenPopup(ctx, key, x, y);
                state->LastPickerFrame = 0;
            }
            const bool popupOpen = ZUIBeginPopup(ctx, key);
            if (popupOpen)
            {
                if (!state->LastPickerFrame || state->LastPickerFrame + 1 != ctx->FrameIndex)
                    state->OldStored = before;
                auto* popup    = ctx->Current;
                popup->Size[0] = ZPx(320.f);
                popup->Size[1] = ZPx(567.f);
                ZUIPadding(popup, 0.f);
                popup->FloatPos[0]  = std::clamp(popup->FloatPos[0], 0.f, std::max(0.f, static_cast<float>(ctx->ScreenW) - 320.f));
                popup->FloatPos[1]  = std::clamp(popup->FloatPos[1], 0.f, std::max(0.f, static_cast<float>(ctx->ScreenH) - 567.f));
                char contentKey[64] = {};
                Key(contentKey, owner, "picker");
                auto* content = ZUIBeginColumn(ctx, contentKey, ZPx(320), ZPx(567));
                PickerContent(ctx, content, owner, display, alpha);
                ZUIEndColumn(ctx);
                ZUIEndPopup(ctx);
                CopyColor(swatch->BorderColor, AccentBlue);
            }
            else
            {
                state->DragKey    = 0;
                state->HexEditing = false;
                for (auto& channel : state->Channels)
                    channel = {};
            }
            for (int i = 0; i < (alpha ? 4 : 3); ++i)
            {
                if (state->RestoreOld)
                    color[i] = state->OldStored[i];
                else if (display[i] != initialDisplay[i])
                    color[i] = static_cast<float>(FromDisplay(display[i], i == 3 ? ZUIColorSpace::Srgb : space));
            }
            state->RestoreOld  = false;
            state->LastDisplay = Snapshot(display);
            bool active        = state->DragKey != 0;
            for (const auto& number : state->Numbers)
                active |= number.Pressed;
            for (const auto& number : state->Channels)
                active |= number.Pressed;
            FinishHistory(ctx, *state, owner, before, color, alpha, space, active);
            state->LastStored       = Snapshot(color, alpha);
            state->LastStorageFrame = ctx->FrameIndex;
            SetSwatch(ctx, swatch, Bytes(display));
            return undone || !Same(before, Snapshot(color, alpha));
        }
    } // namespace

    bool ZUIColorEdit4(ZUIContext* ctx, const char* key, float color[4], ZUIColorSpace space, ZUISize width, uintptr_t binding_identity)
    {
        return Edit(ctx, key, color, space, true, width, binding_identity);
    }

    bool ZUIColorEdit3(ZUIContext* ctx, const char* key, float color[3], ZUIColorSpace space, ZUISize width, uintptr_t binding_identity)
    {
        return Edit(ctx, key, color, space, false, width, binding_identity);
    }

    bool ZUIColorPicker4(ZUIContext* ctx, const char* key, float color[4])
    {
        if (!color || !key)
            return false;
        const auto owner = ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key)));
        auto*      state = State(ctx, owner);
        if (!state)
            return false;
        PrepareBinding(ctx, *state, owner, color, true, ZUIColorSpace::Srgb);
        const bool undone = Undo(ctx, *state, owner, color, true, ZUIColorSpace::Srgb);
        if (undone)
            state->Initialized = false;
        const auto before         = Snapshot(color);
        char       contentKey[64] = {};
        Key(contentKey, owner, "picker");
        auto* canvas  = ZUIBeginColumn(ctx, contentKey, ZPx(320), ZPx(567));
        canvas->Flags = ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow;
        Frame(canvas, ctx->Theme.PopupBg, ctx->Theme.PopupBorder, Metrics::Radius6);
        float display[4] = {};
        for (int i = 0; i < 4; ++i)
            display[i] = static_cast<float>(ToDisplay(color[i], ZUIColorSpace::Srgb));
        const auto initialDisplay = Snapshot(display);
        if (!state->LastPickerFrame || state->LastPickerFrame + 1 != ctx->FrameIndex)
            state->OldStored = before;
        PickerContent(ctx, canvas, owner, display, true);
        ZUIEndColumn(ctx);
        for (int i = 0; i < 4; ++i)
        {
            if (state->RestoreOld)
                color[i] = state->OldStored[i];
            else if (display[i] != initialDisplay[i])
                color[i] = display[i];
        }
        state->RestoreOld = false;
        bool active       = state->DragKey != 0;
        for (const auto& number : state->Channels)
            active |= number.Pressed;
        FinishHistory(ctx, *state, owner, before, color, true, ZUIColorSpace::Srgb, active);
        state->LastStored       = Snapshot(color);
        state->LastStorageFrame = ctx->FrameIndex;
        return undone || !Same(before, Snapshot(color));
    }
} // namespace ZEngine::UI
