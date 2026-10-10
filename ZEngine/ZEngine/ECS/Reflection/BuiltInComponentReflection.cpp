#include <ZEngine/ECS/Reflection/BuiltInComponentReflection.h>

namespace ZEngine::ECS::Components
{
    // Defined in each component's .cpp
    void RegisterTransformComponentReflection();
    void RegisterMeshComponentReflection();
    void RegisterCameraComponentReflection();
    void RegisterLightComponentReflection();
    void RegisterMaterialComponentReflection();
    void RegisterNameComponentReflection();
    void RegisterRigidBodyComponentReflection();
    void RegisterUUIDComponentReflection();

    void RegisterBuiltInComponentReflection()
    {
        RegisterTransformComponentReflection();
        RegisterMeshComponentReflection();
        RegisterCameraComponentReflection();
        RegisterLightComponentReflection();
        RegisterMaterialComponentReflection();
        RegisterNameComponentReflection();
        RegisterRigidBodyComponentReflection();
        RegisterUUIDComponentReflection();
    }
} // namespace ZEngine::ECS::Components
