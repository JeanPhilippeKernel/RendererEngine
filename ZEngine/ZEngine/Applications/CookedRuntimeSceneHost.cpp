#include <ZEngine/Applications/CookedRuntimeSceneHost.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/ECS/SceneRuntimeCamera.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Profiling/Profiling.h>
#include <limits>
#include <utility>

namespace ZEngine::Applications
{
    CookedRuntimeSceneHost::~CookedRuntimeSceneHost()
    {
        ZENGINE_VALIDATE_ASSERT(!m_active, "Runtime scene host must retire its scene after readers join")
    }

    bool CookedRuntimeSceneHost::Publish(EngineContext& engine, GameApplication& app, std::unique_ptr<Rendering::Scenes::CookedRuntimeScene>& candidate, ECS::SceneRequestId request, uint64_t generation, const uuids::uuid& camera, RenderFrameState frame, ECS::SceneDiagnostics* diagnostics)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Main.RuntimePublication");
        if (!candidate || !request || !generation || generation == std::numeric_limits<uint64_t>::max() || !engine.ScenePublication.Idle() || engine.ScenePublication.Generation() != generation || engine.CloseRequested.value.load(std::memory_order_acquire) || !app.RenderPipeline || !engine.RenderResourceManager || !frame.RenderTargetW || !frame.RenderTargetH)
            return false;
        if (m_active && (engine.Scene != &m_active->Instance().World() || engine.ActorManager != &m_active->Instance().Actors() || engine.WorldCommands != &m_active->Instance().Commands() || engine.WorldTick != &m_active->Instance().Tick() || app.CurrentScene != &m_active->Render()))
            return false;
        Rendering::RenderResourceManager::SceneResourcePreparationResult result = {};
        if (!candidate->Resources().Poll(&result) || result.Id.Request != request || result.ExpectedGeneration != generation || result.State != Rendering::RenderResourceManager::SceneResourcePreparationState::ResourcesRetained)
            return false;
        if (!ECS::CaptureSceneRuntimeCamera(candidate->Instance().World(), camera, frame.RenderTargetW, frame.RenderTargetH, &frame.Camera, diagnostics))
            return false;
        frame.SceneGeneration = generation + 1;
        frame.SceneResources  = candidate->Resources().InitialResources();
        frame.ShowGrid        = false;
        frame.Gizmo.Reset();
        frame.RenderOverlay                            = app.EnableRenderOverlay;
        frame.FramebufferW                             = app.CurrentWindow ? app.CurrentWindow->GetWidth() : 0;
        frame.FramebufferH                             = app.CurrentWindow ? app.CurrentWindow->GetHeight() : 0;
        AppRenderPipeline::PreparedFrameState prepared = {};
        if (!app.RenderPipeline->PrepareFrameState(frame, &candidate->Render(), &prepared))
            return false;
        ECS::ScenePublication::Transition                      transition = {};
        std::unique_ptr<Rendering::Scenes::CookedRuntimeScene> retired    = {};
        {
            ZENGINE_PROFILE_SCOPE("SceneLoad.Main.RuntimeGuardedCommit");
            if (!engine.ScenePublication.TryBegin(generation, &transition) || !engine.SceneLoads.BeginCommit(request, generation))
                return false;
            if (!m_active)
            {
                m_original_world    = engine.Scene;
                m_original_actors   = engine.ActorManager;
                m_original_commands = engine.WorldCommands;
                m_original_tick     = engine.WorldTick;
                m_original_render   = app.CurrentScene;
            }
            retired              = std::move(m_active);
            m_active             = std::move(candidate);
            engine.Scene         = &m_active->Instance().World();
            engine.ActorManager  = &m_active->Instance().Actors();
            engine.WorldCommands = &m_active->Instance().Commands();
            engine.WorldTick     = &m_active->Instance().Tick();
            app.CurrentScene     = &m_active->Render();
            transition.Publish();
            prepared.Publish();
            const bool committed = engine.SceneLoads.Commit(request, engine.ScenePublication.Generation());
            ZENGINE_VALIDATE_ASSERT(committed, "Runtime scene publication lost its admitted result")
            (void) committed;
        }
        return true;
    }

    bool CookedRuntimeSceneHost::ShutdownAfterReadersJoined(EngineContext& engine, GameApplication& app) noexcept
    {
        if (!m_active)
            return true;
        if (engine.Scene != &m_active->Instance().World() || engine.ActorManager != &m_active->Instance().Actors() || engine.WorldCommands != &m_active->Instance().Commands() || engine.WorldTick != &m_active->Instance().Tick() || app.CurrentScene != &m_active->Render())
            return false;
        engine.Scene         = m_original_world;
        engine.ActorManager  = m_original_actors;
        engine.WorldCommands = m_original_commands;
        engine.WorldTick     = m_original_tick;
        app.CurrentScene     = m_original_render;
        m_active.reset();
        m_original_world    = nullptr;
        m_original_actors   = nullptr;
        m_original_commands = nullptr;
        m_original_tick     = nullptr;
        m_original_render   = nullptr;
        return true;
    }
} // namespace ZEngine::Applications
