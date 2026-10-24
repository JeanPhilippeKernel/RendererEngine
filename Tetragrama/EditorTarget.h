#pragma once
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/ECS/Scene.h>
#include <uuid.h>

namespace Tetragrama
{
    enum class SceneTarget : uint8_t
    {
        None,
        Grid,
    };

    /// @brief Authored identity plus a runtime hint, never an actor handle or ECS pointer.
    struct EntityTarget
    {
        uuids::uuid            UUID         = {};
        ZEngine::ECS::EntityID CachedEntity = {};
        uint64_t               SceneEpoch   = 0;
    };

    /// @brief Main-thread, revision-cached UUID lookup shared by editor consumers.
    /// Duplicate identities are unresolvable, including through otherwise-live cached IDs.
    class EntityTargetResolver
    {
    public:
        void                                 Initialize(ZEngine::Core::Memory::ArenaAllocator* arena);
        void                                 Bind(ZEngine::ECS::Scene* scene, uint64_t scene_epoch);
        void                                 Invalidate();

        [[nodiscard]] EntityTarget           TargetFor(ZEngine::ECS::EntityID entity);
        [[nodiscard]] ZEngine::ECS::EntityID Resolve(const EntityTarget& target);
        [[nodiscard]] ZEngine::ECS::EntityID Resolve(const uuids::uuid& identity);
        [[nodiscard]] uint64_t               SceneEpoch() const
        {
            return m_scene_epoch;
        }
        [[nodiscard]] uint64_t IndexRevision() const
        {
            return m_index_revision;
        }

    private:
        bool                                                                             Refresh();

        ZEngine::Core::Containers::UnorderedHashMap<uuids::uuid, ZEngine::ECS::EntityID> m_index          = {};
        ZEngine::ECS::Scene*                                                             m_scene          = nullptr;
        uint64_t                                                                         m_scene_epoch    = 0;
        uint64_t                                                                         m_index_revision = 0;
        bool                                                                             m_initialized    = false;
        bool                                                                             m_index_valid    = false;
    };
} // namespace Tetragrama
