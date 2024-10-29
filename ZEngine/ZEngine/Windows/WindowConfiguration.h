#pragma once
#include <Helpers/IntrusivePtr.h>
#include <string>
#include <vector>

namespace ZEngine::Windows::Layers
{
    class Layer;
}

namespace ZEngine::Windows
{
    struct WindowConfiguration
    {
        uint32_t                                                   Width{1500};
        uint32_t                                                   Height{800};
        bool                                                       EnableVsync{true};
        std::string                                                Title;
        std::vector<Helpers::Ref<ZEngine::Windows::Layers::Layer>> RenderingLayerCollection;
        std::vector<Helpers::Ref<ZEngine::Windows::Layers::Layer>> OverlayLayerCollection;
    };

} // namespace ZEngine::Windows
