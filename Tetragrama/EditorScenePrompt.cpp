#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorSceneIO.h>
#include <ZEngine/UI/ZUITokens.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <algorithm>

using namespace ZEngine::UI;

namespace Tetragrama
{
    namespace
    {
        constexpr char ModalKey[]   = "##editor_scene_change";
        constexpr char SaveKey[]    = "Save##scene_change_save";
        constexpr char DiscardKey[] = "Discard##scene_change_discard";
        constexpr char CancelKey[]  = "Cancel##scene_change_cancel";

        uint64_t       ModalHash()
        {
            return ZUIHashStr(ModalKey, sizeof(ModalKey) - 1);
        }

        bool ChoiceKey(uint64_t key)
        {
            return key && (key == ZUIHashStr(SaveKey, sizeof(SaveKey) - 1) || key == ZUIHashStr(DiscardKey, sizeof(DiscardKey) - 1) || key == ZUIHashStr(CancelKey, sizeof(CancelKey) - 1));
        }
    } // namespace

    bool Editor::ChooseSceneChange(SceneChangeChoice choice, uint64_t prompt_serial)
    {
        return !m_closing && m_scene_change.Choose(choice, prompt_serial);
    }

    SceneChangeGate Editor::GateSceneChange(SceneChangeKind kind)
    {
        auto* owner = static_cast<EditorScene*>(CurrentScene);
        if (!owner || !Session.InMutationPhase())
        {
            return SceneChangeGate::Cancelled;
        }
        const auto epoch = Session.SceneEpoch();
        const auto gate  = m_scene_change.Evaluate(kind, epoch, Session.DocumentRevision(), owner->IsDirty(), owner->ExternalRevision());
        if (gate != SceneChangeGate::Save)
        {
            return gate;
        }
        // Native picker invocation is main-thread work outside UI construction.
        // No saved checkpoint or discard authorization is fabricated here.
        SaveScene();
        if (!(m_scene_save.Pending && m_scene_save.Owner.Matches(*this)) && !(m_scene_save_picker.valid() && m_scene_save_picker_owner.Matches(*this)))
        {
            m_scene_change.SaveFinished(epoch, false);
        }
        return SceneChangeGate::Waiting;
    }

    void Editor::ProcessSceneChangeInput(ZUIContext* ctx, bool focused)
    {
        if (!ctx || !focused || !ctx->EscapePressed)
        {
            return;
        }
        const auto prompt = SceneChangePromptState();
        const auto key    = ModalHash();
        if (!prompt.Visible() || (ctx->ActiveModalKey && ctx->ActiveModalKey != key) || (ctx->ModalBox && ctx->ModalBox->Key != key))
        {
            return;
        }
        // Keyboard cancellation must not depend on another overlay being built.
        // Queue intent before the mutation phase; another modal keeps its Esc.
        if (ChooseSceneChange(SceneChangeChoice::Cancel, prompt.Serial))
        {
            ctx->EscapePressed = false;
        }
    }

    void Editor::SyncSceneChangeCapture(ZUIContext* ctx)
    {
        if (!ctx)
        {
            return;
        }
        const auto key = ModalHash();
        if (SceneChangePromptState().Visible())
        {
            // Never replace another modal owner. Ours will build when it closes.
            if (!ctx->ActiveModalKey || ctx->ActiveModalKey == key)
            {
                ctx->ActiveModalKey  = key;
                // Background menus must not become modal-owned popup exceptions.
                // This confirmation has no child popups of its own.
                ctx->PopupStackSize  = 0;
                ctx->PendingPopupKey = 0;
            }
            ctx->InteractionCapture = {true, true};
        }
        else if (ctx->ActiveModalKey == key)
        {
            ctx->ActiveModalKey = 0;
            if (ctx->ModalBox && ctx->ModalBox->Key == key)
            {
                ctx->ModalBox = nullptr;
            }
            if (ChoiceKey(ctx->ActiveKey))
            {
                ctx->ActiveKey = 0;
            }
            if (ChoiceKey(ctx->HotKey))
            {
                ctx->HotKey = 0;
            }
        }
    }

