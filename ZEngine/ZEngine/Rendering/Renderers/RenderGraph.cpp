#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers
{
    // kAccessTable — stage + access + layout for every RGAccess value.
    static constexpr struct
    {
        VkPipelineStageFlags Stage;
        VkAccessFlags        Access;
        VkImageLayout        Layout;
    } kAccessTable[static_cast<int>(RGAccess::Count_)] = {
        // None
        {                                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,                                                                                          0,                        VK_IMAGE_LAYOUT_UNDEFINED},
        // ColorWrite
        {                                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,                                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        // DepthWrite
        {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        // DepthRead — stays in DEPTH_STENCIL_ATTACHMENT_OPTIMAL; depthWrite=false in pipeline
        {                                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,                                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        // ShaderRead
        {                                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,                                                                  VK_ACCESS_SHADER_READ_BIT,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        // ShaderReadWrite
        {                                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,                                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,                          VK_IMAGE_LAYOUT_GENERAL},
        // TransferRead
        {                                                        VK_PIPELINE_STAGE_TRANSFER_BIT,                                                                VK_ACCESS_TRANSFER_READ_BIT,             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
        // TransferWrite
        {                                                        VK_PIPELINE_STAGE_TRANSFER_BIT,                                                               VK_ACCESS_TRANSFER_WRITE_BIT,             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
        // Present
        {                                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,                                                                                          0,                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
    };

    static VkImage GetVkImage(Hardwares::VulkanDevice* device, Textures::TextureHandle handle)
    {
        if (!handle.Valid())
            return VK_NULL_HANDLE;
        auto* tex = device->GlobalTextures.Access(handle);
        if (!tex)
            return VK_NULL_HANDLE;
        auto* img_buf = device->Image2DBufferManager.Access(tex->BufferHandle);
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
            if (s.Format != spec.Format || s.Width != spec.Width || s.Height != spec.Height || s.LayerCount != spec.LayerCount)
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
        ResourceIndex.init(Device->Arena, 64);
        PassIndex.init(Device->Arena, 32);
        TransientPool.Initialize(Device->Arena);

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
        p.ImageBarriers.init(Device->Arena, 8);

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
        BuildLifetimes();
        AllocateTransientResources();
        BuildBarriers();
        BuildTopology();

        for (uint32_t i = 0; i < SortedPassIndices.size(); ++i)
        {
            uint32_t pi   = SortedPassIndices[i];
            RGPass&  pass = Passes[pi];
            if (!pass.Enabled || !pass.Callback)
                continue;

            // Pre-populate the builder with resolved resource handles so the pass's
            // Compile() can call SetPipelineName()...Detach() and get a spec that
            // already has the correct input attachments and render targets.
            // Only DepthRead reads become VkRenderPass input attachments.
            // ShaderRead reads are bound via SetTexture() in the pass's Compile().
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
                    RenderPassBuilder->UseRenderTarget(res.TextureHandle);
            }

            pass.Callback->Compile(Device, SceneData, RenderPassBuilder, ResourceInspector, &pass.Handle);
        }

        AllocateFramebuffers();
    }

    void RenderGraph::Execute(Hardwares::CommandBufferPtr const cb)
    {
        cb->ClearColor(0.11f, 0.11f, 0.11f, 1.0f);
        cb->ClearDepth(1.0f, 0);

        auto scratch = ZGetScratch(Device->Arena);

        for (uint32_t i = 0; i < SortedPassIndices.size(); ++i)
        {
            RGPass& pass = Passes[SortedPassIndices[i]];
            if (!pass.Enabled)
                continue;

            // Build barriers for this pass fresh each frame so layout transitions
            // correctly track the actual per-frame resource states.
            Core::Containers::Array<VkImageMemoryBarrier> barriers;
            barriers.init(scratch.Arena, 8);
            VkPipelineStageFlags src_stage    = 0;
            VkPipelineStageFlags dst_stage    = 0;

            auto                 emit_barrier = [&](const RGPassResource& pr) {
                if (!pr.Handle.Valid() || pr.Access == RGAccess::None)
                    return;
                RGResource& res = Resources[pr.Handle.Index];
                const auto& dst = kAccessTable[static_cast<int>(pr.Access)];
                if (res.RuntimeState.Layout == dst.Layout && res.RuntimeState.Access == dst.Access)
                    return;

                VkImageMemoryBarrier b = {};
                b.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout            = res.RuntimeState.Layout;
                b.newLayout            = dst.Layout;
                b.srcAccessMask        = res.RuntimeState.Access;
                b.dstAccessMask        = dst.Access;
                b.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
                b.image                = GetVkImage(Device, res.TextureHandle);
                b.subresourceRange     = FullSubresourceRange(res, Device);
                if (b.image == VK_NULL_HANDLE)
                    return;

                barriers.push(b);
                src_stage        |= res.RuntimeState.Stage;
                dst_stage        |= dst.Stage;
                res.RuntimeState  = {dst.Stage, dst.Access, dst.Layout};
            };

            for (const auto& w : pass.Writes)
                emit_barrier(w);
            for (const auto& r : pass.Reads)
                emit_barrier(r);

            if (!barriers.empty())
            {
                vkCmdPipelineBarrier(cb->GetHandle(), src_stage ? src_stage : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dst_stage ? dst_stage : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
            }

            if (!pass.Framebuffer || !pass.Framebuffer->Handle)
                continue;
            pass.Callback->Execute(Device, ResourceInspector, SceneData, pass.Handle, pass.Framebuffer, cb);
        }

        ZReleaseScratch(scratch);
    }

    void RenderGraph::Resize(uint32_t width, uint32_t height)
    {
        // Phase 1 — collect old Vulkan handles that need to be freed.
        // Do NOT call any vkDestroy* yet — new resources must be created first
        // so the driver cannot recycle these handles for new allocations.
        TransientPool.Clear();
        uint64_t                  timeline      = Device->SwapchainPtr->RenderTimelineNextValue;

        // Stack-local scratch for old handles (max 16 passes, max 32 transients).
        VkFramebuffer             old_fbs[16]   = {};
        uint32_t                  old_fb_count  = 0;
        Core::Memory::BufferImage old_imgs[32]  = {};
        uint32_t                  old_img_count = 0;

        for (auto& pass : Passes)
        {
            if (pass.Framebuffer && pass.Framebuffer->Handle)
            {
                if (old_fb_count < 16)
                    old_fbs[old_fb_count++] = pass.Framebuffer->Handle;
                pass.Framebuffer->Handle = VK_NULL_HANDLE;
            }
        }

        // Swap the underlying Image2DBuffer in-place for each transient resource.
        // TextureHandle and Image2DBufferManager slot are REUSED — no new slots,
        // no slot exhaustion, handles stay stable so the editor's cached ImTextureID
        // remains valid.  Old VkImage/VkImageView data is saved for DeferFree.
        for (auto& res : Resources)
        {
            if (res.External || !res.Transient)
                continue;
            res.Spec.Width   = width;
            res.Spec.Height  = height;
            res.CurrentState = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
            res.RuntimeState = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
            if (!res.TextureHandle.Valid())
                continue;
            auto* tex = Device->GlobalTextures.Access(res.TextureHandle);
            if (!tex)
                continue;
            auto* img = Device->Image2DBufferManager.Access(tex->BufferHandle);
            if (!img)
                continue;
            // Save old VkImage/VkImageView for deferred destruction.
            if (old_img_count < 32)
                old_imgs[old_img_count++] = img->GetBuffer();
            // Reconstruct Image2DBuffer in the same slot with new dimensions.
            // This creates new VkImage/VkImageView while old ones are still alive.
            img->Specification.Width  = width;
            img->Specification.Height = height;
            img->Construct(Device);
            // Update Texture metadata.
            tex->Width      = width;
            tex->Height     = height;
            tex->BufferSize = width * height * res.Spec.BytePerPixel * res.Spec.LayerCount;
        }

        // Phase 2 — rebuild framebuffers and re-bind descriptors with new Image2DBuffers.
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
            if (!pass.Handle)
                continue;
            for (const auto& r : pass.Reads)
            {
                if (!r.Handle.Valid() || !r.BindingKey)
                    continue;
                const auto& res = Resources[r.Handle.Index];
                if (res.TextureHandle.Valid())
                    pass.Handle->SetTexture(r.BindingKey, res.TextureHandle);
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
        for (uint32_t i = 0; i < old_img_count; ++i)
        {
            Hardwares::DeferredFreeEntry e;
            e.EntryKind     = Hardwares::DeferredFreeEntry::Kind::Image;
            e.TimelineValue = timeline;
            e.Data.Image    = old_imgs[i];
            Device->DeferFree(e);
        }
    }

    void RenderGraph::Dispose()
    {
        for (auto& res : Resources)
        {
            if (res.External || !res.Transient || !res.TextureHandle.Valid())
                continue;
            auto* tex = Device->GlobalTextures.Access(res.TextureHandle);
            if (!tex)
                continue;
            auto* img = Device->Image2DBufferManager.Access(tex->BufferHandle);
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

    RGPass* RenderGraph::GetPass(cstring name)
    {
        if (auto* idx = PassIndex.find(name))
            return &Passes[*idx];
        return nullptr;
    }

    void RenderGraph::SetPassEnabled(cstring name, bool enabled)
    {
        if (auto* idx = PassIndex.find(name))
            Passes[*idx].Enabled = enabled;
    }

    void RenderGraph::BuildLifetimes()
    {
        for (auto& res : Resources)
        {
            res.FirstPassIndex = UINT32_MAX;
            res.LastPassIndex  = 0;
            res.CurrentState   = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
            res.RuntimeState   = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
        }

        for (uint32_t i = 0; i < Passes.size(); ++i)
        {
            const auto& pass = Passes[i];
            for (const auto& w : pass.Writes)
            {
                if (!w.Handle.Valid())
                    continue;
                auto& res = Resources[w.Handle.Index];
                if (i < res.FirstPassIndex)
                    res.FirstPassIndex = i;
                if (i > res.LastPassIndex)
                    res.LastPassIndex = i;
            }
            for (const auto& r : pass.Reads)
            {
                if (!r.Handle.Valid())
                    continue;
                auto& res = Resources[r.Handle.Index];
                if (i < res.FirstPassIndex)
                    res.FirstPassIndex = i;
                if (i > res.LastPassIndex)
                    res.LastPassIndex = i;
            }
        }
    }

    void RenderGraph::AllocateTransientResources()
    {
        for (auto& res : Resources)
        {
            if (res.External || !res.Transient)
                continue;
            if (res.FirstPassIndex == UINT32_MAX)
                continue;
            if (res.TextureHandle.Valid())
                continue;
            auto aliased = TransientPool.TryAlias(res.Spec, res.FirstPassIndex);
            if (aliased.Valid())
            {
                res.TextureHandle = aliased;
                TransientPool.MarkInUse(aliased, res.LastPassIndex);
            }
            else
            {
                // Derive usage from access declarations and set appropriate spec flags.
                for (const auto& pass : Passes)
                {
                    for (const auto& w : pass.Writes)
                        if (w.Handle.Valid() && w.Handle.Index == res.FirstPassIndex && w.Access == RGAccess::ShaderReadWrite)
                            res.Spec.IsUsageStorage = true;
                }
                res.TextureHandle = Device->CreateTexture(res.Spec);
                TransientPool.Register(res.TextureHandle, res.Spec, res.LastPassIndex);
            }
        }
    }

    void RenderGraph::BuildBarriers()
    {
        for (uint32_t i = 0; i < Passes.size(); ++i)
        {
            RGPass& pass = Passes[i];
            pass.ImageBarriers.clear();
            pass.BarrierSrcStage = 0;
            pass.BarrierDstStage = 0;

            auto emit            = [&](const RGPassResource& pr) {
                if (!pr.Handle.Valid() || pr.Access == RGAccess::None)
                    return;
                RGResource& res = Resources[pr.Handle.Index];
                const auto& dst = kAccessTable[static_cast<int>(pr.Access)];

                if (res.CurrentState.Layout == dst.Layout && res.CurrentState.Access == dst.Access)
                    return;

                VkImageMemoryBarrier b = {};
                b.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                // Use UNDEFINED only for transient resources on first use (discard previous alias contents).
                // External/persistent resources must specify the actual old layout so the barrier
                // declares the correct dependency across frames.
                bool discard           = (res.Transient && !res.External && res.FirstPassIndex == i);
                b.oldLayout            = discard ? VK_IMAGE_LAYOUT_UNDEFINED : res.CurrentState.Layout;
                b.newLayout            = dst.Layout;
                b.srcAccessMask        = res.CurrentState.Access;
                b.dstAccessMask        = dst.Access;
                b.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
                b.image                = GetVkImage(Device, res.TextureHandle);
                b.subresourceRange     = FullSubresourceRange(res, Device);

                if (b.image == VK_NULL_HANDLE)
                    return;

                pass.ImageBarriers.push(b);
                pass.BarrierSrcStage |= res.CurrentState.Stage;
                pass.BarrierDstStage |= dst.Stage;
                res.CurrentState      = {dst.Stage, dst.Access, dst.Layout};
            };

            for (const auto& w : pass.Writes)
                emit(w);
            for (const auto& r : pass.Reads)
                emit(r);
        }
    }

    void RenderGraph::BuildTopology()
    {
        // Simple insertion-order execution for now — passes execute in the order they were added.
        // A full DFS topological sort can replace this once producer/consumer edges are tracked.
        SortedPassIndices.clear();
        for (uint32_t i = 0; i < Passes.size(); ++i)
            SortedPassIndices.push(i);
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
                auto* img = Device->Image2DBufferManager.Access(tex->BufferHandle);
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

            pass.Handle->RenderAreaWidth  = w;
            pass.Handle->RenderAreaHeight = h;

            VkRenderPass  rp              = pass.Handle->GetAttachment()->GetHandle();
            VkFramebuffer vk_fb           = Device->CreateFramebuffer(Core::Containers::ArrayView<VkImageView>{view_buf, view_count}, rp, w, h);

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

    static void RecordAccess(RenderGraph* graph, uint32_t pass_idx, uint32_t res_idx, RGAccess access, cstring binding_key, bool is_write)
    {
        if (pass_idx == UINT32_MAX)
            return;
        RGPass&        pass = graph->Passes[pass_idx];
        RGPassResource pr;
        pr.Handle     = {res_idx, 0};
        pr.Access     = access;
        pr.BindingKey = binding_key;
        if (is_write)
            pass.Writes.push(pr);
        else
            pass.Reads.push(pr);
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteColorAttachment(cstring name, const Specifications::TextureSpecification& spec)
    {
        uint32_t idx = GetOrCreateResource(Graph, name, RGResourceKind::Attachment, false, spec);
        RecordAccess(Graph, CurrentPass, idx, RGAccess::ColorWrite, nullptr, true);
        return {idx, 0};
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteDepthAttachment(cstring name, const Specifications::TextureSpecification& spec)
    {
        uint32_t idx = GetOrCreateResource(Graph, name, RGResourceKind::Attachment, false, spec);
        RecordAccess(Graph, CurrentPass, idx, RGAccess::DepthWrite, nullptr, true);
        return {idx, 0};
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadTexture(cstring name, cstring binding_key)
    {
        Specifications::TextureSpecification empty_spec = {};
        uint32_t                             idx        = GetOrCreateResource(Graph, name, RGResourceKind::Texture, true, empty_spec);
        RecordAccess(Graph, CurrentPass, idx, RGAccess::ShaderRead, binding_key, false);
        return {idx, 0};
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadDepth(cstring name)
    {
        Specifications::TextureSpecification empty_spec = {};
        uint32_t                             idx        = GetOrCreateResource(Graph, name, RGResourceKind::Attachment, true, empty_spec);
        RecordAccess(Graph, CurrentPass, idx, RGAccess::DepthRead, nullptr, false);
        return {idx, 0};
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
