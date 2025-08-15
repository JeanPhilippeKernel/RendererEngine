#pragma once
#include <Core/Containers/Strings.h>

namespace ZEngine::Windows
{
    struct WindowConfiguration
    {
        uint32_t                 Width       = 1500;
        uint32_t                 Height      = 800;
        bool                     EnableVsync = true;
        Core::Containers::String Title;
    };
    ZDEFINE_PTR(WindowConfiguration);

} // namespace ZEngine::Windows
