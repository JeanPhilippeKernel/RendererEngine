#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <cstdint>

namespace Tetragrama
{
    class EditorSession;
}

namespace ZEngine::Controllers
{
    struct ICameraController;
}

namespace Tetragrama::Panels
{
    /// @brief Scene viewport panel.  Displays the renderer output texture,
    ///        exposes a floating gizmo/grid toolbar, accepts drag-drop of scene
    ///        and mesh files, drives viewport-hover for the camera controller,
    ///        and emits render-target resize requests on layout changes.
    struct ViewportPanel : ZEngine::UI::ZUIPanelView
    {
        ViewportPanel()
        {
            Title = "Viewport";
        }

        Tetragrama::Layers::ZUILayer* m_layer = nullptr;

        void                          Initialize(Tetragrama::Layers::ZUILayer* layer);

        /// @brief Builds the viewport image, overlay toolbar, and FPS counter.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void                          BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        friend struct ViewportPanelTestAccess;
        void                                        BuildProjectionControl(ZEngine::UI::ZUIContext* ctx, const EditorSession& session, ZEngine::Controllers::ICameraController* controller);
        void                                        BuildTransformToolbar(ZEngine::UI::ZUIContext* ctx, EditorSession& session);
        ZEngine::Rendering::Textures::TextureHandle m_scene_texture = {};
        uint32_t                                    m_last_w        = 0;
        uint32_t                                    m_last_h        = 0;
    };
} // namespace Tetragrama::Panels
