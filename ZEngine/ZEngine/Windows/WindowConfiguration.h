#pragma once
#include <Core/Container/Array.h>
#include <Core/Container/Strings.h>
#include <Helpers/IntrusivePtr.h>

namespace ZEngine::Windows::Layers
{
    class Layer;
}

namespace ZEngine::Windows
{
    struct WindowConfiguration
    {
        uint32_t                                       Width       = 1500;
        uint32_t                                       Height      = 800;
        bool                                           EnableVsync = true;
        Core::Container::String                        Title;

        Core::Container::Array<ZRawPtr(Layers::Layer)> RenderingLayerCollection;
        Core::Container::Array<ZRawPtr(Layers::Layer)> OverlayLayerCollection;
    };

} // namespace ZEngine::Windows
