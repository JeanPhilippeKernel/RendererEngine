#include <ZEngine/ECS/Components/RigidBodyComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>

namespace ZEngine::ECS::Components
{
    using RBC                             = RigidBodyComponent;

    static const EnumValue kMotionTypes[] = {
        {   "Static", 0},
        {"Kinematic", 1},
        {  "Dynamic", 2},
    };

    static const FieldDescriptor kFields[] = {
        {.Name = "MotionKind", .Type = FieldType::Enum, .Offset = offsetof(RBC, MotionKind), .Size = sizeof(RBC::MotionType), .EnumValues = kMotionTypes, .EnumCount = static_cast<uint32_t>(sizeof(kMotionTypes) / sizeof(kMotionTypes[0]))},
        {.Name = "Mass", .Type = FieldType::Float, .Offset = offsetof(RBC, Mass), .Size = sizeof(float)},
        {.Name = "Friction", .Type = FieldType::Float, .Offset = offsetof(RBC, Friction), .Size = sizeof(float)},
        {.Name = "Restitution", .Type = FieldType::Float, .Offset = offsetof(RBC, Restitution), .Size = sizeof(float)},
        {.Name = "BodyID", .Type = FieldType::UInt32, .Offset = offsetof(RBC, BodyID), .Size = sizeof(uint32_t), .Hidden = true, .ReadOnly = true, .Tooltip = "Assigned by PhysicsWorld; UINT32_MAX = inactive"},
    };

    void RegisterRigidBodyComponentReflection()
    {
        ComponentReflectionRegistry::Get().Register({
            .TypeID     = ComponentTypeOf<RBC>(),
            .TypeName   = "RigidBodyComponent",
            .Size       = sizeof(RBC),
            .Align      = alignof(RBC),
            .Fields     = kFields,
            .FieldCount = static_cast<uint32_t>(sizeof(kFields) / sizeof(kFields[0])),
            .Category   = "Physics",
        });
    }
} // namespace ZEngine::ECS::Components
