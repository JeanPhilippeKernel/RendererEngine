#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>
#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <ZEngine/Profiling/Profiling.h>
#include <ZEngine/Rendering/Scenes/SceneResourcePreparation.h>
#include <algorithm>

namespace ZEngine::Rendering::Scenes
{
    SceneResourcePreparation::~SceneResourcePreparation()
    {
        if (m_resources && m_preparation)
            m_resources->ReleaseSceneResourcePreparation(m_preparation);
    }

    bool SceneResourcePreparation::Prepare(ECS::SceneInstance& instance, RenderScene& render, std::span<const uuids::uuid> meshes, RenderResourceManager& resources, ECS::SceneRequestId request, uint64_t generation, const SceneInitialView& view, const Managers::CookedCpuAssetSet* cooked, bool* terminal_failure)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Main.ResourceAdmission");
        if (terminal_failure)
            *terminal_failure = false;
        if (cooked && (!cooked->IsValid() || !instance.MatchesCookedClosure(cooked->ManifestDigest(), cooked->SceneUUID())))
        {
            if (terminal_failure)
                *terminal_failure = true;
            return false;
        }
        if (m_preparation)
        {
            if (m_resources != &resources || m_cooked != (cooked != nullptr))
            {
                if (terminal_failure)
                    *terminal_failure = true;
                return false;
            }
            if (m_instances_copied < render.Instances.size())
            {
                const auto count = std::min(size_t(64), render.Instances.size() - m_instances_copied);
                if (resources.AppendSceneResourceInstances(m_preparation, {render.Instances.data() + m_instances_copied, count}))
                    m_instances_copied += static_cast<uint32_t>(count);
            }
            return true;
        }
        auto& world = instance.World();
        ECS::Systems::SyncHierarchy(world);
        for (auto& item : render.Instances)
        {
            const auto* transform = world.GetComponent<ECS::Components::TransformComponent>(item.Entity);
            if (!transform)
            {
                if (terminal_failure)
                    *terminal_failure = true;
                return false;
            }
            item.Transform = transform->WorldTransform;
        }
        render.MarkInstancesDirty();
        ECS::Systems::SyncECSToLights(world, render);
        const RenderResourceManager::SceneEnvironmentInput  environment = {render.Sky, render.CelestialLight, render.SkyRevision.value.load(std::memory_order_acquire)};
        const RenderResourceManager::SceneInitialFrameInput frame       = {
            {render.Instances.data(), render.Instances.size()},
            render.PendingLights, render.Grid, view
        };
        if (!resources.PrepareSceneResources(request, generation, meshes, &m_preparation, RenderResourceManager::ScenePreparationContent::MeshMaterials, &environment, &frame, true, cooked))
            return false;
        m_instances_copied = 0;
        m_resources        = &resources;
        m_cooked           = cooked != nullptr;
        return true;
    }

    bool SceneResourcePreparation::Poll(RenderResourceManager::SceneResourcePreparationResult* output) const
    {
        return m_resources && m_resources->PollSceneResourcePreparation(m_preparation, output);
    }

    RenderResourceManager::SceneResourcePreparationId SceneResourcePreparation::InitialResources()
    {
        RenderResourceManager::SceneResourcePreparationResult result = {};
        if (!m_cooked && Poll(&result) && result.FrameRecorded)
        {
            m_resources->ReleaseSceneResourcePreparation(m_preparation);
            m_preparation = {};
            m_resources   = nullptr;
        }
        return m_preparation;
    }
    bool SceneResourcePreparation::InstancesCollected(size_t count) const noexcept
    {
        return bool(m_preparation) && m_instances_copied == count;
    }

    bool SceneResourcePreparation::HasPreparation() const noexcept
    {
        return bool(m_preparation);
    }

} // namespace ZEngine::Rendering::Scenes
