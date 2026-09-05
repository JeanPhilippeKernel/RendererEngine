#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <cstdint>

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

        /// @brief Builds the viewport image, overlay toolbar, and FPS counter.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void                          BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

        /// @brief Main-thread only. Spawns an Actor for the mesh dropped into m_pending_mesh_drop.
        void                          SpawnDroppedMesh();

        /// @brief Main-thread only. Opens the scene at the path dropped into m_pending_scene_drop.
        void                          OpenDroppedScene();

    private:
        ZEngine::Rendering::Textures::TextureHandle m_scene_texture           = {};
        uint32_t                                    m_last_w                  = 0;
        uint32_t                                    m_last_h                  = 0;

        // Written on drop, consumed by the MainThreadScheduler trampolines above.
        char                                        m_pending_mesh_drop[512]  = {};
        char                                        m_pending_scene_drop[512] = {};

        // Gizmo operation: -1=none, 0=translate, 1=rotate, 2=scale
        int                                         m_gizmo_op                = -1;
        bool                                        m_grid_enabled            = true;
    };
} // namespace Tetragrama::Panels
