#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>

namespace ZEngine::ECS::Components
{
    using MC                               = MeshComponent;

    static const FieldDescriptor kFields[] = {
        {.Name = "MeshUUID", .Type = FieldType::AssetUUID, .Offset = offsetof(MC, MeshUUID), .Size = sizeof(uuids::uuid), .ReadOnly = true, .Tooltip = "Stable identity assigned at import time"},
        {.Name = "RenderInstanceId", .Type = FieldType::UInt32, .Offset = offsetof(MC, RenderInstanceId), .Size = sizeof(uint32_t), .Hidden = true},
    };

    void RegisterMeshComponentReflection()
    {
        ComponentReflectionRegistry::Get().Register({
            .TypeID     = ComponentTypeOf<MC>(),
            .TypeName   = "MeshComponent",
            .Size       = sizeof(MC),
            .Align      = alignof(MC),
            .Fields     = kFields,
            .FieldCount = static_cast<uint32_t>(sizeof(kFields) / sizeof(kFields[0])),
            .Category   = "Rendering",
        });
    }
} // namespace ZEngine::ECS::Components
