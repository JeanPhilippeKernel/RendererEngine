#pragma once
#include <ZEngine/Rendering/Renderers/Graphics/AssetPreviewPass.h>

namespace ZEngine::Rendering::Renderers
{
    struct AssetPreviewPassTestAccess
    {
        static void Arm(AssetPreviewPass& pass, Textures::TextureHandle texture, Hardwares::VulkanDevice* device = nullptr)
        {
            pass.Initialize(device);
            pass.m_tasks[0] = {.Owner = &pass, .Texture = texture, .CurrentState = AssetPreviewPass::State::Recording};
            pass.m_current  = &pass.m_tasks[0];
        }
        static void Cancel(AssetPreviewPass& pass)
        {
            AssetPreviewPass::OnCancelled(&pass.m_tasks[0]);
        }
        static bool Abandoned(const AssetPreviewPass& pass)
        {
            return pass.m_tasks[0].Abandoned;
        }
        static bool Empty(const AssetPreviewPass& pass)
        {
            return pass.m_tasks[0].CurrentState == AssetPreviewPass::State::Empty;
        }
        static void Published(AssetPreviewPass& pass)
        {
            pass.m_published = true;
        }
        static void Submitted(AssetPreviewPass& pass, Primitives::Semaphore* timeline, uint64_t value)
        {
            AssetPreviewPass::OnSubmitted(&pass.m_tasks[0], timeline, value);
        }
        static uint64_t Timeline(const AssetPreviewPass& pass)
        {
            return pass.m_tasks[0].TimelineValue;
        }
    };
} // namespace ZEngine::Rendering::Renderers
