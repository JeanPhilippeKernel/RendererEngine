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
        uint32_t                                          Width{1500};
        uint32_t                                          Height{800};
        bool                                              EnableVsync{true};
        std::string                                       Title;
        std::vector<ZEngine::Ref<ZEngine::Layers::Layer>> RenderingLayerCollection;
        std::vector<ZEngine::Ref<ZEngine::Layers::Layer>> OverlayLayerCollection;
    };

} // namespace ZEngine::Window
