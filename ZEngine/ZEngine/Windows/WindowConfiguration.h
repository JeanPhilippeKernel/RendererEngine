#pragma once
#include <ZEngine/Core/Containers/Strings.h>

namespace ZEngine::Windows
{
    struct WindowConfiguration
    {
        uint32_t                 Width                    = 1500;
        uint32_t                 Height                   = 800;
        bool                     EnableVsync              = true;
        bool                     Resizable                = true;
        bool                     Maximize                 = true;
        bool                     WaitForVisibleTestWindow = false;
        Core::Containers::String Title;
    };
    ZDEFINE_PTR(WindowConfiguration);

} // namespace ZEngine::Windows
