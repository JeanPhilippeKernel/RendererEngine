#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>

namespace ZEngine::ECS::Components
{
    using TC = TransformComponent;
    using Core::Maths::Vec3f;

    static const FieldDescriptor kFields[] = {
        {.Name = "Position", .Type = FieldType::Vec3f, .Offset = offsetof(TC, Position), .Size = sizeof(Vec3f), .Tooltip = "World-space position"},
        {.Name = "Rotation", .Type = FieldType::Vec3f, .Offset = offsetof(TC, Rotation), .Size = sizeof(Vec3f), .Tooltip = "Euler angles (radians)"},
        {.Name = "Scale", .Type = FieldType::Vec3f, .Offset = offsetof(TC, Scale), .Size = sizeof(Vec3f), .Min = 0.001f, .Max = 1000.f},
        {.Name = "PreviousPosition", .Type = FieldType::Vec3f, .Offset = offsetof(TC, PreviousPosition), .Size = sizeof(Vec3f), .Hidden = true},
    };

    void RegisterTransformComponentReflection()
    {
        ComponentReflectionRegistry::Get().Register({
            .TypeID     = ComponentTypeOf<TC>(),
            .TypeName   = "TransformComponent",
            .Size       = sizeof(TC),
            .Align      = alignof(TC),
            .Fields     = kFields,
            .FieldCount = static_cast<uint32_t>(sizeof(kFields) / sizeof(kFields[0])),
            .Category   = "Transform",
        });
    }
} // namespace ZEngine::ECS::Components
