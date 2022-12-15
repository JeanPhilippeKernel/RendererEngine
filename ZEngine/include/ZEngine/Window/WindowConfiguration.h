#pragma once
#include <string>
#include <vector>
#include <ZEngineDef.h>

namespace ZEngine::Layers
{
    class Layer;
}

namespace ZEngine::Window
{
    struct WindowConfiguration
    {
        uint32_t                                          Width{0};
        uint32_t                                          Height{0};
        bool                                              EnableVsync{false};
        std::string                                       Title;
        std::vector<ZEngine::Ref<ZEngine::Layers::Layer>> RenderingLayerCollection;
        std::vector<ZEngine::Ref<ZEngine::Layers::Layer>> OverlayLayerCollection;
    };

} // namespace ZEngine::Window
