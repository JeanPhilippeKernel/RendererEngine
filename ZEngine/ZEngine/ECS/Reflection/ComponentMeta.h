#pragma once
#include <ZEngine/ECS/ComponentTypeID.h>
#include <ZEngine/ECS/Reflection/FieldType.h>
#include <cstdint>

namespace ZEngine::ECS
{

    struct EnumValue
    {
        const char* Name  = nullptr;
        int64_t     Value = 0;
    };

    struct FieldDescriptor
    {
        const char*            Name       = nullptr;
        FieldType              Type       = FieldType::Float;
        uint32_t               Offset     = 0;
        uint32_t               Size       = 0;
        float                  Min        = -1e9f;
        float                  Max        = 1e9f;
        uint32_t               StringCap  = 0;
        const EnumValue*       EnumValues = nullptr;
        uint32_t               EnumCount  = 0;
        const FieldDescriptor* SubFields  = nullptr;
        uint32_t               SubCount   = 0;
        bool                   Hidden     = false;
        bool                   ReadOnly   = false;
        const char*            Tooltip    = nullptr;
    };

    struct ComponentMeta
    {
        ComponentTypeID        TypeID     = 0;
        const char*            TypeName   = nullptr;
        uint32_t               Size       = 0;
        uint32_t               Align      = 0;
        const FieldDescriptor* Fields     = nullptr;
        uint32_t               FieldCount = 0;
        const char*            Category   = "General"; // compare with strcmp, not ==
        const char*            Tooltip    = nullptr;
    };

} // namespace ZEngine::ECS
