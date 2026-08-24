#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>

namespace ZEngine::ECS::Components
{
    using LC = LightComponent;

    static_assert(sizeof(LC::Color) == 3 * sizeof(float), "LightComponent::Color must be float[3]");

    static const EnumValue kLightTypes[] = {
        {"Directional", static_cast<int64_t>(LC::Type::Directional)},
        {      "Point",       static_cast<int64_t>(LC::Type::Point)},
        {       "Spot",        static_cast<int64_t>(LC::Type::Spot)},
    };

    static const FieldDescriptor kFields[] = {
        {.Name = "LightType", .Type = FieldType::Enum, .Offset = offsetof(LC, LightType), .Size = sizeof(LC::Type), .EnumValues = kLightTypes, .EnumCount = static_cast<uint32_t>(sizeof(kLightTypes) / sizeof(kLightTypes[0]))},
        {.Name = "Intensity", .Type = FieldType::Float, .Offset = offsetof(LC, Intensity), .Size = sizeof(float)},
        {.Name = "Range", .Type = FieldType::Float, .Offset = offsetof(LC, Range), .Size = sizeof(float), .Tooltip = "Point and spot only"},
        {.Name = "SpotAngle", .Type = FieldType::Float, .Offset = offsetof(LC, SpotAngle), .Size = sizeof(float), .Tooltip = "Spot only, degrees"},
        {.Name = "Color R", .Type = FieldType::Float, .Offset = offsetof(LC, Color) + 0 * sizeof(float), .Size = sizeof(float), .Min = 0.f, .Max = 1.f},
        {.Name = "Color G", .Type = FieldType::Float, .Offset = offsetof(LC, Color) + 1 * sizeof(float), .Size = sizeof(float), .Min = 0.f, .Max = 1.f},
        {.Name = "Color B", .Type = FieldType::Float, .Offset = offsetof(LC, Color) + 2 * sizeof(float), .Size = sizeof(float), .Min = 0.f, .Max = 1.f},
    };

    void RegisterLightComponentReflection()
    {
        ComponentReflectionRegistry::Get().Register({
            .TypeID     = ComponentTypeOf<LC>(),
            .TypeName   = "LightComponent",
            .Size       = sizeof(LC),
            .Align      = alignof(LC),
            .Fields     = kFields,
            .FieldCount = static_cast<uint32_t>(sizeof(kFields) / sizeof(kFields[0])),
            .Category   = "Lighting",
        });
    }
} // namespace ZEngine::ECS::Components
