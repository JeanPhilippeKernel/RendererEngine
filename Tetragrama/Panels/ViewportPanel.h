#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <cstdint>

namespace Tetragrama::Panels
{
    struct ViewportPanel : ZEngine::UI::ZUIPanelView
    {
        ViewportPanel()
        {
            Title = "Viewport";
        }

        Tetragrama::Layers::ZUILayer* m_layer = nullptr;

        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        ZEngine::Rendering::Textures::TextureHandle m_scene_texture = {};
        uint32_t                                    m_last_w        = 0;
        uint32_t                                    m_last_h        = 0;

        // Gizmo operation: -1=none, 0=translate, 1=rotate, 2=scale
        int   m_gizmo_op     = -1;
        bool  m_grid_enabled = true;
        float m_fps_ema      = 0.f;
    };
} // namespace Tetragrama::Panels
