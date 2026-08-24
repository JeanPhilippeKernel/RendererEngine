#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>

namespace ZEngine::ECS::Components
{
    using CC                               = CameraComponent;

    static const FieldDescriptor kFields[] = {
        {.Name = "FovY", .Type = FieldType::Float, .Offset = offsetof(CC, FovY), .Size = sizeof(float), .Tooltip = "Vertical field of view, degrees"},
        {.Name = "Near", .Type = FieldType::Float, .Offset = offsetof(CC, Near), .Size = sizeof(float)},
        {.Name = "Far", .Type = FieldType::Float, .Offset = offsetof(CC, Far), .Size = sizeof(float)},
        {.Name = "AspectRatio", .Type = FieldType::Float, .Offset = offsetof(CC, AspectRatio), .Size = sizeof(float)},
        {.Name = "IsMain", .Type = FieldType::Bool, .Offset = offsetof(CC, IsMain), .Size = sizeof(bool), .Tooltip = "Exactly one camera should be main"},
    };

    void RegisterCameraComponentReflection()
    {
        ComponentReflectionRegistry::Get().Register({
            .TypeID     = ComponentTypeOf<CC>(),
            .TypeName   = "CameraComponent",
            .Size       = sizeof(CC),
            .Align      = alignof(CC),
            .Fields     = kFields,
            .FieldCount = static_cast<uint32_t>(sizeof(kFields) / sizeof(kFields[0])),
            .Category   = "Camera",
        });
    }
} // namespace ZEngine::ECS::Components
