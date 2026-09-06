#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/ECS/EntityID.h>
#include <uuid.h>

namespace ZEngine::ECS
{
    struct SceneSnapshot
    {
        uuids::uuid                       SceneUUID = {};
        Core::Containers::String          Name      = {};
        Core::Containers::Array<EntityID> Entities  = {};
    };
} // namespace ZEngine::ECS