    void Editor::BuildSceneChangePrompt(ZUIContext* ctx)
    {
        if (!ctx || !ctx->Root)
        {
            return;
        }
        SyncSceneChangeCapture(ctx);
        const auto prompt = SceneChangePromptState();
        if (!prompt.Visible() || !ZUIBeginModal(ctx, ModalKey, nullptr))
        {
            return;
        }
        // Reuse CB11's modal/Button recipe; this prompt needs no new artwork.
        constexpr float width = 288.f, padding = Metrics::Space16 + 1.f;
        constexpr float inner = width - padding * 2.f;
        auto*           panel = ctx->Current;
        panel->Size[0]        = ZPx(width);
        panel->Gap            = Metrics::Space10;
        panel->ShadowKind     = ZUIShadowKind::Modal;
        ZUIBoxSetCornerRadius(panel, Metrics::Radius6);
        constexpr float background[4] = ZUI_TOKEN(Tokens::BgPanel);
        constexpr float border[4]     = ZUI_TOKEN(Tokens::BorderStrong);
        constexpr float text[4]       = ZUI_TOKEN(Tokens::TextPrimary);
        constexpr float secondary[4]  = ZUI_TOKEN(Tokens::TextSecondary);
        constexpr float error[4]      = ZUI_TOKEN(Tokens::AccentRed);
        ZUIBoxSetColorArr(panel, background);
        for (uint32_t side = 0; side < 4; ++side)
        {
            panel->Padding[side]     = padding;
            panel->BorderColor[side] = border[side];
        }
        float    height = padding * 2.f;
        uint32_t rows   = 0;
        auto     label  = [&](const char* value, const float color[4], ZUIFontSize size, const char* key) {
            const auto* box  = ZUILabelEllipsized(ctx, value, inner, color, size, key);
            height          += box->Size[1].Value;
            ++rows;
        };
        label("Unsaved scene changes", text, ZUIFontSize::Heading, "##scene_change_title");
        std::string_view name  = {};
        const auto*      owner = static_cast<const EditorScene*>(CurrentScene);
        label(owner && TrySceneSourceName(owner->Name, &name) ? owner->Name : "Untitled Scene", secondary, ZUIFontSize::Body, "##scene_change_name");
        label("Save your changes before continuing?", text, ZUIFontSize::Body, "##scene_change_body");
        label("Discard loses unsaved changes.", secondary, ZUIFontSize::Small, "##scene_change_warning");
        if (prompt.Saving || prompt.SaveFailed)
        {
            label(prompt.Saving ? "Waiting for scene save..." : "Save failed. Retry or cancel.", prompt.SaveFailed ? error : secondary, ZUIFontSize::Small, "##scene_change_status");
        }

        auto* buttons      = ZUIBeginRow(ctx, "##scene_change_buttons", ZFill(), ZPx(Metrics::HeightControl));
        buttons->MainAlign = ZUIAlign::End;
        buttons->Gap       = Metrics::Space8;
        const bool cancel  = (ZUIButton(ctx, CancelKey, ZPx(72.f), ZPx(Metrics::HeightControl), ZUIButtonKind::Secondary).Flags & ZUI_SignalClicked) != 0;
        if (prompt.Saving)
        {
            ZUIBeginDisabled(ctx);
        }
        const bool discard = (ZUIButton(ctx, DiscardKey, ZPx(80.f), ZPx(Metrics::HeightControl), ZUIButtonKind::Secondary).Flags & ZUI_SignalClicked) != 0;
        const bool save    = (ZUIButton(ctx, SaveKey, ZPx(72.f), ZPx(Metrics::HeightControl), ZUIButtonKind::Primary).Flags & ZUI_SignalClicked) != 0;
        if (prompt.Saving)
        {
            ZUIEndDisabled(ctx);
        }
        ZUIEndRow(ctx);
        height             += Metrics::HeightControl + rows * Metrics::Space10;
        panel->Size[1]      = ZPx(height);
        panel->FloatPos[0]  = std::max(0.f, (ctx->ScreenW - width) * 0.5f);
        panel->FloatPos[1]  = std::max(0.f, (ctx->ScreenH - height) * 0.5f);
        if (!prompt.Saving && ctx->FocusTrapKey == ModalHash() && m_scene_prompt_focus != prompt.Serial)
        {
            ctx->FocusKey        = ZUIHashStr(SaveKey, sizeof(SaveKey) - 1);
            m_scene_prompt_focus = prompt.Serial;
        }
        if (cancel || ctx->EscapePressed)
        {
            (void) ChooseSceneChange(SceneChangeChoice::Cancel, prompt.Serial);
            ctx->EscapePressed = false;
        }
        else if (save && !prompt.Saving)
        {
            (void) ChooseSceneChange(SceneChangeChoice::Save, prompt.Serial);
        }
        else if (discard && !prompt.Saving)
        {
            (void) ChooseSceneChange(SceneChangeChoice::Discard, prompt.Serial);
        }
        ZUIEndModal(ctx);
    }
} // namespace Tetragrama
