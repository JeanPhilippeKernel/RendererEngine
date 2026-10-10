#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS::Components
{
    using NC                               = NameComponent;

    static const FieldDescriptor kFields[] = {
        {.Name = "Value", .Type = FieldType::String, .Offset = offsetof(NC, Value), .Size = sizeof(NC::Value), .StringCap = static_cast<uint32_t>(sizeof(NC::Value)), .Tooltip = "Display name shown in the Outliner"},
    };

    void RegisterNameComponentReflection()
    {
        ComponentReflectionRegistry::Get().Register({
            .TypeID     = ComponentTypeOf<NC>(),
            .TypeName   = "NameComponent",
            .Size       = sizeof(NC),
            .Align      = alignof(NC),
            .Fields     = kFields,
            .FieldCount = static_cast<uint32_t>(sizeof(kFields) / sizeof(kFields[0])),
            .Category   = "General",
            .Add        = [](Scene& scene, EntityID id) { scene.AddComponent<NC>(id, {}); },
        });
    }
} // namespace ZEngine::ECS::Components
