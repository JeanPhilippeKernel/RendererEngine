#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/EntityID.h>
#include <uuid.h>

namespace ZEngine::ECS
{
    struct SceneSnapshot
    {
        uuids::uuid                       SceneUUID = {};
        Core::Containers::String          Name      = {};

        Core::Containers::Array<EntityID> Entities  = {};

        static SceneSnapshot              Create(Core::Memory::ArenaAllocator* arena, cstring name, uint32_t entity_capacity = 64)
        {
            SceneSnapshot snapshot{};
            snapshot.Name.init(arena, name ? name : "");
            snapshot.Entities.init(arena, entity_capacity ? entity_capacity : 1);
            return snapshot;
        }
    };
} // namespace ZEngine::ECS
