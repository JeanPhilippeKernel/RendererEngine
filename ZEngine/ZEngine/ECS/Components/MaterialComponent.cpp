#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>

namespace ZEngine::ECS::Components
{
    using MatC                             = MaterialComponent;

    static const FieldDescriptor kFields[] = {
        {.Name = "MaterialUUID", .Type = FieldType::AssetUUID, .Offset = offsetof(MatC, MaterialUUID), .Size = sizeof(uuids::uuid), .Tooltip = "Overrides the mesh's baked material"},
    };

    void RegisterMaterialComponentReflection()
    {
        ComponentReflectionRegistry::Get().Register({
            .TypeID     = ComponentTypeOf<MatC>(),
            .TypeName   = "MaterialComponent",
            .Size       = sizeof(MatC),
            .Align      = alignof(MatC),
            .Fields     = kFields,
            .FieldCount = static_cast<uint32_t>(sizeof(kFields) / sizeof(kFields[0])),
            .Category   = "Rendering",
        });
    }
} // namespace ZEngine::ECS::Components
