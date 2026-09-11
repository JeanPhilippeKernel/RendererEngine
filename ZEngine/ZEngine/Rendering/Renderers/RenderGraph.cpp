#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Renderers/RenderGraphTopology.h>
#include <algorithm>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers
{
    void BuildQueueBatches(ArrayView<RGPass> passes, ArrayView<uint32_t> order, bool has_separate_transfer_queue, bool has_separate_compute_queue, Array<RGQueueBatch>& out_batches)
    {
        out_batches.clear();

        for (uint32_t order_index = 0; order_index < order.size(); ++order_index)
        {
            const uint32_t pass_index = order[order_index];
            if (pass_index >= passes.size() || !passes[pass_index].Enabled)
                continue;

            RGPass& pass = passes[pass_index];
            pass.Queue   = pass.RequestedQueue;
            if ((pass.Queue == Rendering::QueueType::TRANSFER_QUEUE && !has_separate_transfer_queue) || (pass.Queue == Rendering::QueueType::COMPUTE_QUEUE && !has_separate_compute_queue))
                pass.Queue = Rendering::QueueType::GRAPHIC_QUEUE;

            if (!out_batches.empty() && out_batches.back().Queue == pass.Queue && out_batches.back().FirstPassOrder + out_batches.back().PassCount == order_index)
            {
                ++out_batches.back().PassCount;
                continue;
            }

            auto& batch          = out_batches.push_use({});
            batch.Queue          = pass.Queue;
            batch.FirstPassOrder = order_index;
            batch.PassCount      = 1;
        }
    }

    void BuildQueueDependencies(Core::Memory::ArenaAllocator* scratch_arena, ArrayView<RGPass> passes, ArrayView<uint32_t> order, ArrayView<RGQueueBatch> batches, uint32_t resource_count, Array<RGQueueDependency>& out_dependencies)
    {
        out_dependencies.clear();
        if (batches.size() == 0 || resource_count == 0)
            return;

        Array<uint32_t> batch_for_order;
        batch_for_order.init(scratch_arena, order.size(), order.size());
        for (uint32_t i = 0; i < order.size(); ++i)
            batch_for_order[i] = UINT32_MAX;
        for (uint32_t batch_index = 0; batch_index < batches.size(); ++batch_index)
        {
            const auto& batch = batches[batch_index];
            for (uint32_t i = 0; i < batch.PassCount; ++i)
                batch_for_order[batch.FirstPassOrder + i] = batch_index;
        }

        Array<uint32_t> last_batch_for_resource;
        last_batch_for_resource.init(scratch_arena, resource_count, resource_count);
        for (uint32_t i = 0; i < resource_count; ++i)
            last_batch_for_resource[i] = UINT32_MAX;

        auto record_access = [&](const RGPassResource& access, uint32_t current_batch) {
            if (!access.Handle.Valid() || access.Handle.Index >= resource_count)
                return;
            const uint32_t previous_batch = last_batch_for_resource[access.Handle.Index];
            if (previous_batch != UINT32_MAX && previous_batch != current_batch && batches[previous_batch].Queue != batches[current_batch].Queue)
            {
                bool already_recorded = false;
                for (const auto& dependency : out_dependencies)
                {
                    if (dependency.FromBatch == previous_batch && dependency.ToBatch == current_batch)
                    {
                        already_recorded = true;
                        break;
                    }
                }
                if (!already_recorded)
                    out_dependencies.push({previous_batch, current_batch});
            }
            last_batch_for_resource[access.Handle.Index] = current_batch;
        };

        for (uint32_t order_index = 0; order_index < order.size(); ++order_index)
        {
            const uint32_t pass_index = order[order_index];
            if (pass_index >= passes.size() || !passes[pass_index].Enabled)
                continue;
            const uint32_t batch_index = batch_for_order[order_index];
            if (batch_index == UINT32_MAX)
                continue;
            for (const auto& read : passes[pass_index].Reads)
                record_access(read, batch_index);
            for (const auto& write : passes[pass_index].Writes)
                record_access(write, batch_index);
        }
    }

    void BuildQueueOwnershipTransfers(Core::Memory::ArenaAllocator* scratch_arena, ArrayView<RGPass> passes, ArrayView<uint32_t> order, ArrayView<RGQueueBatch> batches, uint32_t resource_count, Array<RGQueueOwnershipTransfer>& out_transfers)
    {
        out_transfers.clear();
        if (batches.size() == 0 || resource_count == 0)
            return;

        Array<uint32_t> batch_for_order;
        batch_for_order.init(scratch_arena, order.size(), order.size());
        for (uint32_t batch_index = 0; batch_index < batches.size(); ++batch_index)
        {
            const auto& batch = batches[batch_index];
            for (uint32_t i = 0; i < batch.PassCount; ++i)
                batch_for_order[batch.FirstPassOrder + i] = batch_index;
        }

        Array<uint32_t> last_batch_for_resource;
        last_batch_for_resource.init(scratch_arena, resource_count, resource_count);
        for (uint32_t i = 0; i < resource_count; ++i)
            last_batch_for_resource[i] = UINT32_MAX;

        auto record_access = [&](const RGPassResource& access, uint32_t current_batch) {
            if (!access.Handle.Valid() || access.Handle.Index >= resource_count)
                return;
            const uint32_t previous_batch = last_batch_for_resource[access.Handle.Index];
            if (previous_batch != UINT32_MAX && previous_batch != current_batch && batches[previous_batch].Queue != batches[current_batch].Queue)
                out_transfers.push({access.Handle.Index, previous_batch, current_batch});
            last_batch_for_resource[access.Handle.Index] = current_batch;
        };

        for (uint32_t order_index = 0; order_index < order.size(); ++order_index)
        {
            const uint32_t pass_index = order[order_index];
            if (pass_index >= passes.size() || !passes[pass_index].Enabled)
                continue;
            const uint32_t batch_index = batch_for_order[order_index];
            for (const auto& read : passes[pass_index].Reads)
                record_access(read, batch_index);
            for (const auto& write : passes[pass_index].Writes)
                record_access(write, batch_index);
        }
    }

    // kAccessTable — stage + access + layout for every RGAccess value.
    static constexpr struct
    {
        VkPipelineStageFlags2 Stage;
        VkAccessFlags2        Access;
        VkImageLayout         Layout;
    } kAccessTable[static_cast<int>(RGAccess::Count_)] = {
        // None
        {                                                       VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,                                                                                              0,                        VK_IMAGE_LAYOUT_UNDEFINED},
        // ColorWrite
        {                                           VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,                                                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        // DepthWrite
        {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        // DepthRead — stays in DEPTH_STENCIL_ATTACHMENT_OPTIMAL; depthWrite=false in pipeline
        {                                              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,                                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        // ShaderRead
        {                                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,                                                                    VK_ACCESS_2_SHADER_READ_BIT,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        // ShaderReadWrite
        {                                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,                                     VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,                          VK_IMAGE_LAYOUT_GENERAL},
        // TransferRead
        {                                                          VK_PIPELINE_STAGE_2_TRANSFER_BIT,                                                                  VK_ACCESS_2_TRANSFER_READ_BIT,             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
        // TransferWrite
        {                                                          VK_PIPELINE_STAGE_2_TRANSFER_BIT,                                                                 VK_ACCESS_2_TRANSFER_WRITE_BIT,             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
        // Present
        {                                                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,                                                                                              0,                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
        // BufferRead
        {                                                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,                                                                    VK_ACCESS_2_SHADER_READ_BIT,                        VK_IMAGE_LAYOUT_UNDEFINED},
        // BufferReadWrite
        {                                                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,                                     VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,                        VK_IMAGE_LAYOUT_UNDEFINED},
        // IndirectRead
        {                                                     VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,                                                          VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,                        VK_IMAGE_LAYOUT_UNDEFINED},
    };

    RGResourceState GetRGAccessState(RGAccess access)
    {
        const uint32_t index = static_cast<uint32_t>(access);
        if (index >= static_cast<uint32_t>(RGAccess::Count_))
            return {};

        const auto& access_info = kAccessTable[index];
        return {access_info.Stage, access_info.Access, access_info.Layout};
    }

    static VkImage GetVkImage(Hardwares::VulkanDevice* device, Textures::TextureHandle handle)
    {
        if (!handle.Valid())
            return VK_NULL_HANDLE;
        auto* tex = device->GlobalTextures.Access(handle);
        if (!tex)
            return VK_NULL_HANDLE;
        auto* img_buf = device->ImageBufferManager.Access(tex->BufferHandle);
        if (!img_buf)
            return VK_NULL_HANDLE;
        return img_buf->GetBuffer().Handle;
    }

    static VkImageSubresourceRange FullSubresourceRange(const RGResource& res, Hardwares::VulkanDevice* device)
    {
        bool depth = (res.Spec.Format == Rendering::Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE);
        // For imported (external) resources the spec may be empty; check the actual texture.
        if (!depth && res.TextureHandle.Valid())
        {
            auto* tex = device->GlobalTextures.Access(res.TextureHandle);
            if (tex && tex->IsDepthTexture)
                depth = true;
        }
        VkImageSubresourceRange r = {};
        r.aspectMask              = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        r.baseMipLevel            = 0;
        r.levelCount              = VK_REMAINING_MIP_LEVELS;
        r.baseArrayLayer          = 0;
        r.layerCount              = VK_REMAINING_ARRAY_LAYERS;
        return r;
    }

    static bool NeedsImageBarrier(const RGResourceState& src, const RGResourceState& dst)
    {
        constexpr VkAccessFlags2 write_accesses = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
        return src.Layout != dst.Layout || (src.Access & write_accesses) != 0 || (dst.Access & write_accesses) != 0;
    }

    static bool NeedsBufferBarrier(const RGResourceState& src, const RGResourceState& dst)
    {
        constexpr VkAccessFlags2 write_accesses = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
        return (src.Access & write_accesses) != 0 || (dst.Access & write_accesses) != 0;
    }

    // Read-after-read in the same layout needs no execution dependency. Preserve
    // both read stages so a later write waits for every prior reader.
    static void MergeReadState(RGResourceState& state, const RGResourceState& read_state)
    {
        state.Stage  |= read_state.Stage;
        state.Access |= read_state.Access;
    }

    void RGTransientPool::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        Slots.init(arena, 32);
    }

    Textures::TextureHandle RGTransientPool::TryAlias(const Specifications::TextureSpecification& spec, uint32_t first_pass)
    {
        for (auto& slot : Slots)
        {
            if (slot.FreeAfterPass >= first_pass)
                continue;
            const auto& s = slot.Spec;
            if (s.Format != spec.Format || s.Width != spec.Width || s.Height != spec.Height || s.LayerCount != spec.LayerCount || s.IsCubemap != spec.IsCubemap)
                continue;
            // Reuse is legal only when the existing image was created with every
            // usage bit required by the new logical resource.
            if ((spec.IsUsageSampled && !s.IsUsageSampled) || (spec.IsUsageStorage && !s.IsUsageStorage) || (spec.IsUsageTransfert && !s.IsUsageTransfert) || (spec.IsUsageTransferSource && !s.IsUsageTransferSource))
                continue;
            return slot.Handle;
        }
        return {};
    }

    void RGTransientPool::Register(Textures::TextureHandle handle, const Specifications::TextureSpecification& spec, uint32_t last_pass)
    {
        auto& slot         = Slots.push_use({});
        slot.Handle        = handle;
        slot.Spec          = spec;
        slot.FreeAfterPass = last_pass;
    }

    void RGTransientPool::MarkInUse(Textures::TextureHandle handle, uint32_t last_pass)
    {
        for (auto& slot : Slots)
        {
            if (slot.Handle.Index == handle.Index && slot.Handle.Generation == handle.Generation)
            {
                slot.FreeAfterPass = last_pass;
                return;
            }
        }
    }

    void RGTransientPool::Clear()
    {
        Slots.clear();
    }

    void RenderGraph::Initialize(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr data)
    {
        Device    = device;
        SceneData = data;

        Passes.init(Device->Arena, 16);
        Resources.init(Device->Arena, 32);
        SortedPassIndices.init(Device->Arena, 16);
        QueueBatches.init(Device->Arena, 8);
        QueueDependencies.init(Device->Arena, 8);
        QueueOwnershipTransfers.init(Device->Arena, 16);
        ResourceIndex.init(Device->Arena, 64);
        PassIndex.init(Device->Arena, 32);
        TransientPool.Initialize(Device->Arena);

        for (uint32_t i = 0; i < QueueTimelineCount; ++i)
            QueueTimelines[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Primitives::Semaphore, Device, true);

        ResourceBuilder   = ZPushStruct(Device->Arena, RenderGraphResourceBuilder);
        ResourceInspector = ZPushStruct(Device->Arena, RenderGraphResourceInspector);
        RenderPassBuilder = ZPushStructCtorArgs(Device->Arena, RenderPasses::RenderPassBuilder);

        RenderPassBuilder->Initialize(Device->Arena);
        ResourceBuilder->Initialize(this);
        ResourceInspector->Initialize(this);
    }

    void RenderGraph::AddCallbackPass(cstring pass_name, IRenderGraphCallbackPass* const cb, bool enabled)
    {
        uint32_t idx = static_cast<uint32_t>(Passes.size());
        auto&    p   = Passes.push_use({});
        p.Name       = pass_name;
        p.Enabled    = enabled;
        p.Callback   = cb;
        p.Reads.init(Device->Arena, 8);
        p.Writes.init(Device->Arena, 8);
        p.BarrierPlans.init(Device->Arena, 8);
        p.BufferBarrierPlans.init(Device->Arena, 8);

        PassIndex[pass_name] = idx;
    }

    void RenderGraph::Setup()
    {
        for (uint32_t i = 0; i < Passes.size(); ++i)
        {
            ResourceBuilder->CurrentPass = i;
            Passes[i].Callback->Setup(Device, Passes[i].Name, ResourceBuilder, ResourceInspector);
        }
        ResourceBuilder->CurrentPass = UINT32_MAX;
    }

    void RenderGraph::Compile()
    {
        // BuildTopology must run first — BuildLifetimes indexes by sorted execution
        // order, not raw declaration order, so lifetimes can only be computed once the
        // real order is known.
        if (!BuildTopology())
        {
            m_compile_valid   = false;
            m_needs_recompile = true;
            return;
        }
        if (!ValidateDeclarations())
        {
            m_compile_valid   = false;
            m_needs_recompile = true;
            return;
        }
        BuildLifetimes();
        AllocateTransientResources();
        BuildBarriers();

        for (uint32_t i = 0; i < SortedPassIndices.size(); ++i)
        {
            uint32_t pi   = SortedPassIndices[i];
            RGPass&  pass = Passes[pi];
            if (!pass.Enabled || !pass.Callback)
                continue;

            // Detach() resets RenderPassBuilder. Existing pass implementations
            // only detach while creating their RenderPass, so feeding the shared
            // builder for an already-created pass would leak those attachments
            // into a later newly enabled pass.
            if (!pass.Handle)
            {
                // Pre-populate the builder with resolved resource handles so the
                // pass's Compile() receives its own input attachments and targets.
                // Only DepthRead reads become VkRenderPass input attachments;
                // ShaderRead resources are bound by the pass's Compile().
                for (const auto& r : pass.Reads)
                {
                    if (!r.Handle.Valid())
                        continue;
                    if (r.Access != RGAccess::DepthRead && r.Access != RGAccess::DepthWrite)
                        continue;
                    const auto& res = Resources[r.Handle.Index];
                    if (res.TextureHandle.Valid())
                        RenderPassBuilder->AddInputAttachment(res.TextureHandle);
                }
                for (const auto& w : pass.Writes)
                {
                    if (!w.Handle.Valid())
                        continue;
                    const auto& res = Resources[w.Handle.Index];
                    if (res.TextureHandle.Valid())
                        RenderPassBuilder->UseRenderTarget(res.TextureHandle, w.LoadOp);
                }
            }

            pass.Callback->Compile(Device, SceneData, RenderPassBuilder, ResourceInspector, &pass.Handle);
        }

        AllocateFramebuffers();
        BuildQueueSchedule();
        m_needs_recompile = false;
        m_compile_valid   = true;
    }

    Hardwares::CommandBuffer* RenderGraph::Execute(Hardwares::CommandBufferPtr const cb)
    {
        if (m_needs_recompile)
            Compile();
        if (!m_compile_valid)
            return cb;

        // Imported graph buffers currently model the scene's host-visible buffers.
        // They may be updated by the CPU before every frame, so restore their
        // declared external producer state before stamping this frame's barriers.
        for (auto& resource : Resources)
        {
            if (resource.Kind == RGResourceKind::Buffer && resource.External)
                resource.RuntimeState = resource.InitialState;
        }

        cb->ClearColor(0.11f, 0.11f, 0.11f, 1.0f);
        cb->ClearDepth(1.0f, 0);

        auto scratch = ZGetScratch(Device->Arena);

        // The application-owned primary buffer stays open for overlay rendering.
        // Therefore only the final graphics batch is retained for it; all earlier
        // batches are submitted immediately on their resolved queue.
        if (QueueBatches.empty())
        {
            ZReleaseScratch(scratch);
            return cb;
        }

        ZENGINE_VALIDATE_ASSERT(QueueBatches.size() <= Device->CommandBufferMgr->MaxGraphBatchesPerPool, "Render graph batch command buffer capacity exceeded")

        const uint32_t                           external_async_operation_count = static_cast<uint32_t>(Device->SwapchainPtr->FrameAsyncOperations.size());
        const uint32_t                           final_batch_index              = static_cast<uint32_t>(QueueBatches.size() - 1);
        const uint8_t                            frame_index                    = static_cast<uint8_t>(Device->SwapchainPtr->CurrentFrame->Index);

        Array<Rendering::Primitives::Semaphore*> batch_timelines;
        Array<uint64_t>                          batch_signal_values;
        batch_timelines.init(scratch.Arena, QueueBatches.size(), QueueBatches.size());
        batch_signal_values.init(scratch.Arena, QueueBatches.size(), QueueBatches.size());
        for (uint32_t i = 0; i < QueueBatches.size(); ++i)
        {
            batch_timelines[i]     = nullptr;
            batch_signal_values[i] = 0;
        }

        auto add_wait = [](Array<VkSemaphoreSubmitInfo>& waits, Rendering::Primitives::Semaphore* timeline, uint64_t value, VkPipelineStageFlags2 stages) {
            if (!timeline || value == 0)
                return;

            for (auto& wait : waits)
            {
                if (wait.semaphore != timeline->GetHandle())
                    continue;
                wait.value      = std::max(wait.value, value);
                wait.stageMask |= stages;
                return;
            }

            waits.push({
                .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = timeline->GetHandle(),
                .value     = value,
                .stageMask = stages,
            });
        };

        auto emit_ownership_barriers = [&](uint32_t batch_index, bool release, Hardwares::CommandBuffer* target) {
            Core::Containers::Array<VkImageMemoryBarrier2>  image_barriers;
            Core::Containers::Array<VkBufferMemoryBarrier2> buffer_barriers;
            image_barriers.init(scratch.Arena, 8);
            buffer_barriers.init(scratch.Arena, 8);
            for (const auto& transfer : QueueOwnershipTransfers)
            {
                if ((release ? transfer.FromBatch : transfer.ToBatch) != batch_index || transfer.ResourceIndex >= Resources.size() || transfer.FromBatch >= QueueBatches.size() || transfer.ToBatch >= QueueBatches.size())
                    continue;
                RGResource& resource = Resources[transfer.ResourceIndex];
                const auto& source   = QueueBatches[transfer.FromBatch];
                const auto& dest     = QueueBatches[transfer.ToBatch];
                if (Device->GetQueue(source.Queue).FamilyIndex == Device->GetQueue(dest.Queue).FamilyIndex)
                    continue;
                if (resource.Kind == RGResourceKind::Buffer)
                {
                    if (!resource.Buffer || resource.Buffer->Handle == VK_NULL_HANDLE)
                        continue;
                    VkBufferMemoryBarrier2 barrier = {
                        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .srcStageMask        = release ? resource.RuntimeState.Stage : VK_PIPELINE_STAGE_2_NONE,
                        .srcAccessMask       = release ? resource.RuntimeState.Access : 0,
                        .dstStageMask        = release ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        .dstAccessMask       = release ? 0 : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                        .srcQueueFamilyIndex = Device->GetQueue(source.Queue).FamilyIndex,
                        .dstQueueFamilyIndex = Device->GetQueue(dest.Queue).FamilyIndex,
                        .buffer              = resource.Buffer->Handle,
                        .offset              = 0,
                        .size                = VK_WHOLE_SIZE};
                    buffer_barriers.push(barrier);
                }
                else
                {
                    VkImage image = GetVkImage(Device, resource.TextureHandle);
                    if (image == VK_NULL_HANDLE)
                        continue;
                    VkImageMemoryBarrier2 barrier = {
                        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask        = release ? resource.RuntimeState.Stage : VK_PIPELINE_STAGE_2_NONE,
                        .srcAccessMask       = release ? resource.RuntimeState.Access : 0,
                        .dstStageMask        = release ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        .dstAccessMask       = release ? 0 : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                        .oldLayout           = resource.RuntimeState.Layout,
                        .newLayout           = resource.RuntimeState.Layout,
                        .srcQueueFamilyIndex = Device->GetQueue(source.Queue).FamilyIndex,
                        .dstQueueFamilyIndex = Device->GetQueue(dest.Queue).FamilyIndex,
                        .image               = image,
                        .subresourceRange    = FullSubresourceRange(resource, Device)};
                    image_barriers.push(barrier);
                }
                if (!release)
                    resource.RuntimeState = {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, resource.RuntimeState.Layout};
            }
            if (!image_barriers.empty() || !buffer_barriers.empty())
            {
                VkDependencyInfo dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = static_cast<uint32_t>(image_barriers.size()), .pImageMemoryBarriers = image_barriers.data(), .bufferMemoryBarrierCount = static_cast<uint32_t>(buffer_barriers.size()), .pBufferMemoryBarriers = buffer_barriers.data()};
                target->PipelineBarrier2(dependency);
            }
        };

        auto record_batch = [&](uint32_t batch_index, Hardwares::CommandBuffer* target) {
            const auto& batch = QueueBatches[batch_index];
            emit_ownership_barriers(batch_index, false, target);

            const uint32_t batch_end = batch.FirstPassOrder + batch.PassCount;
            for (uint32_t i = batch.FirstPassOrder; i < batch_end; ++i)
            {
                if (i >= SortedPassIndices.size())
                    break;

                RGPass& pass = Passes[SortedPassIndices[i]];
                if (!pass.Enabled)
                    continue;

                // Every path below, including a pass skipped for missing runtime
                // objects, closes this label before advancing to the next pass.
                target->BeginDebugLabel(pass.Name);

                // Stamp compile-time transition intents with this frame's actual image
                // and old state. Imported resources may change backing image and layouts
                // are execution-history dependent, so neither belongs in Compile().
                Core::Containers::Array<VkImageMemoryBarrier2> barriers;
                barriers.init(scratch.Arena, 8);
                Core::Containers::Array<VkBufferMemoryBarrier2> buffer_barriers;
                buffer_barriers.init(scratch.Arena, 8);

                for (const auto& plan : pass.BarrierPlans)
                {
                    if (plan.ResourceIndex >= Resources.size())
                        continue;

                    RGResource& res = Resources[plan.ResourceIndex];
                    const auto& dst = plan.DestinationState;
                    if (!plan.DiscardContents && !NeedsImageBarrier(res.RuntimeState, dst))
                    {
                        MergeReadState(res.RuntimeState, dst);
                        continue;
                    }

                    VkImageMemoryBarrier2 b = {};
                    b.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    b.oldLayout             = plan.DiscardContents ? VK_IMAGE_LAYOUT_UNDEFINED : res.RuntimeState.Layout;
                    b.newLayout             = dst.Layout;
                    b.srcStageMask          = plan.DiscardContents ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : res.RuntimeState.Stage;
                    b.srcAccessMask         = plan.DiscardContents ? 0 : res.RuntimeState.Access;
                    b.dstStageMask          = dst.Stage;
                    b.dstAccessMask         = dst.Access;
                    b.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
                    b.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
                    b.image                 = GetVkImage(Device, res.TextureHandle);
                    b.subresourceRange      = FullSubresourceRange(res, Device);
                    if (b.image == VK_NULL_HANDLE)
                        continue;

                    barriers.push(b);
                    res.RuntimeState = {dst.Stage, dst.Access, dst.Layout};
                }

                for (const auto& plan : pass.BufferBarrierPlans)
                {
                    if (plan.ResourceIndex >= Resources.size())
                        continue;
                    RGResource& res = Resources[plan.ResourceIndex];
                    if (!res.Buffer || res.Buffer->Handle == VK_NULL_HANDLE)
                        continue;
                    const auto& dst = plan.DestinationState;
                    if (!NeedsBufferBarrier(res.RuntimeState, dst))
                    {
                        MergeReadState(res.RuntimeState, dst);
                        continue;
                    }

                    VkBufferMemoryBarrier2 barrier = {};
                    barrier.sType                  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    barrier.srcStageMask           = res.RuntimeState.Stage;
                    barrier.srcAccessMask          = res.RuntimeState.Access;
                    barrier.dstStageMask           = dst.Stage;
                    barrier.dstAccessMask          = dst.Access;
                    barrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
                    barrier.buffer                 = res.Buffer->Handle;
                    barrier.offset                 = 0;
                    barrier.size                   = VK_WHOLE_SIZE;
                    buffer_barriers.push(barrier);
                    res.RuntimeState = dst;
                }

                if (!barriers.empty() || !buffer_barriers.empty())
                {
                    VkDependencyInfo dependency         = {};
                    dependency.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dependency.imageMemoryBarrierCount  = static_cast<uint32_t>(barriers.size());
                    dependency.pImageMemoryBarriers     = barriers.data();
                    dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(buffer_barriers.size());
                    dependency.pBufferMemoryBarriers    = buffer_barriers.data();
                    target->PipelineBarrier2(dependency);
                }

                const bool is_graphics = pass.Handle && pass.Handle->Specification.Type == Specifications::RenderPassType::GRAPHIC;
                if (!pass.Callback || (is_graphics && (!pass.Framebuffer || !pass.Framebuffer->Handle)))
                {
                    target->EndDebugLabel();
                    continue;
                }
                pass.Callback->Execute(Device, ResourceInspector, SceneData, pass.Handle, pass.Framebuffer, target);
                target->EndDebugLabel();
            }
            emit_ownership_barriers(batch_index, true, target);
        };

        for (uint32_t batch_index = 0; batch_index < QueueBatches.size(); ++batch_index)
        {
            const auto&               batch              = QueueBatches[batch_index];
            const bool                retain_for_overlay = batch_index == final_batch_index && batch.Queue == Rendering::QueueType::GRAPHIC_QUEUE;
            Hardwares::CommandBuffer* target             = retain_for_overlay ? cb : Device->CommandBufferMgr->GetGraphBatchCommandBuffer(batch.Queue, frame_index, 0, static_cast<uint8_t>(batch_index), true);

            if (!retain_for_overlay)
            {
                target->ClearColor(0.11f, 0.11f, 0.11f, 1.0f);
                target->ClearDepth(1.0f, 0);
            }

            record_batch(batch_index, target);

            if (retain_for_overlay)
                continue;

            target->End();

            Array<VkSemaphoreSubmitInfo> wait_infos;
            wait_infos.init(scratch.Arena, external_async_operation_count + QueueDependencies.size());
            for (uint32_t operation_index = 0; operation_index < external_async_operation_count; ++operation_index)
            {
                const auto& operation = Device->SwapchainPtr->FrameAsyncOperations[operation_index];
                add_wait(wait_infos, operation.Timeline, operation.SignalValue, operation.StageFlags);
            }
            for (const auto& dependency : QueueDependencies)
            {
                if (dependency.ToBatch != batch_index || dependency.FromBatch >= batch_signal_values.size())
                    continue;
                add_wait(wait_infos, batch_timelines[dependency.FromBatch], batch_signal_values[dependency.FromBatch], VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
            }

            const uint32_t timeline_index = static_cast<uint32_t>(batch.Queue);
            ZENGINE_VALIDATE_ASSERT(timeline_index < QueueTimelineCount, "Invalid render graph queue type")
            auto* const    timeline     = QueueTimelines[timeline_index];
            const uint64_t signal_value = ++QueueTimelineValues[timeline_index];
            if (!Device->QueueSubmit(target, timeline, signal_value, wait_infos.data(), static_cast<uint32_t>(wait_infos.size())))
                break;

            batch_timelines[batch_index]     = timeline;
            batch_signal_values[batch_index] = signal_value;
            // RenderGraph and DeviceSwapchain run on the render thread. Appending
            // directly avoids re-draining the MPSC producer queue while ensuring
            // Present waits on every independently-signalled queue timeline.
            Device->SwapchainPtr->FrameAsyncOperations.push({VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, signal_value, timeline});
        }

        ZReleaseScratch(scratch);
        return cb;
    }

    void RenderGraph::Resize(uint32_t width, uint32_t height)
    {
        // Phase 1 — collect old Vulkan handles that need to be freed.
        // Do NOT call any vkDestroy* yet — new resources must be created first
        // so the driver cannot recycle these handles for new allocations.
        TransientPool.Clear();
        uint64_t      timeline     = Device->SwapchainPtr->RenderTimelineNextValue;

        // Stack-local scratch for old framebuffer handles (max 16 passes).
        VkFramebuffer old_fbs[16]  = {};
        uint32_t      old_fb_count = 0;

        for (auto& pass : Passes)
        {
            if (pass.Framebuffer && pass.Framebuffer->Handle)
            {
                if (old_fb_count < 16)
                    old_fbs[old_fb_count++] = pass.Framebuffer->Handle;
                pass.Framebuffer->Handle = VK_NULL_HANDLE;
            }
        }

        // Reconstruct each transient resource in place — same TextureHandle, same slot, so
        // the editor's cached ImTextureID stays valid. Old VkImage is defer-freed inside
        // ReconstructTexture itself.
        for (auto& res : Resources)
        {
            if (res.External || !res.Transient)
                continue;
            res.Spec.Width   = width;
            res.Spec.Height  = height;
            res.CurrentState = {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
            res.RuntimeState = {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
            if (!res.TextureHandle.Valid())
                continue;
            Device->ReconstructTexture(res.TextureHandle, res.Spec);
        }

        // Phase 2 — rebuild framebuffers and re-bind descriptors with new ImageBuffers.
        // All TextureHandles remain valid (in-place swap) so AllocateTransientResources
        // is a no-op for existing resources; call it only for safety (skips valid handles).

        if (const auto* idx = ResourceIndex.find(RendererResourceName::FrameColorRenderTargetName))
            Device->TextureHandleToUpdates.Enqueue(Resources[*idx].TextureHandle);

        // Sync pass Specification.Inputs and ExternalOutputs to the new handles so
        // CommandBuffer::BeginRenderPass builds clear values from valid pointers.
        for (auto& pass : Passes)
        {
            if (!pass.Handle)
                continue;
            uint32_t out_idx = 0;
            for (const auto& w : pass.Writes)
            {
                if (w.Handle.Valid() && out_idx < pass.Handle->Specification.ExternalOutputs.size())
                {
                    const auto& res = Resources[w.Handle.Index];
                    if (res.TextureHandle.Valid())
                        pass.Handle->Specification.ExternalOutputs[out_idx] = res.TextureHandle;
                }
                ++out_idx;
            }
            uint32_t in_idx = 0;
            for (const auto& r : pass.Reads)
            {
                if (!r.Handle.Valid())
                    continue;
                if (r.Access != RGAccess::DepthRead && r.Access != RGAccess::DepthWrite)
                    continue;
                if (in_idx < pass.Handle->Specification.Inputs.size())
                {
                    const auto& res = Resources[r.Handle.Index];
                    if (res.TextureHandle.Valid())
                        pass.Handle->Specification.Inputs[in_idx] = res.TextureHandle;
                }
                ++in_idx;
            }
        }

        AllocateFramebuffers();

        for (auto& pass : Passes)
        {
            if (!pass.Handle || pass.Handle->Specification.Type == Specifications::RenderPassType::COMPUTE)
                continue;
            auto* gp = static_cast<RenderPasses::GraphicPass*>(pass.Handle);
            for (const auto& r : pass.Reads)
            {
                if (!r.Handle.Valid() || !r.BindingKey)
                    continue;
                const auto& res = Resources[r.Handle.Index];
                if (res.TextureHandle.Valid())
                    gp->SetTexture(r.BindingKey, res.TextureHandle);
            }
        }

        // Phase 3 — now that new resources are live, schedule old ones for GPU-safe deletion.
        // Framebuffers must be enqueued before their referenced image views.
        for (uint32_t i = 0; i < old_fb_count; ++i)
        {
            Hardwares::DeferredFreeEntry e;
            e.EntryKind     = Hardwares::DeferredFreeEntry::Kind::VkHandle;
            e.TimelineValue = timeline;
            e.Data.Vk       = {reinterpret_cast<void*>(old_fbs[i]), Rendering::DeviceResourceType::FRAMEBUFFER, nullptr};
            Device->DeferFree(e);
        }
    }

    void RenderGraph::Dispose()
    {
        for (uint32_t i = 0; i < QueueTimelineCount; ++i)
        {
            if (QueueTimelines[i])
            {
                QueueTimelines[i]->~Semaphore();
                QueueTimelines[i] = nullptr;
            }
        }

        for (auto& res : Resources)
        {
            if (res.External || !res.Transient || !res.TextureHandle.Valid())
                continue;
            auto* tex = Device->GlobalTextures.Access(res.TextureHandle);
            if (!tex)
                continue;
            auto* img = Device->ImageBufferManager.Access(tex->BufferHandle);
            if (img)
                img->Dispose();
        }

        for (auto& pass : Passes)
        {
            if (pass.Callback)
                pass.Callback->Deinitialize(Device);
        }
    }

    RGResourceHandle RenderGraph::ImportRenderTarget(cstring name, Textures::TextureHandle handle)
    {
        if (auto* idx = ResourceIndex.find(name))
        {
            Resources[*idx].TextureHandle = handle;
            return {*idx, 0};
        }
        uint32_t idx        = static_cast<uint32_t>(Resources.size());
        auto&    res        = Resources.push_use({});
        res.Name            = name;
        res.Kind            = RGResourceKind::Attachment;
        res.External        = true;
        res.Transient       = false;
        res.TextureHandle   = handle;
        ResourceIndex[name] = idx;
        return {idx, 0};
    }

    RGResourceHandle RenderGraph::ImportBuffer(cstring name, const Core::Memory::BufferView* buffer)
    {
        return ResourceBuilder->ImportBuffer(name, buffer);
    }

    bool RenderGraph::UpdateImportedBuffer(cstring name, const Core::Memory::BufferView* buffer)
    {
        const auto* index = ResourceIndex.find(name);
        if (!buffer || !*buffer || !index || *index >= Resources.size() || Resources[*index].Kind != RGResourceKind::Buffer || !Resources[*index].External)
            return false;

        Resources[*index].Buffer = buffer;
        return true;
    }

    RGPass* RenderGraph::GetPass(cstring name)
    {
        if (auto* idx = PassIndex.find(name))
            return &Passes[*idx];
        return nullptr;
    }

    void RenderGraph::SetPassEnabled(cstring name, bool enabled)
    {
        if (auto* idx = PassIndex.find(name))
        {
            RGPass& pass = Passes[*idx];
            if (pass.Enabled != enabled)
            {
                pass.Enabled      = enabled;
                m_needs_recompile = true;
            }
        }
    }

    bool RenderGraph::ValidateDeclarations()
    {
        auto                          scratch = ZGetScratch(Device->Arena);
        RGDeclarationValidationResult result;
        const bool                    valid = ValidatePassDeclarations(scratch.Arena, Passes, Resources, &result);
        ZReleaseScratch(scratch);

        if (valid)
            return true;

        const cstring pass_name     = result.PassIndex < Passes.size() ? Passes[result.PassIndex].Name : "?";
        const cstring resource_name = result.ResourceIndex < Resources.size() ? Resources[result.ResourceIndex].Name : "?";
        switch (result.Error)
        {
            case RGDeclarationError::InvalidHandle:
                ZENGINE_CORE_ERROR("[RenderGraph] Pass '{}' declares an invalid resource handle", pass_name ? pass_name : "?")
                break;
            case RGDeclarationError::DuplicateProducer:
                ZENGINE_CORE_ERROR("[RenderGraph] Resource '{}' version {} has {} enabled producers", resource_name ? resource_name : "?", result.Version, result.WriterCount)
                break;
            case RGDeclarationError::MissingProducer:
                ZENGINE_CORE_ERROR("[RenderGraph] Resource '{}' version {} is read but has no enabled producer", resource_name ? resource_name : "?", result.Version)
                break;
            case RGDeclarationError::MissingImport:
                ZENGINE_CORE_ERROR("[RenderGraph] Resource '{}' version 0 is read but was not imported", resource_name ? resource_name : "?")
                break;
            default:
                break;
        }
        return valid;
    }

    void RenderGraph::BuildLifetimes()
    {
        for (auto& res : Resources)
        {
            res.FirstPassIndex = UINT32_MAX;
            res.LastPassIndex  = 0;
            res.CurrentState   = res.InitialState;
            res.RuntimeState   = res.InitialState;
        }

        // Indexed by position in SortedPassIndices (real execution order), not by raw
        // declaration order — BuildTopology() must run before this so transient-resource
        // lifetimes reflect when a pass actually executes, not where it was declared.
        for (uint32_t order_pos = 0; order_pos < SortedPassIndices.size(); ++order_pos)
        {
            const auto& pass = Passes[SortedPassIndices[order_pos]];
            for (const auto& w : pass.Writes)
            {
                if (!w.Handle.Valid())
                {
                    continue;
                }
                auto& res = Resources[w.Handle.Index];
                if (order_pos < res.FirstPassIndex)
                {
                    res.FirstPassIndex = order_pos;
                }
                if (order_pos > res.LastPassIndex)
                {
                    res.LastPassIndex = order_pos;
                }
            }
            for (const auto& r : pass.Reads)
            {
                if (!r.Handle.Valid())
                {
                    continue;
                }
                auto& res = Resources[r.Handle.Index];
                if (order_pos < res.FirstPassIndex)
                {
                    res.FirstPassIndex = order_pos;
                }
                if (order_pos > res.LastPassIndex)
                {
                    res.LastPassIndex = order_pos;
                }
            }
        }
    }

    void RenderGraph::AllocateTransientResources()
    {
        // Derive image creation usage before alias selection: a storage-capable
        // logical resource must never reuse an image created without STORAGE.
        for (uint32_t res_idx = 0; res_idx < Resources.size(); ++res_idx)
        {
            auto& res = Resources[res_idx];
            if (res.External || !res.Transient)
            {
                continue;
            }
            if (res.FirstPassIndex == UINT32_MAX)
            {
                continue;
            }

            for (const auto& pass : Passes)
            {
                auto derive_usage = [&](const RGPassResource& use) {
                    if (!use.Handle.Valid() || use.Handle.Index != res_idx)
                        return;
                    if (use.Access == RGAccess::ShaderRead || use.Access == RGAccess::ShaderReadWrite)
                        res.Spec.IsUsageSampled = true;
                    if (use.Access == RGAccess::ShaderReadWrite)
                        res.Spec.IsUsageStorage = true;
                    if (use.Access == RGAccess::TransferRead || use.Access == RGAccess::TransferWrite)
                        res.Spec.IsUsageTransfert = true;
                    if (use.Access == RGAccess::TransferRead)
                        res.Spec.IsUsageTransferSource = true;
                };
                for (const auto& read : pass.Reads)
                    derive_usage(read);
                for (const auto& write : pass.Writes)
                    derive_usage(write);
            }

            if (res.TextureHandle.Valid())
            {
                continue;
            }
            auto aliased = TransientPool.TryAlias(res.Spec, res.FirstPassIndex);
            if (aliased.Valid())
            {
                res.TextureHandle = aliased;
                TransientPool.MarkInUse(aliased, res.LastPassIndex);
            }
            else
            {
                res.TextureHandle = Device->CreateTexture(res.Spec);
                TransientPool.Register(res.TextureHandle, res.Spec, res.LastPassIndex);
            }
        }
    }

    void RenderGraph::BuildBarriers()
    {
        // Build one transition intent per state change in actual execution order.
        // Execute stamps those intents with per-frame image handles and old states.
        for (uint32_t order_pos = 0; order_pos < SortedPassIndices.size(); ++order_pos)
        {
            RGPass& pass = Passes[SortedPassIndices[order_pos]];
            pass.BarrierPlans.clear();
            pass.BufferBarrierPlans.clear();
            if (!pass.Enabled)
                continue;

            auto emit = [&](const RGPassResource& pr) {
                if (!pr.Handle.Valid() || pr.Access == RGAccess::None)
                    return;
                RGResource&           res = Resources[pr.Handle.Index];
                const RGResourceState dst = GetRGAccessState(pr.Access);

                if (res.Kind == RGResourceKind::Buffer)
                {
                    if (!NeedsBufferBarrier(res.CurrentState, dst))
                    {
                        MergeReadState(res.CurrentState, dst);
                        return;
                    }
                    pass.BufferBarrierPlans.push({pr.Handle.Index, dst});
                    res.CurrentState = dst;
                    return;
                }

                // First use of a graph-owned transient may reuse the memory of a
                // previous alias owner, so its old contents are explicitly discarded.
                const bool discard = res.Transient && !res.External && res.FirstPassIndex == order_pos;
                if (!discard && !NeedsImageBarrier(res.CurrentState, dst))
                {
                    MergeReadState(res.CurrentState, dst);
                    return;
                }

                pass.BarrierPlans.push({pr.Handle.Index, dst, discard});
                res.CurrentState = dst;
            };

            for (const auto& w : pass.Writes)
                emit(w);
            for (const auto& r : pass.Reads)
                emit(r);
        }
    }

    bool ValidatePassDeclarations(Core::Memory::ArenaAllocator* scratch_arena, ArrayView<RGPass> passes, ArrayView<RGResource> resources, RGDeclarationValidationResult* out_result)
    {
        if (out_result)
            *out_result = {};

        struct VersionUse
        {
            uint32_t ResourceIndex = UINT32_MAX;
            uint32_t Version       = 0;
            uint32_t WriterCount   = 0;
            uint32_t ReaderCount   = 0;
        };

        uint32_t access_count = 0;
        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            const auto& pass = passes[pass_index];
            if (pass.Enabled)
                access_count += static_cast<uint32_t>(pass.Reads.size() + pass.Writes.size());
        }

        Array<VersionUse> versions;
        versions.init(scratch_arena, access_count > 0 ? access_count : 1);
        auto fail = [&](RGDeclarationError error, uint32_t resource_index, uint32_t version, uint32_t pass_index, uint32_t writer_count = 0) {
            if (out_result)
                *out_result = {error, resource_index, version, pass_index, writer_count};
            return false;
        };
        auto find_or_add = [&](RGResourceHandle handle) -> VersionUse* {
            for (auto& entry : versions)
            {
                if (entry.ResourceIndex == handle.Index && entry.Version == handle.Version)
                    return &entry;
            }
            auto& entry         = versions.push_use({});
            entry.ResourceIndex = handle.Index;
            entry.Version       = handle.Version;
            return &entry;
        };

        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            const RGPass& pass = passes[pass_index];
            if (!pass.Enabled)
                continue;
            auto record = [&](const RGPassResource& use, bool is_write) {
                if (!use.Handle.Valid() || use.Handle.Index >= resources.size())
                    return fail(RGDeclarationError::InvalidHandle, use.Handle.Index, use.Handle.Version, pass_index);
                auto* entry = find_or_add(use.Handle);
                if (is_write)
                    ++entry->WriterCount;
                else
                    ++entry->ReaderCount;
                return true;
            };
            for (const auto& read : pass.Reads)
            {
                if (!record(read, false))
                    return false;
            }
            for (const auto& write : pass.Writes)
            {
                if (!record(write, true))
                    return false;
            }
        }

        for (const auto& entry : versions)
        {
            if (entry.WriterCount > 1)
                return fail(RGDeclarationError::DuplicateProducer, entry.ResourceIndex, entry.Version, UINT32_MAX, entry.WriterCount);
            if (entry.ReaderCount == 0)
                continue;
            if (entry.Version > 0 && entry.WriterCount != 1)
                return fail(RGDeclarationError::MissingProducer, entry.ResourceIndex, entry.Version, UINT32_MAX);
            const RGResource& resource     = resources[entry.ResourceIndex];
            const bool        valid_import = resource.Kind == RGResourceKind::Buffer ? resource.Buffer && resource.Buffer->Handle != VK_NULL_HANDLE : resource.TextureHandle.Valid();
            if (entry.Version == 0 && (!resource.External || !valid_import))
                return fail(RGDeclarationError::MissingImport, entry.ResourceIndex, entry.Version, UINT32_MAX);
        }
        return true;
    }

    bool BuildPassTopology(Core::Memory::ArenaAllocator* scratch_arena, ArrayView<RGPass> passes, Array<uint32_t>& out_order, uint32_t* out_cycle_pass_index)
    {
        out_order.clear();
        if (out_cycle_pass_index)
        {
            *out_cycle_pass_index = UINT32_MAX;
        }

        const uint32_t pass_count = static_cast<uint32_t>(passes.size());
        if (pass_count == 0)
        {
            return true;
        }

        uint32_t enabled_pass_count = 0;
        for (uint32_t i = 0; i < pass_count; ++i)
        {
            enabled_pass_count += passes[i].Enabled ? 1u : 0u;
        }
        if (enabled_pass_count == 0)
        {
            return true;
        }

        // Flatten every Read/Write into one event log — reads before writes within each
        // pass, passes in declaration order. Stable-sorting by resource and logical
        // version groups each producer with precisely the readers it satisfies.
        uint32_t total_events = 0;
        for (uint32_t i = 0; i < pass_count; ++i)
        {
            if (!passes[i].Enabled)
                continue;
            total_events += static_cast<uint32_t>(passes[i].Reads.size() + passes[i].Writes.size());
        }

        Array<RGEvent> events;
        events.init(scratch_arena, total_events > 0 ? total_events : 1);
        for (uint32_t i = 0; i < pass_count; ++i)
        {
            const auto& pass = passes[i];
            if (!pass.Enabled)
                continue;
            for (const auto& r : pass.Reads)
            {
                if (r.Handle.Valid())
                {
                    events.push({r.Handle.Index, r.Handle.Version, i, false});
                }
            }
            for (const auto& w : pass.Writes)
            {
                if (w.Handle.Valid())
                {
                    events.push({w.Handle.Index, w.Handle.Version, i, true});
                }
            }
        }
        std::stable_sort(events.begin(), events.end(), [](const RGEvent& a, const RGEvent& b) { return a.ResourceIndex != b.ResourceIndex ? a.ResourceIndex < b.ResourceIndex : a.Version < b.Version; });

        Array<uint32_t> indegree;
        indegree.init(scratch_arena, pass_count, pass_count);
        for (uint32_t i = 0; i < pass_count; ++i)
        {
            indegree[i] = 0;
        }

        Array<RGEdge> edges;
        edges.init(scratch_arena, events.size() * 2 + 1);

        auto add_edge = [&](uint32_t from, uint32_t to) {
            if (from == to) // drops same-pass self-edges (RMW passes, duplicate same-pass writes)
            {
                return;
            }
            edges.push({from, to});
            indegree[to]++;
        };

        // First bind every read to the single writer of its declared version.
        // The subsequent resource-wide walk retains WAW/WAR ordering for the
        // current in-place attachment implementation.
        uint32_t version_idx = 0;
        while (version_idx < events.size())
        {
            const uint32_t resource = events[version_idx].ResourceIndex;
            const uint32_t version  = events[version_idx].Version;
            const uint32_t start    = version_idx;
            while (version_idx < events.size() && events[version_idx].ResourceIndex == resource && events[version_idx].Version == version)
                ++version_idx;

            uint32_t writer  = UINT32_MAX;
            uint32_t writers = 0;
            for (uint32_t event = start; event < version_idx; ++event)
            {
                if (events[event].IsWrite)
                {
                    writer = events[event].PassIndex;
                    ++writers;
                }
            }
            if (writers == 1)
            {
                for (uint32_t event = start; event < version_idx; ++event)
                {
                    if (!events[event].IsWrite)
                        add_edge(writer, events[event].PassIndex);
                }
            }
        }

        // A later logical version currently shares the same physical image, so
        // preserve WAW/WAR hazards between version groups. Within a version the
        // explicit producer-to-reader edges above are authoritative.
        uint32_t resource_idx = 0;
        while (resource_idx < events.size())
        {
            const uint32_t  resource    = events[resource_idx].ResourceIndex;
            uint32_t        last_writer = UINT32_MAX;
            Array<uint32_t> prior_readers;
            prior_readers.init(scratch_arena, 8);

            while (resource_idx < events.size() && events[resource_idx].ResourceIndex == resource)
            {
                const uint32_t version = events[resource_idx].Version;
                const uint32_t start   = resource_idx;
                while (resource_idx < events.size() && events[resource_idx].ResourceIndex == resource && events[resource_idx].Version == version)
                    ++resource_idx;

                uint32_t writer  = UINT32_MAX;
                uint32_t writers = 0;
                for (uint32_t event = start; event < resource_idx; ++event)
                {
                    if (events[event].IsWrite)
                    {
                        writer = events[event].PassIndex;
                        ++writers;
                    }
                }

                if (writers == 1)
                {
                    if (last_writer != UINT32_MAX)
                        add_edge(last_writer, writer);
                    for (uint32_t reader : prior_readers)
                        add_edge(reader, writer);
                    prior_readers.clear();
                    last_writer = writer;
                }
                else if (writers > 1)
                {
                    // Invalid once callers fully adopt versioned writes, but keep
                    // the legacy declaration-order hazard behavior for existing
                    // callers until validation rejects this form.
                    uint32_t declared_writer = last_writer;
                    for (uint32_t event = start; event < resource_idx; ++event)
                    {
                        const uint32_t pass = events[event].PassIndex;
                        if (!events[event].IsWrite)
                        {
                            if (declared_writer != UINT32_MAX)
                                add_edge(declared_writer, pass);
                            continue;
                        }
                        if (declared_writer != UINT32_MAX)
                            add_edge(declared_writer, pass);
                        for (uint32_t reader : prior_readers)
                            add_edge(reader, pass);
                        prior_readers.clear();
                        declared_writer = pass;
                    }
                    last_writer = declared_writer;
                }

                for (uint32_t event = start; event < resource_idx; ++event)
                {
                    if (!events[event].IsWrite)
                        prior_readers.push(events[event].PassIndex);
                }
            }
        }

        // Kahn's algorithm. O(pass_count^2) scan-for-lowest-ready-index is fine at
        // frame-graph pass counts (this runs once at Compile(), not per frame) and keeps
        // today's declaration order for independent passes as a stable tie-break.
        Array<bool> emitted;
        emitted.init(scratch_arena, pass_count, pass_count);
        for (uint32_t i = 0; i < pass_count; ++i)
        {
            emitted[i] = false;
        }

        for (uint32_t emitted_count = 0; emitted_count < enabled_pass_count; ++emitted_count)
        {
            uint32_t best = UINT32_MAX;
            for (uint32_t i = 0; i < pass_count; ++i)
            {
                if (!passes[i].Enabled || emitted[i] || indegree[i] != 0)
                {
                    continue;
                }
                best = i;
                break;
            }
            if (best == UINT32_MAX)
            {
                if (out_cycle_pass_index)
                {
                    for (uint32_t i = 0; i < pass_count; ++i)
                    {
                        if (passes[i].Enabled && !emitted[i])
                        {
                            *out_cycle_pass_index = i;
                            break;
                        }
                    }
                }
                out_order.clear();
                return false;
            }
            emitted[best] = true;
            out_order.push(best);
            for (const auto& e : edges)
            {
                if (e.From == best && !emitted[e.To])
                {
                    indegree[e.To]--;
                }
            }
        }
        return true;
    }

    bool RenderGraph::BuildTopology()
    {
        auto     scratch   = ZGetScratch(Device->Arena);
        uint32_t cycle_idx = UINT32_MAX;
        if (!BuildPassTopology(scratch.Arena, Passes, SortedPassIndices, &cycle_idx))
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Cycle detected in enabled resource dependency graph at pass '{}' — compile rejected", cycle_idx < Passes.size() ? Passes[cycle_idx].Name : "?")
            SortedPassIndices.clear();
            ZReleaseScratch(scratch);
            return false;
        }
        ZReleaseScratch(scratch);
        return true;
    }

    void RenderGraph::BuildQueueSchedule()
    {
        for (auto& pass : Passes)
        {
            pass.RequestedQueue = Rendering::QueueType::GRAPHIC_QUEUE;
            if (pass.Handle)
            {
                switch (pass.Handle->Specification.Type)
                {
                    case Specifications::RenderPassType::COMPUTE:
                        pass.RequestedQueue = Rendering::QueueType::COMPUTE_QUEUE;
                        break;
                    case Specifications::RenderPassType::TRANSFER:
                        pass.RequestedQueue = Rendering::QueueType::TRANSFER_QUEUE;
                        break;
                    case Specifications::RenderPassType::GRAPHIC:
                    default:
                        break;
                }
            }
        }
        BuildQueueBatches(Passes, SortedPassIndices, Device->HasSeperateTransfertQueueFamily, Device->HasSeparateComputeQueueFamily, QueueBatches);
        auto scratch = ZGetScratch(Device->Arena);
        BuildQueueDependencies(scratch.Arena, Passes, SortedPassIndices, QueueBatches, static_cast<uint32_t>(Resources.size()), QueueDependencies);
        BuildQueueOwnershipTransfers(scratch.Arena, Passes, SortedPassIndices, QueueBatches, static_cast<uint32_t>(Resources.size()), QueueOwnershipTransfers);
        ZReleaseScratch(scratch);
    }

    void RenderGraph::AllocateFramebuffers()
    {
        for (uint32_t i = 0; i < Passes.size(); ++i)
        {
            RGPass& pass = Passes[i];
            if (!pass.Handle)
                continue;
            if (pass.Handle->Specification.Type == Specifications::RenderPassType::COMPUTE)
                continue;
            // Compile() is also used for structural changes such as enabling one
            // optional pass. Existing framebuffers remain compatible in that case;
            // recreating them would leak their VkFramebuffer handles.
            if (pass.Framebuffer && pass.Framebuffer->Handle)
                continue;

            // Stack-local view array avoids aliasing between the scratch arena and
            // the Device->Arena allocations inside the same loop body.
            VkImageView view_buf[16] = {};
            uint32_t    view_count   = 0;
            uint32_t    w            = 0;
            uint32_t    h            = 0;

            auto        push_view    = [&](Textures::TextureHandle handle) {
                if (view_count >= 16)
                    return;
                auto* tex = Device->GlobalTextures.Access(handle);
                if (!tex)
                    return;
                auto* img = Device->ImageBufferManager.Access(tex->BufferHandle);
                if (!img)
                    return;
                VkImageView view = img->GetImageViewHandle();
                if (view == VK_NULL_HANDLE)
                    return;
                view_buf[view_count++] = view;
                if (w == 0)
                {
                    w = tex->Width;
                    h = tex->Height;
                }
            };

            for (const auto& r : pass.Reads)
            {
                if (!r.Handle.Valid())
                    continue;
                if (r.Access != RGAccess::DepthRead && r.Access != RGAccess::DepthWrite)
                    continue;
                push_view(Resources[r.Handle.Index].TextureHandle);
            }
            for (const auto& wr : pass.Writes)
            {
                if (!wr.Handle.Valid())
                    continue;
                push_view(Resources[wr.Handle.Index].TextureHandle);
            }

            if (view_count == 0 || w == 0)
                continue;

            auto* gp             = static_cast<RenderPasses::GraphicPass*>(pass.Handle);
            gp->RenderAreaWidth  = w;
            gp->RenderAreaHeight = h;

            VkRenderPass  rp     = gp->GetAttachment()->GetHandle();
            VkFramebuffer vk_fb  = Device->CreateFramebuffer(Core::Containers::ArrayView<VkImageView>{view_buf, view_count}, rp, w, h);

            if (vk_fb == VK_NULL_HANDLE)
            {
                ZENGINE_CORE_ERROR("[RenderGraph] AllocateFramebuffers: CreateFramebuffer returned null for pass '{}' (views={} rp={} w={} h={})", pass.Name ? pass.Name : "?", view_count, (void*) rp, w, h)
                continue;
            }

            if (!pass.Framebuffer)
                pass.Framebuffer = ZPushStructCtorArgs(Device->Arena, Buffers::FramebufferVNext, Device);
            pass.Framebuffer->Reset(vk_fb, w, h);
        }
    }

    void RenderGraphResourceBuilder::Initialize(RenderGraph* graph)
    {
        Graph = graph;
    }

    static uint32_t GetOrCreateResource(RenderGraph* graph, cstring name, RGResourceKind kind, bool external, const Specifications::TextureSpecification& spec)
    {
        if (auto* idx = graph->ResourceIndex.find(name))
            return *idx;

        uint32_t idx               = static_cast<uint32_t>(graph->Resources.size());
        auto&    res               = graph->Resources.push_use({});
        res.Name                   = name;
        res.Kind                   = kind;
        res.External               = external;
        res.Transient              = !external;
        res.Spec                   = spec;
        graph->ResourceIndex[name] = idx;
        return idx;
    }

    static RGResourceHandle RecordAccess(RenderGraph* graph, uint32_t pass_idx, uint32_t res_idx, RGAccess access, cstring binding_key, bool is_write, uint32_t read_version = UINT32_MAX)
    {
        if (pass_idx == UINT32_MAX)
            return {};
        RGPass&        pass = graph->Passes[pass_idx];
        RGPassResource pr;
        pr.Handle     = {res_idx, is_write ? ++graph->Resources[res_idx].LatestVersion : (read_version == UINT32_MAX ? graph->Resources[res_idx].LatestVersion : read_version)};
        pr.Access     = access;
        pr.BindingKey = binding_key;
        if (is_write)
            pass.Writes.push(pr);
        else
            pass.Reads.push(pr);
        return pr.Handle;
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteColorAttachment(cstring name, const Specifications::TextureSpecification& spec)
    {
        uint32_t idx = GetOrCreateResource(Graph, name, RGResourceKind::Attachment, false, spec);
        auto&    res = Graph->Resources[idx];
        if (res.External && !res.TextureHandle.Valid())
        {
            res.Kind      = RGResourceKind::Attachment;
            res.External  = false;
            res.Transient = true;
            res.Spec      = spec;
        }
        auto handle                                     = RecordAccess(Graph, CurrentPass, idx, RGAccess::ColorWrite, nullptr, true);
        Graph->Passes[CurrentPass].Writes.back().LoadOp = spec.LoadOp;
        return handle;
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteDepthAttachment(cstring name, const Specifications::TextureSpecification& spec)
    {
        uint32_t idx = GetOrCreateResource(Graph, name, RGResourceKind::Attachment, false, spec);
        auto&    res = Graph->Resources[idx];
        if (res.External && !res.TextureHandle.Valid())
        {
            res.Kind      = RGResourceKind::Attachment;
            res.External  = false;
            res.Transient = true;
            res.Spec      = spec;
        }
        auto handle                                     = RecordAccess(Graph, CurrentPass, idx, RGAccess::DepthWrite, nullptr, true);
        Graph->Passes[CurrentPass].Writes.back().LoadOp = spec.LoadOp;
        return handle;
    }

    RGResourceHandle RenderGraphResourceBuilder::UpdateColorAttachment(cstring name, const Specifications::TextureSpecification& spec)
    {
        ZENGINE_VALIDATE_ASSERT(spec.LoadOp == Specifications::LoadOperation::LOAD, "UpdateColorAttachment requires LoadOperation::LOAD")
        return WriteColorAttachment(name, spec);
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadTexture(cstring name, cstring binding_key)
    {
        Specifications::TextureSpecification empty_spec = {};
        uint32_t                             idx        = GetOrCreateResource(Graph, name, RGResourceKind::Texture, true, empty_spec);
        return ReadTexture({idx, Graph->Resources[idx].LatestVersion}, binding_key);
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadTexture(RGResourceHandle handle, cstring binding_key)
    {
        if (!handle.Valid() || handle.Index >= Graph->Resources.size())
            return {};
        return RecordAccess(Graph, CurrentPass, handle.Index, RGAccess::ShaderRead, binding_key, false, handle.Version);
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadDepth(cstring name)
    {
        Specifications::TextureSpecification empty_spec = {};
        uint32_t                             idx        = GetOrCreateResource(Graph, name, RGResourceKind::Attachment, true, empty_spec);
        return ReadDepth({idx, Graph->Resources[idx].LatestVersion});
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadDepth(RGResourceHandle handle)
    {
        if (!handle.Valid() || handle.Index >= Graph->Resources.size())
            return {};
        return RecordAccess(Graph, CurrentPass, handle.Index, RGAccess::DepthRead, nullptr, false, handle.Version);
    }

    RGResourceHandle RenderGraphResourceBuilder::ImportBuffer(cstring name, const Core::Memory::BufferView* buffer)
    {
        Specifications::TextureSpecification empty_spec = {};
        uint32_t                             idx        = GetOrCreateResource(Graph, name, RGResourceKind::Buffer, true, empty_spec);
        RGResource&                          resource   = Graph->Resources[idx];
        resource.Kind                                   = RGResourceKind::Buffer;
        resource.External                               = true;
        resource.Transient                              = false;
        resource.Buffer                                 = buffer;
        // Scene buffers are host-visible and uploaded before Execute(). The first
        // graph use must make those host writes visible to GPU commands.
        resource.InitialState                           = {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
        resource.CurrentState                           = resource.InitialState;
        resource.RuntimeState                           = resource.InitialState;
        return {idx, 0};
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadBuffer(cstring name, cstring binding_key)
    {
        if (auto* idx = Graph->ResourceIndex.find(name))
            return ReadBuffer({*idx, Graph->Resources[*idx].LatestVersion}, binding_key);
        return {};
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadBuffer(RGResourceHandle handle, cstring binding_key)
    {
        if (!handle.Valid() || handle.Index >= Graph->Resources.size() || Graph->Resources[handle.Index].Kind != RGResourceKind::Buffer)
            return {};
        return RecordAccess(Graph, CurrentPass, handle.Index, RGAccess::BufferRead, binding_key, false, handle.Version);
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadWriteBuffer(cstring name, cstring binding_key)
    {
        if (auto* idx = Graph->ResourceIndex.find(name); idx && Graph->Resources[*idx].Kind == RGResourceKind::Buffer)
            return RecordAccess(Graph, CurrentPass, *idx, RGAccess::BufferReadWrite, binding_key, true);
        return {};
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadIndirectBuffer(cstring name)
    {
        if (auto* idx = Graph->ResourceIndex.find(name); idx && Graph->Resources[*idx].Kind == RGResourceKind::Buffer)
            return RecordAccess(Graph, CurrentPass, *idx, RGAccess::IndirectRead, nullptr, false);
        return {};
    }

    RGResourceHandle RenderGraphResourceBuilder::ImportRenderTarget(cstring name, Textures::TextureHandle handle)
    {
        Specifications::TextureSpecification empty_spec = {};
        uint32_t                             idx        = GetOrCreateResource(Graph, name, RGResourceKind::Attachment, true, empty_spec);
        Graph->Resources[idx].TextureHandle             = handle;
        Graph->Resources[idx].External                  = true;
        Graph->Resources[idx].Transient                 = false;
        return {idx, 0};
    }

    RGResourceHandle RenderGraphResourceBuilder::AttachRenderTarget(cstring name, const Textures::TextureHandle& texture)
    {
        return ImportRenderTarget(name, texture);
    }

    void RenderGraphResourceInspector::Initialize(RenderGraph* graph)
    {
        Graph = graph;
    }

    Textures::TextureHandle RenderGraphResourceInspector::GetTextureHandle(RGResourceHandle handle) const
    {
        if (!handle.Valid() || handle.Index >= Graph->Resources.size())
            return {};
        return Graph->Resources[handle.Index].TextureHandle;
    }

    const Core::Memory::BufferView* RenderGraphResourceInspector::GetBuffer(RGResourceHandle handle) const
    {
        if (!handle.Valid() || handle.Index >= Graph->Resources.size())
            return nullptr;
        return Graph->Resources[handle.Index].Kind == RGResourceKind::Buffer ? Graph->Resources[handle.Index].Buffer : nullptr;
    }

    Textures::TextureHandle RenderGraphResourceInspector::GetRenderTarget(cstring name) const
    {
        if (auto* idx = Graph->ResourceIndex.find(name))
            return Graph->Resources[*idx].TextureHandle;
        return {};
    }

    Textures::TextureHandle RenderGraphResourceInspector::GetTexture(cstring name) const
    {
        return GetRenderTarget(name);
    }

} // namespace ZEngine::Rendering::Renderers
