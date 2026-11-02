#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/AssetPreviewPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <algorithm>
#include <bit>
#include <cmath>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Managers;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        constexpr cstring ColorName = "AssetPreviewColor";
        constexpr cstring DepthName = "AssetPreviewDepth";
        uint64_t          Mix(uint64_t hash, uint64_t value)
        {
            for (uint32_t byte = 0; byte < 8; ++byte)
                hash = (hash ^ static_cast<uint8_t>(value >> (byte * 8))) * 1099511628211ull;
            return hash;
        }
        // No file I/O, no borrowed importer arrays escaping the try-lock.
        bool ReadMaterial(const AssetRecordSnapshot& record, Meshes::MeshMaterial* output, uint64_t* fingerprint, uint64_t* persistent_hash = nullptr)
        {
            auto* assets = AssetManager::Instance();
            if (!assets || !assets->Registry)
                return false;
            std::unique_lock lock(assets->IngestMutex, std::try_to_lock);
            if (!lock.owns_lock())
                return false;
            const auto* slot = assets->UUIDToMaterialSlot.find(record.Record.UUID);
            if (!slot || *slot >= assets->Materials.size())
                return false;
            const auto& material = assets->Materials[*slot];
            if (material.MaterialUUID != record.Record.UUID)
                return false;
            Meshes::MeshMaterial data = {};
            data.AlphaMode            = material.AlphaMode;
            for (uint32_t c = 0; c < 4; ++c)
            {
                data.AlbedoColor[c]    = material.AlbedoColor[c];
                data.EmissiveColor[c]  = material.EmissiveColor[c];
                data.RoughnessColor[c] = material.RoughnessColor[c];
                data.Factors[c]        = material.Factors[c];
                if (!std::isfinite(data.AlbedoColor[c]) || !std::isfinite(data.EmissiveColor[c]) || !std::isfinite(data.RoughnessColor[c]) || !std::isfinite(data.Factors[c]))
                    return false;
            }
            const uuids::uuid               ids[]   = {material.AlbedoTexUUID, material.EmissiveTexUUID, material.NormalTexUUID, material.OpacityTexUUID, material.SpecularTexUUID};
            const Core::Containers::String* paths[] = {&material.AlbedoTexPath, &material.EmissiveTexPath, &material.NormalTexPath, &material.OpacityTexPath, &material.SpecularTexPath};
            uint32_t*                       maps[]  = {&data.AlbedoMap, &data.EmissiveMap, &data.NormalMap, &data.OpacityMap, &data.SpecularMap};
            uint64_t                        hash    = Mix(Mix(14695981039346656037ull, AssetPreviewPass::EnvironmentRevision), static_cast<uint32_t>(material.AlphaMode));
            for (uint32_t c = 0; c < 4; ++c)
            {
                hash = Mix(hash, std::bit_cast<uint32_t>(data.AlbedoColor[c]));
                hash = Mix(hash, std::bit_cast<uint32_t>(data.Factors[c]));
            }
            uint64_t runtime = hash;
            for (uint32_t i = 0; i < 5; ++i)
            {
                AssetRecordSnapshot dependency = {};
                bool                found      = !ids[i].is_nil() && assets->Registry->TryReadRecord(ids[i], &dependency);
                if (!found && !paths[i]->empty())
                {
                    const auto path = VFSPath::Parse(paths[i]->c_str());
                    found           = path.Succeeded() && assets->Registry->TryReadRecordByPath(path.Value(), &dependency);
                }
                hash = Mix(hash, i);
                for (const auto byte : ids[i].as_bytes())
                    hash = Mix(hash, static_cast<uint8_t>(byte));
                if ((!ids[i].is_nil() || !paths[i]->empty()) && (!found || dependency.Record.Type != AssetType::TEXTURE || (dependency.Record.State != AssetState::Registered && dependency.Record.State != AssetState::Loaded)))
                    return false; // Missing/stale dependencies retain the authored fallback.
                if (!found)
                    continue;
                for (const auto byte : dependency.Record.UUID.as_bytes())
                    hash = Mix(hash, static_cast<uint8_t>(byte));
                hash              = Mix(hash, dependency.Record.Meta.SourceHash);
                runtime           = Mix(runtime, static_cast<uint64_t>(dependency.Record.State));
                const auto handle = AssetManager::FindTextureHandle(dependency.Record.UUID);
                if (!handle.Valid())
                    return false;
                runtime  = Mix(Mix(runtime, handle.Index), handle.Generation);
                *maps[i] = static_cast<uint32_t>(handle.Index);
            }
            *output      = data;
            *fingerprint = Mix(hash, runtime);
            if (persistent_hash)
                *persistent_hash = hash;
            return true;
        }
    } // namespace

    bool AssetPreviewPass::IsFullyTransparent(const Meshes::MeshMaterial& material)
    {
        return Materials::IsConstantFullyTransparent(material.AlphaMode, material.AlbedoColor.w, material.Factors.z, material.AlbedoMap != UINT32_MAX);
    }

    bool AssetPreviewPass::DependencyFingerprint(const AssetRecordSnapshot& record, uint64_t* hash, uint64_t* persistent_hash, bool* fully_transparent, Materials::AlphaInfo* alpha_info)
    {
        if (fully_transparent)
            *fully_transparent = false;
        if (alpha_info)
            *alpha_info = {};
        if (!hash)
            return false;
        if (record.Record.Type == AssetType::MESH)
        {
            *hash = EnvironmentRevision;
            if (persistent_hash)
                *persistent_hash = EnvironmentRevision;
            return true;
        }
        if (record.Record.Type != AssetType::MATERIAL)
            return false;
        Meshes::MeshMaterial material = {};
        if (!ReadMaterial(record, &material, hash, persistent_hash))
            return false;
        if (fully_transparent)
            *fully_transparent = IsFullyTransparent(material);
        if (alpha_info)
            *alpha_info = {.Mode = material.AlphaMode, .BaseOpacity = material.AlbedoColor.w, .Cutoff = material.Factors.z};
        return true;
    }

    bool AssetPreviewPass::FrameBounds(const Vec3f& center, float radius, Mat4f* output)
    {
        if (!output || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(radius) || radius < 1e-12f || radius > 1e12f)
            return false;
        // Vertices are recentered and divided by the validated bound's radius.
        // Thus clipping and distance are radius-relative, including tiny meshes.
        const Vec3f     back     = Vec3f(1.f, 0.65f, 1.35f).normalize();
        const Vec3f     right    = cross3d(Vec3f(0, 1, 0), back).normalize();
        const Vec3f     up       = cross3d(back, right);
        constexpr float distance = 3.2f; // > 1.15 / sin(45 degrees / 2)
        const Vec3f     eye      = back * distance;
        const Mat4f     view(right.x, right.y, right.z, -dot(right, eye), up.x, up.y, up.z, -dot(up, eye), back.x, back.y, back.z, -dot(back, eye), 0, 0, 0, 1);
        constexpr float n = distance - 1.25f, f = distance + 1.25f;
        const float     scale = 1.f / std::tan(3.14159265359f / 8.f);
        const Mat4f     projection(scale, 0, 0, 0, 0, -scale, 0, 0, 0, 0, f / (n - f), (n * f) / (n - f), 0, 0, -1, 0);
        *output = projection * view;
        return true;
    }

    void AssetPreviewPass::Initialize(Hardwares::VulkanDevice* device)
    {
        std::lock_guard lock(m_mutex);
        m_device      = device;
        Publish.Owner = this;
    }

    AssetPreviewPass::Task* AssetPreviewPass::Find(Textures::TextureHandle texture)
    {
        for (auto& task : m_tasks)
            if (task.CurrentState != State::Empty && task.Texture.Index == texture.Index && task.Texture.Generation == texture.Generation)
                return &task;
        return nullptr;
    }

    Textures::TextureHandle AssetPreviewPass::Request(const AssetRecordSnapshot& record, uint64_t dependency_hash, const char* cache_path, const RenderResourceManager::ThumbnailCacheIdentity* identity)
    {
        std::lock_guard lock(m_mutex);
        if (!m_device || (record.Record.Type != AssetType::MESH && record.Record.Type != AssetType::MATERIAL))
            return {};
        for (auto& task : m_tasks)
        {
            if (task.CurrentState != State::Empty)
                continue;
            const auto texture = m_device->CreateTexture({.IsUsageTransfert = false, .IsUsageTransferSource = true, .Width = Extent, .Height = Extent, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM}, "AssetPreview", RenderResourceManager::ThumbnailTextureSlotReserve);
            if (!texture.Valid())
                return {};
            task = {.Owner = this, .Record = record, .Texture = texture, .DependencyHash = dependency_hash, .CurrentState = State::Queued};
            if (cache_path && identity && Helpers::secure_strlen(cache_path) < sizeof(task.CachePath))
            {
                Helpers::secure_strcpy(task.CachePath, sizeof(task.CachePath), cache_path);
                task.CacheIdentity = *identity;
            }
            return texture;
        }
        return {};
    }

    AssetPreviewPass::Result AssetPreviewPass::Observe(Textures::TextureHandle texture)
    {
        std::lock_guard lock(m_mutex);
        const auto*     task = Find(texture);
        if (!task || task->CurrentState == State::Failed)
            return Result::Failed;
        return task->CurrentState == State::Succeeded ? Result::Succeeded : Result::Pending;
    }

    void AssetPreviewPass::Forget(Textures::TextureHandle texture)
    {
        std::lock_guard lock(m_mutex);
        if (auto* task = Find(texture); task && (task->CurrentState == State::Succeeded || task->CurrentState == State::Failed))
            *task = {}; // Ownership transfers to the editor's resident/LRU entry.
    }

    void AssetPreviewPass::Release(Task& task)
    {
        static_cast<RenderResourceManager*>(m_device->RRM)->ReleaseTexture(task.Texture);
        task = {};
    }

    bool AssetPreviewPass::Discard(Textures::TextureHandle texture)
    {
        std::lock_guard lock(m_mutex);
        auto*           task = Find(texture);
        if (!task)
            return false;
        task->Abandoned = true;
        if (task->CurrentState == State::Queued || task->CurrentState == State::Succeeded || task->CurrentState == State::Failed)
            Release(*task);
        return true;
    }

    bool AssetPreviewPass::PrepareTask(Task& task)
    {
        auto*               assets    = AssetManager::Instance();
        auto*               resources = static_cast<RenderResourceManager*>(m_device->RRM);
        AssetRecordSnapshot current   = {};
        if (!assets || !assets->Registry || !assets->Registry->TryReadRecord(task.Record.Record.UUID, &current) || current.Record.Meta.SourceHash != task.Record.Record.Meta.SourceHash || (current.Record.State != AssetState::Loaded && current.Record.State != AssetState::Registered))
        {
            task.CurrentState = State::Failed;
            return false;
        }
        m_uniform    = {};
        m_draw_count = 0;
        if (current.Record.Type == AssetType::MATERIAL)
        {
            // Never persist a render made from an allocated-but-not-decoded map,
            // or from the preceding bytes of a reload under the new source key.
            // Once pixels are queued for upload, graph bindless acquires handle
            // the exact GPU producer timeline without a CPU fence wait.
            if (resources->HasPendingTextureUploads())
                return false;
            Meshes::MeshMaterial material = {};
            uint64_t             hash     = 0;
            if (!ReadMaterial(current, &material, &hash))
                return false;
            if (hash != task.DependencyHash)
            {
                task.CurrentState = State::Failed;
                return false;
            }
            m_uniform.Albedo   = material.AlbedoColor;
            m_uniform.Emissive = material.EmissiveColor;
            m_uniform.Surface  = {std::clamp(material.RoughnessColor.y, 0.04f, 1.f), std::clamp(material.RoughnessColor.x, 0.f, 1.f), material.Factors.z, 1};
            m_uniform.Maps[5]  = static_cast<uint32_t>(material.AlphaMode);
            m_uniform.Maps[0]  = material.AlbedoMap;
            m_uniform.Maps[1]  = material.EmissiveMap;
            m_uniform.Maps[2]  = material.NormalMap;
            m_uniform.Maps[3]  = material.OpacityMap;
            m_uniform.Maps[4]  = material.SpecularMap;
            return FrameBounds({}, 1, &m_uniform.ViewProjection);
        }
        const auto handle = resources->FindMeshBuffer(current.Record.UUID);
        if (!resources->IsMeshResident(handle))
        {
            if (handle.IsValid())
                resources->RequestMeshLoad(handle, current.Record.UUID);
            return false;
        }
        std::unique_lock lock(assets->IngestMutex, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        const auto* mesh = assets->GetMeshAsset(current.Record.UUID);
        if (!mesh || !mesh->BoundsValid || mesh->SubMeshes.empty() || mesh->SubMeshes.size() > MaxDraws || !FrameBounds(mesh->BoundsCenter, mesh->BoundsRadius, &m_uniform.ViewProjection))
        {
            task.CurrentState = State::Failed;
            return false;
        }
        m_uniform.CenterRadius = {mesh->BoundsCenter.x, mesh->BoundsCenter.y, mesh->BoundsCenter.z, mesh->BoundsRadius};
        uint32_t vertex = 0, index = 0;
        if (!resources->GetMeshOffsets(handle, vertex, index))
            return false;
        for (const auto& submesh : mesh->SubMeshes)
            m_draws[m_draw_count++] = {vertex + submesh.VertexOffset, index + submesh.IndexOffset, submesh.IndexCount};
        resources->MarkMeshReferenced(handle);
        return true;
    }

    void AssetPreviewPass::BeginFrame()
    {
        std::lock_guard lock(m_mutex);
        m_current  = nullptr;
        m_recorded = m_published = false;
        m_color                  = {};
        m_cache_ticket           = nullptr;
        for (auto& task : m_tasks)
        {
            if (task.CurrentState == State::Submitted)
            {
                uint64_t   completed = 0;
                const auto result    = vkGetSemaphoreCounterValue(m_device->LogicalDevice, task.Timeline->GetHandle(), &completed);
                if (result != VK_SUCCESS)
                    task.CurrentState = State::Failed;
                else if (completed >= task.TimelineValue)
                    task.CurrentState = State::Succeeded;
            }
            if (task.Abandoned && (task.CurrentState == State::Succeeded || task.CurrentState == State::Failed))
                Release(task);
        }
        // Fair round-robin and one successful preview per render frame.
        for (uint32_t n = 0; n < Capacity; ++n)
        {
            auto& task = m_tasks[m_cursor++ % Capacity];
            if (task.CurrentState != State::Queued)
                continue;
            if (++task.Attempts > 180)
            {
                task.CurrentState = State::Failed;
                continue;
            }
            if (!PrepareTask(task))
                continue;
            task.CurrentState = State::Recording;
            m_current         = &task;
            break;
        }
    }

    void AssetPreviewPass::OnSubmitted(void* context, Primitives::Semaphore* timeline, uint64_t value)
    {
        auto&           task = *static_cast<Task*>(context);
        std::lock_guard lock(task.Owner->m_mutex);
        task.Timeline      = timeline;
        task.TimelineValue = value;
        task.CurrentState  = State::Submitted;
    }

    void AssetPreviewPass::OnCancelled(void* context)
    {
        auto&           task = *static_cast<Task*>(context);
        std::lock_guard lock(task.Owner->m_mutex);
        task.CurrentState = State::Failed;
    }

    void AssetPreviewPass::EndFrame()
    {
        std::lock_guard lock(m_mutex);
        if (!m_current)
            return;
        if (m_recorded && m_published)
        {
            static_cast<RenderResourceManager*>(m_device->RRM)->AcceptThumbnailCacheWrite(m_cache_ticket);
            m_device->SwapchainPtr->EnqueueRenderWorkSubmittedCallback(&OnSubmitted, m_current, &OnCancelled);
        }
        else
            m_current->CurrentState = State::Failed;
        m_current = nullptr;
    }

    Specifications::GraphicsPipelineDesc AssetPreviewPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator*) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "AssetPreview-Pipeline";
        desc.EnableDepthTest = desc.EnableDepthWrite = true;
        desc.CullMode                                = VK_CULL_MODE_NONE;
        desc.ShaderSpecificationValue.Name           = "asset_preview";
        return desc;
    }

    bool AssetPreviewPass::Register(Hardwares::VulkanDevicePtr device, cstring, const RenderGraphFrameContext&, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr)
    {
        if (!m_current || !device || !builder)
            return false;
        builder->ImportRenderTarget(ColorName, m_current->Texture);
        m_color = builder->WriteColorAttachment(
            ColorName,
            {
            .Width = Extent, .Height = Extent, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM, .ClearColor = {0.f, 0.f, 0.f, 0.f}
        });
        builder->Export(m_color);
        builder->WriteDepthAttachment(DepthName, {.Width = Extent, .Height = Extent, .Format = Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE});
        builder->ReadBuffer(RendererBufferName::GlobalVertex, "VertexSB");
        builder->ReadBuffer(RendererBufferName::GlobalIndex, "IndexSB");
        builder->ReadBindless();
        // The ticket is RRM-owned and independent of this task's lifetime. A
        // resident preview may be evicted while its PNG is still being encoded.
        auto* resources = static_cast<RenderResourceManager*>(device->RRM);
        if (m_current->CachePath[0] && resources)
        {
            void* ticket = resources->ReserveThumbnailCacheWrite(m_current->CachePath, m_current->CacheIdentity);
            if (ticket)
            {
                const RGImageReadbackRegion region = {
                    .Subresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                      .Offset = {0, 0, 0},
                      .Extent = {Extent, Extent, 1}
                };
                if (!builder->DeclareImageReadback("Asset Preview Cache Readback", m_color, region, &RenderResourceManager::CompleteThumbnailCacheReadback, ticket, Extent * Extent * 4, &RenderResourceManager::CancelThumbnailCacheReadback, true).Valid())
                    RenderResourceManager::CancelThumbnailCacheReadback(ticket);
                else
                    m_cache_ticket = ticket;
            }
        }
        return true;
    }

    void AssetPreviewPass::Prepare(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr, RenderGraphResourceInspectorPtr, RenderPasses::RenderPass* pass)
    {
        auto* graphic = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!graphic->Pipeline || !graphic->Pipeline->Shader || !graphic->Pipeline->Shader->HasStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT))
            return;
        graphic->SetDynamicUniform("UBPreview", sizeof(Uniform));
        graphic->UseTextureArray("TextureArray");
        graphic->SetSampler("PreviewSampler", device->GlobalLinearWrapSamplerImageInfo);
        const auto allocation = device->FrameHeaps[device->SwapchainPtr->CurrentFrame->Index].Push(&m_uniform, sizeof(m_uniform), device->MinUniformBufferOffsetAlignment());
        m_uniform_offset      = allocation.Offset;
    }

    void AssetPreviewPass::Execute(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr commands)
    {
        auto* graphic = static_cast<RenderPasses::GraphicPass*>(pass);
        if (framebuffer && commands->BeginRenderPass(graphic, framebuffer->Handle, false))
        {
            RecordDraw(device, inspector, scene, pass, framebuffer, commands);
            commands->EndRenderPass();
        }
    }

    bool AssetPreviewPass::RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr commands)
    {
        if (!m_current || !commands || !pass)
            return false;
        auto* pipeline = static_cast<RenderPasses::GraphicPass*>(pass)->Pipeline;
        // Vulkan permits vertex-only pipelines. A successful draw with one only
        // clears the preview target: never report/persist that as a valid image.
        if (!pipeline || !pipeline->Shader || !pipeline->Shader->HasStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT))
            return false;
        commands->SetViewport(Extent, Extent);
        commands->SetScissor(Extent, Extent);
        commands->BindPipeline(pipeline);
        commands->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &m_uniform_offset, 1);
        if (m_uniform.Surface.w == 1)
        {
            const uint32_t offsets[2] = {};
            commands->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(offsets), offsets);
            commands->Draw(64 * 32 * 6, 1, 0, 0);
        }
        else
        {
            for (uint32_t i = 0; i < m_draw_count; ++i)
            {
                const uint32_t offsets[] = {m_draws[i].VertexOffset, m_draws[i].IndexOffset};
                commands->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(offsets), offsets);
                commands->Draw(m_draws[i].Count, 1, 0, 0);
            }
        }
        m_recorded = true;
        return true;
    }

    bool AssetPreviewPass::PublishPass::Register(Hardwares::VulkanDevicePtr, cstring, const RenderGraphFrameContext&, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr)
    {
        if (!Owner || !Owner->m_color.Valid())
            return false;
        const auto sampled = builder->ReadTexture(Owner->m_color);
        return sampled.Valid();
    }

    void AssetPreviewPass::PublishPass::Execute(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr, RenderPasses::RenderPass*, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr)
    {
        // The graph has now transitioned the target to sampled-image layout.
        device->RequestDescriptorUpdate(Owner->m_current->Texture);
        Owner->m_published = true;
    }

    void AssetPreviewPass::Deinitialize(Hardwares::VulkanDevicePtr)
    {
        std::lock_guard lock(m_mutex);
        for (auto& task : m_tasks)
            if (task.CurrentState != State::Empty)
                Release(task);
        m_current = nullptr;
        m_device  = nullptr;
    }
} // namespace ZEngine::Rendering::Renderers
