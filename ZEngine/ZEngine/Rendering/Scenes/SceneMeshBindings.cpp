#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Scenes/SceneMeshBindings.h>
#include <algorithm>

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

namespace ZEngine::Rendering::Scenes
{
    namespace
    {
        bool Reject(SceneDiagnostics* diagnostics, cstring message)
        {
            SceneDiagnosticError(diagnostics, message);
            return false;
        }
        struct MeshRequirement
        {
            uuids::uuid UUID = {};
            uint32_t    Uses = 1;
        };
    } // namespace

    bool BuildSceneMeshBindings(SceneInstance& instance, RenderScene& render, const SceneMeshResolver& assets, const SceneMeshBindingLimits& limits, Core::Containers::Array<uuids::uuid>& required_meshes, uint32_t& draw_count, SceneDiagnostics* diagnostics)
    {
        if (!instance.Arena() || !render.Instances.m_allocator || !render.Instances.empty() || !required_meshes.empty() || draw_count || limits.MaxInstances > SceneData::MAX_DRAW_COMMANDS || limits.MaxDrawCommands > SceneData::MAX_DRAW_COMMANDS)
            return Reject(diagnostics, "Invalid unpublished scene mesh binding inputs.");
        auto&                                             world        = instance.World();
        // O(E + M log M), with one asset lookup per distinct mesh, not per placed
        // instance. Scratch is charged to the bounded candidate scene arena.
        ZEngine::Core::Containers::Array<MeshRequirement> requirements = {};
        requirements.m_allocator                                       = instance.Arena();
        bool valid                                                     = true;
        world.ForEach<MeshComponent>([&](EntityID entity, const MeshComponent& mesh) {
            if (!world.HasComponent<TransformComponent>(entity) || mesh.MeshUUID.is_nil() || requirements.size() >= limits.MaxInstances)
            {
                valid = false;
                return;
            }
            if (!requirements.try_push({mesh.MeshUUID}))
                valid = false;
        });
        if (!valid)
        {
            return Reject(diagnostics, "A mesh has no transform/asset identity, or the complete scene exceeds the instance budget.");
        }
        const auto instance_count = static_cast<uint32_t>(requirements.size());
        if (instance_count && !assets.ResolveMesh)
        {
            return Reject(diagnostics, "Scene meshes need an explicit asset resolver; no render bindings were published.");
        }
        if (!requirements.empty())
            std::sort(requirements.begin(), requirements.end(), [](const auto& a, const auto& b) { return a.UUID < b.UUID; });
        size_t unique_count = 0;
        for (size_t i = 0; i < requirements.size(); ++i)
        {
            if (unique_count && requirements[unique_count - 1].UUID == requirements[i].UUID)
            {
                ++requirements[unique_count - 1].Uses;
            }
            else
            {
                requirements[unique_count++] = requirements[i];
            }
        }
        requirements.m_size         = unique_count;
        required_meshes.m_allocator = instance.Arena();
        if (!required_meshes.try_reserve(unique_count))
        {
            return Reject(diagnostics, "Cannot retain the complete scene resource identity set.");
        }
        for (const auto& requirement : requirements)
        {
            required_meshes.push(requirement.UUID);
            uint32_t draws = 0;
            if (!assets.ResolveMesh(assets.Context, requirement.UUID, &draws, diagnostics) || draws == 0)
            {
                return Reject(diagnostics, "A referenced mesh is missing or has no complete drawable binding; the active scene was preserved.");
            }
            const uint64_t additional = uint64_t(draws) * requirement.Uses;
            if (additional > limits.MaxDrawCommands - draw_count)
            {
                return Reject(diagnostics, "The complete scene exceeds the GPU submesh draw budget; no instances were truncated.");
            }
            draw_count += static_cast<uint32_t>(additional);
        }
        if (!render.Instances.try_reserve(instance_count))
        {
            return Reject(diagnostics, "Cannot retain the complete scene instance list in its bounded arena.");
        }
        world.ForEach<MeshComponent>([&](EntityID entity, MeshComponent& mesh) {
            ZEngine::Rendering::Scenes::MeshInstance instance = {};
            instance.Id                                       = render.NextInstanceId++;
            instance.MeshUUID                                 = mesh.MeshUUID;
            instance.Entity                                   = entity;
            instance.Transform                                = world.GetComponent<TransformComponent>(entity)->WorldTransform;
            if (const auto* name = world.GetComponent<NameComponent>(entity))
            {
                ZEngine::Helpers::secure_strncpy(instance.Name, sizeof(instance.Name), name->Value, sizeof(instance.Name) - 1);
            }
            mesh.RenderInstanceId = instance.Id;
            render.Instances.push(instance);
        });

        instance.RecordMemoryUsage();
        return true;
    }
} // namespace ZEngine::Rendering::Scenes
