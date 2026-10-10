#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS::Components
{
    using UC                               = UUIDComponent;

    static const FieldDescriptor kFields[] = {
        {.Name = "Value", .Type = FieldType::AssetUUID, .Offset = offsetof(UC, Value), .Size = sizeof(uuids::uuid), .ReadOnly = true, .Tooltip = "Stable cross-session identity"},
    };

    void RegisterUUIDComponentReflection()
    {
        ComponentReflectionRegistry::Get().Register({
            .TypeID     = ComponentTypeOf<UC>(),
            .TypeName   = "UUIDComponent",
            .Size       = sizeof(UC),
            .Align      = alignof(UC),
            .Fields     = kFields,
            .FieldCount = static_cast<uint32_t>(sizeof(kFields) / sizeof(kFields[0])),
            .Category   = "General",
            .Add        = [](Scene& scene, EntityID id) { scene.AddComponent<UC>(id, {}); },
        });
    }
} // namespace ZEngine::ECS::Components
