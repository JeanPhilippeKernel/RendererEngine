#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ComponentTypeID.h>
#include <ZEngine/ECS/EntityID.h>
#include <yaml-cpp/yaml.h>
#include <cstdint>

namespace ZEngine::ECS
{
    class Scene;

    using SerializeYAMLFn     = void (*)(void* ctx, EntityID id, const Scene& scene, YAML::Node& out);
    using DeserializeYAMLFn   = void (*)(void* ctx, EntityID id, Scene& scene, const YAML::Node& in);
    using SerializeBinaryFn   = void (*)(void* ctx, EntityID id, const Scene& scene, Core::Containers::Array<uint8_t>& out);
    using DeserializeBinaryFn = void (*)(void* ctx, EntityID id, Scene& scene, const uint8_t* data, uint32_t size);

    struct ComponentSerializeFns
    {
        SerializeYAMLFn     SerializeYAML     = nullptr;
        DeserializeYAMLFn   DeserializeYAML   = nullptr;
        SerializeBinaryFn   SerializeBinary   = nullptr;
        DeserializeBinaryFn DeserializeBinary = nullptr;
        void*               Context           = nullptr; // first arg to every callback
    };

    using ForEachSerializerFn = void (*)(void* ctx, ComponentTypeID type_id, const ComponentSerializeFns& fns);

    class ComponentSerializerRegistry
    {
    public:
        static ComponentSerializerRegistry&        Get();

        void                                       Initialize(Core::Memory::ArenaAllocator* arena);

        void                                       Register(ComponentTypeID type_id, ComponentSerializeFns fns);

        [[nodiscard]] const ComponentSerializeFns* Lookup(ComponentTypeID type_id) const;

        // Iterates all registered type IDs in registration order.
        void                                       ForEach(ForEachSerializerFn fn, void* ctx) const;

        [[nodiscard]] uint32_t                     Count() const;

    private:
        Core::Containers::Array<ComponentTypeID>       m_ids;
        Core::Containers::Array<ComponentSerializeFns> m_fns;
        Core::Memory::ArenaAllocator*                  m_arena = nullptr;
    };
} // namespace ZEngine::ECS
