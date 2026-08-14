#pragma once
#include <ZEngine/Core/VFS/VFSPath.h>
#include <imgui.h>

namespace Tetragrama::Components
{
    enum class ContentIconType
    {
        Folder,
        Texture,   // .png .jpg .jpeg .tga .bmp .dds .hdr .exr
        Mesh,      // .obj .fbx .gltf .glb .ply .stl .zemesh
        Shader,    // .glsl .vert .frag .geom .comp .hlsl .tesc .tese .rgen .rchit .rmiss
        Material,  // .mat .zemat
        Audio,     // .wav .mp3 .ogg .flac .aiff
        Scene,     // .scene .zescene .level
        Config,    // .json .yaml .yml .xml .toml .ini .cfg
        CppSource, // .cpp .cc .cxx .c .h .hpp .hxx .inl
        Meta,      // .meta
        Generic,
    };

    inline ContentIconType GetContentIconType(bool is_dir, const ZEngine::Core::VFS::VFSPathComponent& ext)
    {
        if (is_dir)
            return ContentIconType::Folder;
        if (ext.Empty())
            return ContentIconType::Generic;

        if (ext.Equals(".png") || ext.Equals(".jpg") || ext.Equals(".jpeg") || ext.Equals(".tga") || ext.Equals(".bmp") || ext.Equals(".dds") || ext.Equals(".hdr") || ext.Equals(".exr"))
            return ContentIconType::Texture;

        if (ext.Equals(".obj") || ext.Equals(".fbx") || ext.Equals(".gltf") || ext.Equals(".glb") || ext.Equals(".ply") || ext.Equals(".stl") || ext.Equals(".zemesh"))
            return ContentIconType::Mesh;

        if (ext.Equals(".glsl") || ext.Equals(".vert") || ext.Equals(".frag") || ext.Equals(".geom") || ext.Equals(".comp") || ext.Equals(".hlsl") || ext.Equals(".tesc") || ext.Equals(".tese") || ext.Equals(".rgen") || ext.Equals(".rchit") || ext.Equals(".rmiss"))
            return ContentIconType::Shader;

        if (ext.Equals(".mat") || ext.Equals(".zemat"))
            return ContentIconType::Material;

        if (ext.Equals(".wav") || ext.Equals(".mp3") || ext.Equals(".ogg") || ext.Equals(".flac") || ext.Equals(".aiff"))
            return ContentIconType::Audio;

        if (ext.Equals(".scene") || ext.Equals(".zescene") || ext.Equals(".level"))
            return ContentIconType::Scene;

        if (ext.Equals(".json") || ext.Equals(".yaml") || ext.Equals(".yml") || ext.Equals(".xml") || ext.Equals(".toml") || ext.Equals(".ini") || ext.Equals(".cfg"))
            return ContentIconType::Config;

        if (ext.Equals(".cpp") || ext.Equals(".cc") || ext.Equals(".cxx") || ext.Equals(".c") || ext.Equals(".h") || ext.Equals(".hpp") || ext.Equals(".hxx") || ext.Equals(".inl"))
            return ContentIconType::CppSource;

        if (ext.Equals(".meta"))
            return ContentIconType::Meta;

        return ContentIconType::Generic;
    }

