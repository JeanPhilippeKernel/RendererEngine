#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/Strings.h>
#include <Helpers/IntrusivePtr.h>

namespace ZEngine::Windows::Layers
{
    class Layer;
}

namespace ZEngine::Windows
{
    struct WindowConfiguration
    {
        uint32_t                                        Width       = 1500;
        uint32_t                                        Height      = 800;
        bool                                            EnableVsync = true;
        Core::Containers::String                        Title;

        Core::Containers::Array<ZRawPtr(Layers::Layer)> RenderingLayerCollection;
        Core::Containers::Array<ZRawPtr(Layers::Layer)> OverlayLayerCollection;
    };

} // namespace ZEngine::Windows
