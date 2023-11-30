#pragma once
#include <ZEngineDef.h>
#include <Rendering/Specifications/FormatSpecification.h>

namespace ZEngine::Rendering::Specifications
{
    struct TextureSpecification
    {
        bool        IsUsageSampled    = true;
        bool        IsUsageTransfert  = true;
        bool        PerformTransition = true;
        uint32_t    Width             = 0;
        uint32_t    Height            = 0;
        uint32_t    BytePerPixel      = 4;
        ImageFormat Format            = ImageFormat::UNDEFINED;
        const void* Data              = nullptr;
    };
}