    // Draws a flat/minimal content-browser icon into dl.
    // ixo        : top-left of the icon area (same as the caller's ixo)
    // ic         : icon size (sz * 0.85f)
    // dark_theme : true for dark editor backgrounds
    //
    // When a per-asset thumbnail is available, call dl->AddImage() instead and
    // skip this function. This icon acts as the no-thumbnail fallback.
    inline void DrawContentIcon(ImDrawList* dl, ImVec2 ixo, float ic, ContentIconType type, bool dark_theme)
    {
        if (type == ContentIconType::Folder)
        {
            const ImU32 body = dark_theme ? IM_COL32(200, 175, 100, 255) : IM_COL32(185, 155, 75, 255);
            const ImU32 tab  = dark_theme ? IM_COL32(220, 195, 120, 255) : IM_COL32(205, 175, 95, 255);
            const float tw   = ic * 0.42f;
            const float th   = ic * 0.14f;
            const float by   = ixo.y + th;
            dl->AddRectFilled({ixo.x, ixo.y}, {ixo.x + tw, by + 1.0f}, tab, 2.0f);
            dl->AddRectFilled({ixo.x, by}, {ixo.x + ic, ixo.y + ic * 0.92f}, body, 2.0f);
            return;
        }

        ImU32 body_col, fold_col, sym_col;
        switch (type)
        {
            case ContentIconType::Texture:
                body_col = dark_theme ? IM_COL32(230, 130, 50, 255) : IM_COL32(210, 110, 35, 255);
                fold_col = dark_theme ? IM_COL32(180, 100, 30, 255) : IM_COL32(165, 85, 20, 255);
                sym_col  = dark_theme ? IM_COL32(255, 230, 190, 255) : IM_COL32(100, 55, 10, 255);
                break;
            case ContentIconType::Mesh:
                body_col = dark_theme ? IM_COL32(80, 150, 230, 255) : IM_COL32(55, 125, 210, 255);
                fold_col = dark_theme ? IM_COL32(50, 110, 190, 255) : IM_COL32(35, 95, 170, 255);
                sym_col  = dark_theme ? IM_COL32(205, 225, 255, 255) : IM_COL32(20, 60, 130, 255);
                break;
            case ContentIconType::Shader:
                body_col = dark_theme ? IM_COL32(155, 85, 220, 255) : IM_COL32(135, 65, 200, 255);
                fold_col = dark_theme ? IM_COL32(115, 55, 180, 255) : IM_COL32(100, 40, 165, 255);
                sym_col  = dark_theme ? IM_COL32(230, 205, 255, 255) : IM_COL32(75, 25, 140, 255);
                break;
            case ContentIconType::Material:
                body_col = dark_theme ? IM_COL32(50, 195, 175, 255) : IM_COL32(35, 170, 155, 255);
                fold_col = dark_theme ? IM_COL32(30, 150, 135, 255) : IM_COL32(20, 130, 118, 255);
                sym_col  = dark_theme ? IM_COL32(190, 255, 245, 255) : IM_COL32(10, 85, 75, 255);
                break;
            case ContentIconType::Audio:
                body_col = dark_theme ? IM_COL32(75, 200, 105, 255) : IM_COL32(50, 175, 80, 255);
                fold_col = dark_theme ? IM_COL32(45, 160, 75, 255) : IM_COL32(30, 140, 55, 255);
                sym_col  = dark_theme ? IM_COL32(200, 255, 215, 255) : IM_COL32(15, 90, 35, 255);
                break;
            case ContentIconType::Scene:
                body_col = dark_theme ? IM_COL32(230, 200, 50, 255) : IM_COL32(205, 175, 30, 255);
                fold_col = dark_theme ? IM_COL32(185, 160, 30, 255) : IM_COL32(165, 140, 15, 255);
                sym_col  = dark_theme ? IM_COL32(255, 245, 185, 255) : IM_COL32(105, 85, 5, 255);
                break;
            case ContentIconType::Config:
                body_col = dark_theme ? IM_COL32(125, 160, 205, 255) : IM_COL32(95, 130, 180, 255);
                fold_col = dark_theme ? IM_COL32(90, 120, 170, 255) : IM_COL32(65, 98, 150, 255);
                sym_col  = dark_theme ? IM_COL32(220, 230, 248, 255) : IM_COL32(40, 65, 115, 255);
                break;
            case ContentIconType::CppSource:
                body_col = dark_theme ? IM_COL32(220, 80, 80, 255) : IM_COL32(195, 55, 55, 255);
                fold_col = dark_theme ? IM_COL32(175, 50, 50, 255) : IM_COL32(155, 30, 30, 255);
                sym_col  = dark_theme ? IM_COL32(255, 210, 210, 255) : IM_COL32(105, 15, 15, 255);
                break;
            case ContentIconType::Meta:
                body_col = dark_theme ? IM_COL32(130, 120, 200, 255) : IM_COL32(105, 95, 175, 255);
                fold_col = dark_theme ? IM_COL32(95, 85, 160, 255) : IM_COL32(75, 65, 140, 255);
                sym_col  = dark_theme ? IM_COL32(220, 215, 255, 255) : IM_COL32(50, 40, 120, 255);
                break;
            default: // Generic
                body_col = dark_theme ? IM_COL32(160, 160, 165, 255) : IM_COL32(130, 130, 135, 255);
                fold_col = dark_theme ? IM_COL32(120, 120, 125, 255) : IM_COL32(95, 95, 100, 255);
                sym_col  = dark_theme ? IM_COL32(220, 220, 225, 255) : IM_COL32(55, 55, 60, 255);
                break;
        }

        // Document base with dog-ear fold
        const float  f  = ic * 0.22f;
        const ImVec2 tl = {ixo.x + ic * 0.08f, ixo.y + ic * 0.04f};
        const ImVec2 br = {ixo.x + ic * 0.92f, ixo.y + ic * 0.96f};
        dl->AddRectFilled({tl.x, tl.y + f}, br, body_col, 2.0f);
        dl->AddRectFilled(tl, {br.x - f, tl.y + f}, body_col);
        dl->AddTriangleFilled({br.x - f, tl.y}, {br.x, tl.y + f}, {br.x - f, tl.y + f}, fold_col);

        // Symbol drawn in the body area below the fold line
        const float cx     = (tl.x + br.x) * 0.5f;
        const float area_t = tl.y + f;
        const float area_b = br.y;
        const float cy     = (area_t + area_b) * 0.5f;
        const float area_h = area_b - area_t;
        const float area_w = br.x - tl.x;

        switch (type)
        {
            case ContentIconType::Texture:
            {
                // Sun circle + mountain triangle
                const float sr  = area_h * 0.16f;
                const float scx = cx - area_w * 0.12f;
                const float scy = area_t + area_h * 0.28f;
                dl->AddCircleFilled({scx, scy}, sr, sym_col, 10);
                const float my = area_t + area_h * 0.82f;
                dl->AddTriangleFilled({tl.x + area_w * 0.10f, my}, {tl.x + area_w * 0.90f, my}, {cx, area_t + area_h * 0.42f}, sym_col);
                break;
            }
            case ContentIconType::Mesh:
            {
                // Isometric cube: flat-top hexagon + 3 inner Y-lines
                const float  r      = area_h * 0.30f;
                const float  rx     = r * 0.866f; // cos(30)
                const float  ry     = r * 0.5f;   // sin(30)
                const ImVec2 vtop   = {cx, cy - r};
                const ImVec2 vtr    = {cx + rx, cy - ry};
                const ImVec2 vbr    = {cx + rx, cy + ry};
                const ImVec2 vbot   = {cx, cy + r};
                const ImVec2 vbl    = {cx - rx, cy + ry};
                const ImVec2 vtl    = {cx - rx, cy - ry};
                const ImVec2 vctr   = {cx, cy};
                const float  lw     = 1.5f;
                ImVec2       hex[6] = {vtop, vtr, vbr, vbot, vbl, vtl};
                dl->AddPolyline(hex, 6, sym_col, ImDrawFlags_Closed, lw);
                dl->AddLine(vtop, vctr, sym_col, lw);
                dl->AddLine(vbl, vctr, sym_col, lw);
                dl->AddLine(vbr, vctr, sym_col, lw);
                break;
            }
            case ContentIconType::Shader:
            {
                // GPU render triangle
                const float th = area_h * 0.55f;
                const float tw = area_w * 0.58f;
                dl->AddTriangleFilled({cx, cy - th * 0.5f}, {cx + tw * 0.5f, cy + th * 0.5f}, {cx - tw * 0.5f, cy + th * 0.5f}, sym_col);
                break;
            }
            case ContentIconType::Material:
            {
                // Sphere with specular highlight
                const float r  = area_h * 0.32f;
                const ImU32 hi = IM_COL32(255, 255, 255, dark_theme ? 55 : 80);
                dl->AddCircleFilled({cx, cy}, r, sym_col, 20);
                dl->AddCircleFilled({cx - r * 0.28f, cy - r * 0.28f}, r * 0.35f, hi, 12);
                break;
            }
            case ContentIconType::Audio:
            {
                // Quarter note: filled head + vertical stem
                const float nr = area_h * 0.18f;
                const float nx = cx - nr * 0.3f;
                const float ny = area_t + area_h * 0.72f;
                dl->AddCircleFilled({nx, ny}, nr, sym_col, 10);
                dl->AddLine({nx + nr * 0.9f, ny - nr * 0.2f}, {nx + nr * 0.9f, area_t + area_h * 0.18f}, sym_col, 1.5f);
                break;
            }
            case ContentIconType::Scene:
            {
                // Ground line + building rectangle + sun circle
                const float gy = area_t + area_h * 0.78f;
                dl->AddLine({tl.x + area_w * 0.06f, gy}, {br.x - area_w * 0.06f, gy}, sym_col, 1.5f);
                const float bw = area_w * 0.32f;
                const float bh = area_h * 0.40f;
                dl->AddRectFilled({cx - bw * 0.5f, gy - bh}, {cx + bw * 0.5f, gy}, sym_col, 1.5f);
                const float sr  = area_h * 0.10f;
                const float scx = tl.x + area_w * 0.76f;
                const float scy = area_t + area_h * 0.22f;
                dl->AddCircleFilled({scx, scy}, sr, sym_col, 8);
                break;
            }
            case ContentIconType::Config:
            {
                // Three horizontal data lines (full / short / full)
                const float lh  = 1.5f;
                const float gap = area_h * 0.20f;
                const float lx0 = tl.x + area_w * 0.10f;
                const float lx1 = br.x - area_w * 0.10f;
                float       ly  = area_t + area_h * 0.22f;
                for (int i = 0; i < 3; ++i)
                {
                    const float rx1 = (i == 1) ? lx0 + (lx1 - lx0) * 0.62f : lx1;
                    dl->AddRectFilled({lx0, ly}, {rx1, ly + lh + 0.5f}, sym_col);
                    ly += gap;
                }
                break;
            }
            case ContentIconType::Meta:
            {
                // Info "i": dot above a vertical bar
                const float bar_w  = area_w * 0.12f;
                const float bar_h  = area_h * 0.38f;
                const float dot_r  = bar_w * 1.1f;
                const float bar_x0 = cx - bar_w * 0.5f;
                const float bar_y0 = cy - bar_h * 0.1f;
                dl->AddCircleFilled({cx, cy - bar_h * 0.52f - dot_r}, dot_r, sym_col, 10);
                dl->AddRectFilled({bar_x0, bar_y0}, {bar_x0 + bar_w, bar_y0 + bar_h}, sym_col, 1.0f);
                break;
            }
            case ContentIconType::CppSource:
            {
                // < > angle brackets
                const float aw = area_w * 0.22f;
                const float ah = area_h * 0.38f;
                const float lw = 2.0f;
                const float lx = cx - area_w * 0.32f;
                const float rx = cx + area_w * 0.32f;
                dl->AddLine({lx + aw, cy - ah * 0.5f}, {lx, cy}, sym_col, lw);
                dl->AddLine({lx, cy}, {lx + aw, cy + ah * 0.5f}, sym_col, lw);
                dl->AddLine({rx - aw, cy - ah * 0.5f}, {rx, cy}, sym_col, lw);
                dl->AddLine({rx, cy}, {rx - aw, cy + ah * 0.5f}, sym_col, lw);
                break;
            }
            default:
                break;
        }
    }

} // namespace Tetragrama::Components
