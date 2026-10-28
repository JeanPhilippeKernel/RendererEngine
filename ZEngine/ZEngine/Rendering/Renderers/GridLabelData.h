#pragma once
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Scenes/SceneGridRenderSnapshot.h>
#include <ZEngine/Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Renderers
{
    struct GridLabelGlyph
    {
        Core::Containers::FixedArray<float, 4> UV   = {};
        Core::Maths::Vec2f                     Size = {}; // logical pixels, after font scaling
    };

    /// Main-thread font capture. No atlas/font/glyph pointers cross the mailbox.
    /// The existing editor atlas texture keeps its application-owned lifetime.
    struct GridLabelFontSnapshot
    {
        Core::Containers::FixedArray<GridLabelGlyph, 2> Glyphs                = {}; // U, V
        Textures::TextureHandle                         Atlas                 = {};
        float                                           LogicalViewportHeight = 0.f;
    };

    struct GridLabelPushConstantData
    {
        Core::Containers::FixedArray<Core::Containers::FixedArray<float, 4>, 2> Rects  = {}; // NDC origin/size
        Core::Containers::FixedArray<Core::Containers::FixedArray<float, 4>, 2> UVs    = {};
        Core::Containers::FixedArray<Core::Containers::FixedArray<float, 4>, 2> Colors = {};
        Core::Containers::FixedArray<float, 4>                                  Depths = {}; // packed label depths, reverse-Z flag, reserved zero
    };
    static_assert(sizeof(GridLabelPushConstantData) == 112);
    static_assert(offsetof(GridLabelPushConstantData, UVs) == 32 && offsetof(GridLabelPushConstantData, Colors) == 64 && offsetof(GridLabelPushConstantData, Depths) == 96);

    struct GridLabelFrameData
    {
        GridLabelPushConstantData Push  = {};
        Textures::TextureHandle   Atlas = {};
        uint32_t                  Count = 0;
    };

    /// Overlay layout policy, not authored Grid spacing. Keep labels close enough
    /// to identify the origin while leaving room for gizmos and axis lines.
    inline constexpr float GridLabelAxisOffset = 96.f;
    /// The body font is sized for dense UI. Axis labels need a distinct display
    /// scale to stay legible against a high-frequency grid.
    inline constexpr float GridLabelTextScale  = 1.5f;

    /// Build at most two neutral local-axis labels from one retained camera/Grid.
    /// Disabled/missing-font/hidden labels yield an empty frame. Invalid inputs
    /// preserve output. Fixed value storage; no allocation, scene or UI access.
    [[nodiscard]] bool     BuildGridLabelFrame(const Scenes::SceneGridRenderSnapshot& grid, const Cameras::CameraFrameData& camera, const GridLabelFontSnapshot& font, uint32_t width, uint32_t height, GridLabelFrameData& output);
    [[nodiscard]] bool     ValidGridLabelFrame(const GridLabelFrameData& frame);
} // namespace ZEngine::Rendering::Renderers
