#pragma once
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

namespace Tetragrama::Panels
{
    /// Resolve files from the editor's project/engine mounts only at drop time, never per frame.
    /// Native picker/OS paths keep their original case, drive letter, and separators.
    inline bool ResolveDroppedFile(const char* path, ZEngine::UI::ZUIDragPayloadKind kind, const char* project_root, const char* engine_root, char* out, size_t out_size)
    {
        using namespace ZEngine::Core::VFS;
        using namespace ZEngine::Helpers;
        using ZEngine::UI::ZUIDragPayloadKind;
        if (!out || !out_size)
            return false;
        out[0] = '\0';
        if (!path || !path[0])
            return false;
        if (kind == ZUIDragPayloadKind::NativePath)
            return secure_strcpy(out, out_size, path) == MEMORY_OP_SUCCESS;
        if (kind != ZUIDragPayloadKind::VFSPath || path[0] != '/')
            return false;
        auto logical = VFSPath::Parse(path);
        if (logical.Failed() || logical.Value().IsRoot())
            return false;
        const char* root = project_root;
        if (logical.Value().ComponentAt(0).Equals("ZodiacEngine"))
        {
            root    = engine_root;
            logical = VFSPath::Parse(logical.Value().CStr() + sizeof("/ZodiacEngine") - 1);
            if (logical.Failed() || logical.Value().IsRoot())
                return false;
        }
        const size_t root_len = secure_strlen(root);
        if (!root_len || root_len >= out_size || logical.Value().Length() >= out_size - root_len)
            return false;
        // ResolveNative itself permits truncation; the exact bound above rules it out.
        logical.Value().ResolveNative(root, out, out_size);
        return true;
    }

    /// @brief Fills the full panel area with a solid background, optionally
    ///        centering a dim message label when the panel has no content to show.
    /// @param ctx ZUI context for the current frame.
    /// @param key Unique box key string for this background element.
    /// @param col Background RGBA color.
    /// @param msg Optional centered message; pass nullptr or empty to show nothing.
    inline void EmptyPanelBg(ZEngine::UI::ZUIContext* ctx, const char* key, const float col[4], const char* msg)
    {
        using namespace ZEngine::UI;
        ZUIBox* bg = ZUIBeginColumn(ctx, key, ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, col);
        bg->EdgeSoftness = 0.f;
        if (msg && msg[0])
        {
            {
                char fk[48] = {};
                snprintf(fk, sizeof(fk), "##ept_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) ZEngine::Helpers::secure_strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
            {
                char lk[48] = {};
                snprintf(lk, sizeof(lk), "##epl_%s", key);
                uint32_t mlen     = (uint32_t) ZEngine::Helpers::secure_strlen(msg);
                ZUIBox*  lbl      = ZUIPushBox(ctx, lk, (uint32_t) ZEngine::Helpers::secure_strlen(lk), ZUI_DrawText);
                lbl->Size[0]      = ZFill();
                lbl->Size[1]      = ZText();
                lbl->TextAlign    = ZUITextAlign::Center;
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, msg, mlen);
                lbl->TextColor[0] = ctx->Theme.TextDim[0];
                lbl->TextColor[1] = ctx->Theme.TextDim[1];
                lbl->TextColor[2] = ctx->Theme.TextDim[2];
                lbl->TextColor[3] = ctx->Theme.TextDim[3];
                ZUIPopBox(ctx);
            }
            {
                char fk[48] = {};
                snprintf(fk, sizeof(fk), "##epb_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) ZEngine::Helpers::secure_strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
        }
        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Panels
