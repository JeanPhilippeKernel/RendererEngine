#include <ZEngine/Profiling/Profiling.h>
#include <ZEngine/Rendering/CookedSceneGpuPreparation.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Rendering
{
    CookedSceneGpuPreparation::~CookedSceneGpuPreparation()
    {
        const bool reset = Reset();
        ZENGINE_VALIDATE_ASSERT(reset, "Cooked scene GPU preparation destroyed before cancellation drained")
        (void) reset;
    }

    bool CookedSceneGpuPreparation::Initialize(const Managers::CookedCpuClosureCache::Lease& lease, std::span<const uuids::uuid> meshes, const Cook::CookAssetIdentity& environment, Core::Memory::IAllocationRoute& route, Core::Memory::TLSFSlab& metadata, RenderResourceManager& manager, Hardwares::VulkanDevice& device, RenderResourceManager::SceneResourcePreparationId scene, uint32_t max_textures)
    {
        if (m_state != State::Empty || !scene)
        {
            return false;
        }
        RenderResourceManager::SceneEnvironmentPreparation sky = {};
        if (!manager.GetSceneEnvironmentPreparation(scene, &sky) || (sky.Input.Sky.IsHDRI() ? environment.Kind != Cook::CookAssetKind::EnvironmentMap || environment.UUID != sky.Input.Sky.EnvironmentMap : environment.IsValid()))
        {
            return false;
        }
        Managers::CookedCpuClosureCache::Lease textures = {}, mesh = {}, hdr = {};
        if (!lease.Retain(&textures) || !lease.Retain(&mesh) || (sky.Input.Sky.IsHDRI() && !lease.Retain(&hdr)))
        {
            return false;
        }
        if (!m_textures.Initialize(std::move(textures), route, manager, device, scene, max_textures))
        {
            return false;
        }
        if (!m_meshes.Initialize(std::move(mesh), meshes, route, manager, metadata, scene))
        {
            m_textures.Reset();
            return false;
        }
        if (sky.Input.Sky.IsHDRI() && !m_environment.Initialize(std::move(hdr), environment, manager, device, scene))
        {
            (void) m_meshes.Reset();
            m_textures.Reset();
            return false;
        }
        m_manager         = &manager;
        m_scene           = scene;
        m_has_environment = sky.Input.Sky.IsHDRI();
        m_state           = State::Pending;
        return true;
    }

    void CookedSceneGpuPreparation::Stop(State outcome) noexcept
    {
        m_after_drain = outcome;
        (void) m_manager->ReleaseSceneResourcePreparation(m_scene);
        m_textures.Reset();
        m_environment.Reset();
        m_meshes.Cancel();
        m_state = m_meshes.Status() == Meshes::CookedSceneMeshBatch::State::Draining ? State::Draining : outcome;
    }

    void CookedSceneGpuPreparation::Cancel() noexcept
    {
        if (m_state == State::Pending)
            Stop(State::Cancelled);
    }

    CookedSceneGpuPreparation::State CookedSceneGpuPreparation::Step(uint8_t frame_index, uint8_t thread_index)
    {
        if (m_state == State::Draining)
        {
            const auto mesh = m_meshes.Step(frame_index);
            if (mesh != Meshes::CookedSceneMeshBatch::State::Draining)
                m_state = m_after_drain;
            return m_state;
        }
        if (m_state != State::Pending)
            return m_state;
        RenderResourceManager::SceneResourcePreparationResult scene = {};
        if (!m_manager->PollSceneResourcePreparation(m_scene, &scene) || !scene.Cooked || scene.State != RenderResourceManager::SceneResourcePreparationState::Preparing)
        {
            Stop(State::Failed);
            return m_state;
        }
        using TextureState     = Textures::CookedSceneTextureBatch::State;
        using MeshState        = Meshes::CookedSceneMeshBatch::State;
        using EnvironmentState = Textures::CookedSceneEnvironmentBatch::State;
        for (uint32_t visited = 0; visited < 4; ++visited)
        {
            const uint32_t turn = m_turn++ % 4;
            if (turn == 0 && m_textures.Status() != TextureState::Ready)
            {
                ZENGINE_PROFILE_SCOPE("SceneLoad.Render.CookedTextures");
                if (m_textures.Step(frame_index, thread_index) == TextureState::Failed)
                {
                    ZENGINE_CORE_ERROR("[CookedSceneGpuPreparation] Texture preparation failed")
                    Stop(State::Failed);
                }
                return m_state;
            }
            if (turn == 1 && m_meshes.Status() != MeshState::Ready)
            {
                ZENGINE_PROFILE_SCOPE("SceneLoad.Render.CookedMeshes");
                const auto mesh = m_meshes.Step(frame_index);
                if (mesh == MeshState::Failed || mesh == MeshState::Draining || mesh == MeshState::Cancelled)
                    Stop(State::Failed);
                return m_state;
            }
            if (turn == 2 && m_has_environment && m_environment.Status() != EnvironmentState::Ready)
            {
                ZENGINE_PROFILE_SCOPE("SceneLoad.Render.CookedEnvironment");
                if (m_environment.Step(frame_index, thread_index) == EnvironmentState::Failed)
                    Stop(State::Failed);
                return m_state;
            }
            if (turn == 3 && m_textures.MaterialStatus() != TextureState::Ready && m_textures.Status() == TextureState::Ready)
            {
                ZENGINE_PROFILE_SCOPE("SceneLoad.Render.CookedMaterials");
                if (m_textures.StepMaterials() == TextureState::Failed)
                {
                    ZENGINE_CORE_ERROR("[CookedSceneGpuPreparation] Material preparation failed")
                    Stop(State::Failed);
                }
                return m_state;
            }
        }
        if (m_textures.Status() == TextureState::Ready && m_textures.MaterialStatus() == TextureState::Ready && m_meshes.Status() == MeshState::Ready && (!m_has_environment || m_environment.Status() == EnvironmentState::Ready) && m_manager->FinishCookedSceneResources(m_scene))
        {
            (void) m_meshes.Reset();
            m_textures.Reset();
            m_environment.Reset();
            m_state = State::Submitted;
        }
        return m_state;
    }

    bool CookedSceneGpuPreparation::AbandonAfterDeviceIdle() noexcept
    {
        if (m_state == State::Pending)
            Cancel();
        if (m_state == State::Draining)
        {
            if (!m_meshes.AbandonAfterDeviceIdle())
                return false;
            m_state = m_after_drain;
        }
        return m_state != State::Pending && m_state != State::Draining;
    }

    bool CookedSceneGpuPreparation::Reset() noexcept
    {
        if (m_state == State::Pending || m_state == State::Draining || !m_meshes.Reset())
            return false;
        m_textures.Reset();
        m_environment.Reset();
        m_manager         = nullptr;
        m_scene           = {};
        m_has_environment = false;
        m_turn            = 0;
        m_state           = State::Empty;
        return true;
    }
} // namespace ZEngine::Rendering
