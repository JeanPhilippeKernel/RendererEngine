#pragma once
#ifdef ZENGINE_EDITOR
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ISceneSerializer.h>
#include <yaml-cpp/yaml.h>

namespace ZEngine::ECS
{
    class Scene;

    class YAMLSceneSerializer final : public ISceneSerializer
    {
    public:
        YAMLSceneSerializer() = default;
        YAMLSceneSerializer(Scene* scene, Core::Memory::ArenaAllocator* arena) : m_scene(scene), m_arena(arena) {}

        void                              Initialize(Scene* scene, Core::Memory::ArenaAllocator* arena);

        Core::VFS::VFSResult<void>        Serialize(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const SceneSnapshot& scene) override;

        Core::VFS::VFSResult<void>        Deserialize(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, SceneSnapshot& out_scene) override;

        static Core::VFS::VFSResult<void> ValidateAssetRefs(const YAML::Node& entity_node);

    private:
        Scene*                        m_scene = nullptr;
        Core::Memory::ArenaAllocator* m_arena = nullptr;
    };
} // namespace ZEngine::ECS
#endif // ZENGINE_EDITOR
