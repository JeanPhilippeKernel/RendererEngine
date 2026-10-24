#include <ZEngine/ECS/AuthoredComponentValues.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cmath>

namespace ZEngine::ECS
{
    namespace
    {
        bool Finite(const Core::Maths::Vec3f& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    } // namespace

    LocalTransformValue CaptureLocalTransform(const Components::TransformComponent& component)
    {
        return {component.Position, component.Rotation, component.Scale};
    }

    bool ValidLocalTransform(const LocalTransformValue& value)
    {
        return Finite(value.Position) && Finite(value.Rotation) && Finite(value.Scale);
    }

    bool SameLocalTransform(const LocalTransformValue& a, const LocalTransformValue& b)
    {
        // Vec3's operator== is approximate. History must retain even sub-epsilon authored edits.
        const auto same = [](const Core::Maths::Vec3f& x, const Core::Maths::Vec3f& y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
        return same(a.Position, b.Position) && same(a.Rotation, b.Rotation) && same(a.Scale, b.Scale);
    }

    void ApplyLocalTransform(Components::TransformComponent& component, const LocalTransformValue& value)
    {
        component.Position         = value.Position;
        component.Rotation         = value.Rotation;
        component.Scale            = value.Scale;
        // The editor must not interpolate a preview against an earlier simulation position.
        component.PreviousPosition = value.Position;
    }

    AuthoredLightValue CaptureAuthoredLight(const Components::LightComponent& component)
    {
        return {
            component.LightType, component.Intensity, component.Range, component.SpotAngle, {component.Color[0], component.Color[1], component.Color[2]}
        };
    }

    bool ValidAuthoredLight(const AuthoredLightValue& value)
    {
        using Type = Components::LightComponent::Type;
        return (value.Type == Type::Directional || value.Type == Type::Point || value.Type == Type::Spot) && std::isfinite(value.Intensity) && value.Intensity >= 0.f && std::isfinite(value.Range) && (value.Type == Type::Directional ? value.Range >= 0.f : value.Range > 0.f) && std::isfinite(value.SpotAngle) && value.SpotAngle > 0.f && value.SpotAngle < 180.f && Finite(value.Color) && value.Color.x >= 0.f && value.Color.y >= 0.f && value.Color.z >= 0.f;
    }

    void ApplyAuthoredLight(Components::LightComponent& component, const AuthoredLightValue& value)
    {
        component.LightType = value.Type;
        component.Intensity = value.Intensity;
        component.Range     = value.Range;
        component.SpotAngle = value.SpotAngle;
        component.Color[0]  = value.Color.x;
        component.Color[1]  = value.Color.y;
        component.Color[2]  = value.Color.z;
    }

    AuthoredCameraValue CaptureAuthoredCamera(const Components::CameraComponent& component)
    {
        return {component.FovY, component.Near, component.Far, component.AspectRatio, component.IsMain};
    }

    bool ValidAuthoredCamera(const AuthoredCameraValue& value)
    {
        return std::isfinite(value.FovY) && value.FovY > 0.f && value.FovY < 180.f && std::isfinite(value.Near) && value.Near > 0.f && std::isfinite(value.Far) && value.Far > value.Near && std::isfinite(value.AspectRatio) && value.AspectRatio > 0.f;
    }

    void ApplyAuthoredCamera(Components::CameraComponent& component, const AuthoredCameraValue& value)
    {
        component.FovY        = value.FovY;
        component.Near        = value.Near;
        component.Far         = value.Far;
        component.AspectRatio = value.AspectRatio;
        component.IsMain      = value.IsMain;
    }

    AuthoredRigidBodyValue CaptureAuthoredRigidBody(const Components::RigidBodyComponent& component)
    {
        return {component.MotionKind, component.Mass, component.Friction, component.Restitution};
    }

    bool ValidAuthoredRigidBody(const AuthoredRigidBodyValue& value)
    {
        using Motion = Components::RigidBodyComponent::MotionType;
        return (value.MotionKind == Motion::Static || value.MotionKind == Motion::Kinematic || value.MotionKind == Motion::Dynamic) && std::isfinite(value.Mass) && value.Mass > 0.f && std::isfinite(value.Friction) && value.Friction >= 0.f && std::isfinite(value.Restitution) && value.Restitution >= 0.f && value.Restitution <= 1.f;
    }

    void ApplyAuthoredRigidBody(Components::RigidBodyComponent& component, const AuthoredRigidBodyValue& value)
    {
        component.MotionKind  = value.MotionKind;
        component.Mass        = value.Mass;
        component.Friction    = value.Friction;
        component.Restitution = value.Restitution;
        // BodyID belongs to the runtime physics owner, not an authored snapshot.
    }

    cstring ComponentSchemaKey(AuthoredFloatField field)
    {
        switch (field)
        {
            case AuthoredFloatField::LightRange:
            case AuthoredFloatField::LightSpotAngle:
            case AuthoredFloatField::LightIntensity:
                return "LightComponent";
            case AuthoredFloatField::CameraFar:
            case AuthoredFloatField::CameraFovY:
            case AuthoredFloatField::CameraNear:
            case AuthoredFloatField::CameraAspectRatio:
                return "CameraComponent";
            case AuthoredFloatField::RigidBodyMass:
            case AuthoredFloatField::RigidBodyFriction:
            case AuthoredFloatField::RigidBodyRestitution:
                return "RigidBodyComponent";
            default:
                break;
        }
        return nullptr;
    }

    cstring ComponentFieldKey(AuthoredFloatField field)
    {
        switch (field)
        {
            case AuthoredFloatField::LightRange:
                return "range";
            case AuthoredFloatField::LightSpotAngle:
                return "spot_angle";
            case AuthoredFloatField::CameraFar:
                return "far";
            case AuthoredFloatField::CameraFovY:
                return "fov_y";
            case AuthoredFloatField::LightIntensity:
                return "intensity";
            case AuthoredFloatField::CameraNear:
                return "near";
            case AuthoredFloatField::CameraAspectRatio:
                return "aspect_ratio";
            case AuthoredFloatField::RigidBodyMass:
                return "mass";
            case AuthoredFloatField::RigidBodyFriction:
                return "friction";
            case AuthoredFloatField::RigidBodyRestitution:
                return "restitution";
            default:
                break;
        }
        return nullptr;
    }

    bool FindAuthoredFloatField(cstring component, cstring field, AuthoredFloatField& out)
    {
        if (!component || !field)
        {
            return false;
        }
        for (uint8_t index = 0; index < static_cast<uint8_t>(AuthoredFloatField::Count); ++index)
        {
            const auto candidate = static_cast<AuthoredFloatField>(index);
            if (Helpers::secure_strcmp(component, ComponentSchemaKey(candidate)) == 0 && Helpers::secure_strcmp(field, ComponentFieldKey(candidate)) == 0)
            {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    uint64_t AuthoredFloatFieldLifetime(const Scene& scene, EntityID entity, AuthoredFloatField field)
    {
        switch (field)
        {
            case AuthoredFloatField::LightRange:
            case AuthoredFloatField::LightSpotAngle:
            case AuthoredFloatField::LightIntensity:
                return scene.ComponentLifetime<Components::LightComponent>(entity);
            case AuthoredFloatField::CameraFar:
            case AuthoredFloatField::CameraFovY:
            case AuthoredFloatField::CameraNear:
            case AuthoredFloatField::CameraAspectRatio:
                return scene.ComponentLifetime<Components::CameraComponent>(entity);
            case AuthoredFloatField::RigidBodyMass:
            case AuthoredFloatField::RigidBodyFriction:
            case AuthoredFloatField::RigidBodyRestitution:
                return scene.ComponentLifetime<Components::RigidBodyComponent>(entity);
            default:
                return 0;
        }
    }

    bool ReadAuthoredFloatField(const Scene& scene, EntityID entity, AuthoredFloatField field, float& out)
    {
        if (!scene.IsAlive(entity))
        {
            return false;
        }
        switch (field)
        {
            case AuthoredFloatField::LightRange:
            case AuthoredFloatField::LightSpotAngle:
            case AuthoredFloatField::LightIntensity:
            {
                const auto* light = scene.GetComponent<Components::LightComponent>(entity);
                if (!light || (field == AuthoredFloatField::LightRange && light->LightType != Components::LightComponent::Type::Point && light->LightType != Components::LightComponent::Type::Spot) || (field == AuthoredFloatField::LightSpotAngle && light->LightType != Components::LightComponent::Type::Spot))
                {
                    return false;
                }
                out = field == AuthoredFloatField::LightRange ? light->Range : field == AuthoredFloatField::LightSpotAngle ? light->SpotAngle : light->Intensity;
                return true;
            }
            case AuthoredFloatField::CameraFar:
            case AuthoredFloatField::CameraFovY:
            case AuthoredFloatField::CameraNear:
            case AuthoredFloatField::CameraAspectRatio:
            {
                const auto* camera = scene.GetComponent<Components::CameraComponent>(entity);
                if (!camera)
                {
                    return false;
                }
                out = field == AuthoredFloatField::CameraFar ? camera->Far : field == AuthoredFloatField::CameraFovY ? camera->FovY : field == AuthoredFloatField::CameraNear ? camera->Near : camera->AspectRatio;
                return true;
            }
            case AuthoredFloatField::RigidBodyMass:
            case AuthoredFloatField::RigidBodyFriction:
            case AuthoredFloatField::RigidBodyRestitution:
            {
                const auto* body = scene.GetComponent<Components::RigidBodyComponent>(entity);
                if (!body)
                {
                    return false;
                }
                out = field == AuthoredFloatField::RigidBodyMass ? body->Mass : field == AuthoredFloatField::RigidBodyFriction ? body->Friction : body->Restitution;
                return true;
            }
            default:
                break;
        }
        return false;
    }

    bool ValidAuthoredFloatField(const Scene& scene, EntityID entity, AuthoredFloatField field, float value)
    {
        float current = 0.f;
        if (!std::isfinite(value) || !ReadAuthoredFloatField(scene, entity, field, current))
        {
            return false;
        }
        switch (field)
        {
            case AuthoredFloatField::LightRange:
            case AuthoredFloatField::LightSpotAngle:
            case AuthoredFloatField::LightIntensity:
            {
                auto candidate = CaptureAuthoredLight(*scene.GetComponent<Components::LightComponent>(entity));
                if (field == AuthoredFloatField::LightRange)
                {
                    candidate.Range = value;
                }
                else if (field == AuthoredFloatField::LightSpotAngle)
                {
                    candidate.SpotAngle = value;
                }
                else
                {
                    candidate.Intensity = value;
                }
                return ValidAuthoredLight(candidate);
            }
            case AuthoredFloatField::CameraFovY:
            case AuthoredFloatField::CameraFar:
            case AuthoredFloatField::CameraNear:
            case AuthoredFloatField::CameraAspectRatio:
            {
                auto candidate = CaptureAuthoredCamera(*scene.GetComponent<Components::CameraComponent>(entity));
                if (field == AuthoredFloatField::CameraFar)
                {
                    candidate.Far = value;
                }
                else if (field == AuthoredFloatField::CameraFovY)
                {
                    candidate.FovY = value;
                }
                else if (field == AuthoredFloatField::CameraNear)
                {
                    candidate.Near = value;
                }
                else
                {
                    candidate.AspectRatio = value;
                }
                return ValidAuthoredCamera(candidate);
            }
            case AuthoredFloatField::RigidBodyMass:
            case AuthoredFloatField::RigidBodyFriction:
            case AuthoredFloatField::RigidBodyRestitution:
            {
                auto candidate = CaptureAuthoredRigidBody(*scene.GetComponent<Components::RigidBodyComponent>(entity));
                if (field == AuthoredFloatField::RigidBodyMass)
                {
                    candidate.Mass = value;
                }
                else if (field == AuthoredFloatField::RigidBodyFriction)
                {
                    candidate.Friction = value;
                }
                else
                {
                    candidate.Restitution = value;
                }
                return ValidAuthoredRigidBody(candidate);
            }
            default:
                break;
        }
        return false;
    }

    void ApplyAuthoredFloatField(Scene& scene, EntityID entity, AuthoredFloatField field, float value)
    {
        switch (field)
        {
            case AuthoredFloatField::LightRange:
                scene.GetComponent<Components::LightComponent>(entity)->Range = value;
                break;
            case AuthoredFloatField::LightSpotAngle:
                scene.GetComponent<Components::LightComponent>(entity)->SpotAngle = value;
                break;
            case AuthoredFloatField::CameraFar:
                scene.GetComponent<Components::CameraComponent>(entity)->Far = value;
                break;
            case AuthoredFloatField::CameraFovY:
                scene.GetComponent<Components::CameraComponent>(entity)->FovY = value;
                break;
            case AuthoredFloatField::LightIntensity:
                scene.GetComponent<Components::LightComponent>(entity)->Intensity = value;
                break;
            case AuthoredFloatField::CameraNear:
                scene.GetComponent<Components::CameraComponent>(entity)->Near = value;
                break;
            case AuthoredFloatField::CameraAspectRatio:
                scene.GetComponent<Components::CameraComponent>(entity)->AspectRatio = value;
                break;
            case AuthoredFloatField::RigidBodyMass:
                scene.GetComponent<Components::RigidBodyComponent>(entity)->Mass = value;
                break;
            case AuthoredFloatField::RigidBodyFriction:
                scene.GetComponent<Components::RigidBodyComponent>(entity)->Friction = value;
                break;
            case AuthoredFloatField::RigidBodyRestitution:
                scene.GetComponent<Components::RigidBodyComponent>(entity)->Restitution = value;
                break;
            default:
                break;
        }
    }
} // namespace ZEngine::ECS
