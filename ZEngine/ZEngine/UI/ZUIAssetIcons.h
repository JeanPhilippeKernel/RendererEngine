#pragma once
#include <ZEngine/UI/ZUIIcons.h>
#include <cstdint>

namespace ZEngine::UI
{
    // Grayscale coverage from the Figma exports and custom engine mark, not scaled toolbar
    // glyphs. Keep this order in sync with Scripts/bake_content_icons.swift.
    inline constexpr uint32_t kZUIAssetIconPixels = 152;
    inline constexpr ZUIIcon  kZUIAssetIcons[]    = {
        ZUIIcon::Folder,
        ZUIIcon::Box,
        ZUIIcon::Image,
        ZUIIcon::Map,
        ZUIIcon::Workflow,
        ZUIIcon::Code,
        ZUIIcon::Square,
        ZUIIcon::FolderOpen,
        ZUIIcon::Search,
        ZUIIcon::Engine,
    };
    inline constexpr uint32_t kZUIAssetIconCount = sizeof(kZUIAssetIcons) / sizeof(kZUIAssetIcons[0]);
} // namespace ZEngine::UI
