#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <ZEngine/Rendering/Textures/Texture.h>

namespace Tetragrama::Components
{
    class ZUISceneViewportComponent : public ZUIComponent
    {
    public:
        ZUISceneViewportComponent()          = default;
        ~ZUISceneViewportComponent() override = default;

        void Initialize(Tetragrama::Layers::ZUILayer* parent,
                        cstring name       = "Scene",
                        bool    visibility = true) override;

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

    private:
        ZEngine::Rendering::Textures::TextureHandle m_scene_texture = {};
        uint32_t m_last_w = 0;
        uint32_t m_last_h = 0;
    };
    ZDEFINE_PTR(ZUISceneViewportComponent);
} // namespace Tetragrama::Components
