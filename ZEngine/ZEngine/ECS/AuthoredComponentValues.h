#pragma once
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/RigidBodyComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS
{
    /// @brief Semantic transform value shared by authoring transactions and scene codecs.
    /// Derived/interpolation fields are deliberately absent; this is not a component byte image.
    struct LocalTransformValue
    {
        Core::Maths::Vec3f Position = {};
        Core::Maths::Vec3f Rotation = {};
        Core::Maths::Vec3f Scale    = {1.f, 1.f, 1.f};
    };

    [[nodiscard]] LocalTransformValue CaptureLocalTransform(const Components::TransformComponent& component);
    [[nodiscard]] bool                ValidLocalTransform(const LocalTransformValue& value);
    [[nodiscard]] bool                SameLocalTransform(const LocalTransformValue& a, const LocalTransformValue& b);
    /// @brief Apply a prevalidated value; the hierarchy remains the sole WorldTransform writer.
    void                              ApplyLocalTransform(Components::TransformComponent& component, const LocalTransformValue& value);

    struct AuthoredLightValue
    {
        Components::LightComponent::Type Type      = Components::LightComponent::Type::Directional;
        float                            Intensity = 1.f;
        float                            Range     = 10.f;
        float                            SpotAngle = 45.f;
        Core::Maths::Vec3f               Color     = {1.f, 1.f, 1.f};
    };

    struct AuthoredCameraValue
    {
        float FovY        = 60.f;
        float Near        = 0.1f;
        float Far         = 1000.f;
        float AspectRatio = 16.f / 9.f;
        bool  IsMain      = false;
    };

    struct AuthoredRigidBodyValue
    {
        Components::RigidBodyComponent::MotionType MotionKind  = Components::RigidBodyComponent::MotionType::Dynamic;
        float                                      Mass        = 1.f;
        float                                      Friction    = 0.5f;
        float                                      Restitution = 0.f;
    };

    [[nodiscard]] AuthoredLightValue     CaptureAuthoredLight(const Components::LightComponent& component);
    [[nodiscard]] bool                   ValidAuthoredLight(const AuthoredLightValue& value);
    void                                 ApplyAuthoredLight(Components::LightComponent& component, const AuthoredLightValue& value);
    [[nodiscard]] AuthoredCameraValue    CaptureAuthoredCamera(const Components::CameraComponent& component);
    [[nodiscard]] bool                   ValidAuthoredCamera(const AuthoredCameraValue& value);
    void                                 ApplyAuthoredCamera(Components::CameraComponent& component, const AuthoredCameraValue& value);
    [[nodiscard]] AuthoredRigidBodyValue CaptureAuthoredRigidBody(const Components::RigidBodyComponent& component);
    [[nodiscard]] bool                   ValidAuthoredRigidBody(const AuthoredRigidBodyValue& value);
    void                                 ApplyAuthoredRigidBody(Components::RigidBodyComponent& component, const AuthoredRigidBodyValue& value);

    /// @brief Typed field identities with stable schema/field keys, never raw offsets or type IDs.
    enum class AuthoredFloatField : uint8_t
    {
        LightRange,
        LightSpotAngle,
        CameraFar,
        CameraFovY,
        LightIntensity,
        CameraNear,
        CameraAspectRatio,
        RigidBodyMass,
        RigidBodyFriction,
        RigidBodyRestitution,
        Count,
    };

    [[nodiscard]] cstring  ComponentSchemaKey(AuthoredFloatField field);
    [[nodiscard]] cstring  ComponentFieldKey(AuthoredFloatField field);
    [[nodiscard]] bool     FindAuthoredFloatField(cstring component, cstring field, AuthoredFloatField& out);
    [[nodiscard]] bool     ReadAuthoredFloatField(const Scene& scene, EntityID entity, AuthoredFloatField field, float& out);
    [[nodiscard]] uint64_t AuthoredFloatFieldLifetime(const Scene& scene, EntityID entity, AuthoredFloatField field);
    [[nodiscard]] bool     ValidAuthoredFloatField(const Scene& scene, EntityID entity, AuthoredFloatField field, float value);
    /// @brief Apply a prevalidated field, preserving every unrelated authored field.
    void                   ApplyAuthoredFloatField(Scene& scene, EntityID entity, AuthoredFloatField field, float value);
} // namespace ZEngine::ECS
