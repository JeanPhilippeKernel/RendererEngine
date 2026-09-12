#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Renderers/RenderGraphTopology.h>
#include <algorithm>
#include <cstdio>
#include <latch>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers
{
    float RGTimestampDurationMilliseconds(uint64_t begin_timestamp, uint64_t end_timestamp, uint32_t valid_bits, float timestamp_period)
    {
        if (valid_bits == 0 || timestamp_period <= 0.0f)
            return 0.0f;

        const uint64_t timestamp_mask = valid_bits >= 64 ? UINT64_MAX : (UINT64_C(1) << valid_bits) - 1;
        const uint64_t elapsed_ticks  = (end_timestamp - begin_timestamp) & timestamp_mask;
        return static_cast<float>((static_cast<double>(elapsed_ticks) * static_cast<double>(timestamp_period)) / 1000000.0);
    }

    namespace
    {
        void InitializeResourceVersions(RGResource& resource, Core::Memory::ArenaAllocator* arena)
        {
            if (resource.Versions.data())
                resource.Versions.clear();
            else
                resource.Versions.init(arena, 4);
            resource.Versions.push({});

            if (resource.CompileSubresourceStates.data())
                resource.CompileSubresourceStates.clear();
            else
                resource.CompileSubresourceStates.init(arena, 8);
            if (resource.RuntimeSubresourceStates.data())
                resource.RuntimeSubresourceStates.clear();
            else
                resource.RuntimeSubresourceStates.init(arena, 8);
        }

        uint32_t FindCurrentFrameContextSlot(const Hardwares::DeviceSwapchain* swapchain)
        {
            if (!swapchain || !swapchain->CurrentFrame)
                return UINT32_MAX;

            for (uint32_t index = 0; index < swapchain->FrameContexts.size(); ++index)
            {
                if (&swapchain->FrameContexts[index] == swapchain->CurrentFrame)
                    return index;
            }
            return UINT32_MAX;
        }

        VkQueryPool GetCurrentQueryPool(const RenderGraph* graph, RGQueryHandle handle)
        {
            if (!graph || !graph->Device || !handle.Valid() || handle.Index >= graph->QueryPools.size())
                return VK_NULL_HANDLE;

            const uint32_t frame_slot = FindCurrentFrameContextSlot(graph->Device->SwapchainPtr);
            const auto&    storage    = graph->QueryPools[handle.Index];
            return frame_slot < storage.FramePools.size() ? storage.FramePools[frame_slot] : VK_NULL_HANDLE;
        }

        RGResourceVersion* FindResourceVersion(RGResource& resource, uint32_t version)
        {
            return version < resource.Versions.size() ? &resource.Versions[version] : nullptr;
        }

        bool IsGraphicPass(const RGPass& pass)
        {
            return pass.Handle && pass.Handle->Specification.Type == Specifications::RenderPassType::GRAPHIC;
        }

        VkFramebuffer ResolveGraphFramebuffer(const Hardwares::VulkanDevice* device, const RGPass& pass)
        {
            if (!pass.Handle)
                return VK_NULL_HANDLE;
            if (!pass.Handle->Specification.SwapchainAsRenderTarget)
                return pass.Framebuffer ? pass.Framebuffer->Handle : VK_NULL_HANDLE;

            const auto* swapchain = device ? device->SwapchainPtr : nullptr;
            if (!swapchain || !swapchain->CurrentFrame)
                return VK_NULL_HANDLE;
            const uint32_t image_index = swapchain->CurrentFrame->ImageIndex;
            return image_index < swapchain->SwapchainFramebuffers.size() ? swapchain->SwapchainFramebuffers[image_index] : VK_NULL_HANDLE;
        }

        bool CanRecordSecondary(const Hardwares::VulkanDevice* device, const RGPass& pass)
        {
            // Occlusion queries cannot be emitted by the graph's secondary path:
            // that path does not enable occlusion-query inheritance. Keep query
            // writers on the graphics primary where the graph also resets them.
            if (!pass.IsActive() || !pass.Callback || !pass.RequiresRenderPass || !pass.Handle || !pass.QueryWrites.empty())
                return false;

            if (IsGraphicPass(pass))
            {
                if (!pass.Callback->SupportsSecondaryRecording())
                    return false;
                return device->PhysicalDeviceSupportDynamicRendering || ResolveGraphFramebuffer(device, pass) != VK_NULL_HANDLE;
            }
            return pass.Handle->Specification.Type == Specifications::RenderPassType::COMPUTE;
        }

        void EnsurePassPipelineOnRenderThread(RGPass& pass)
        {
            if (!pass.Handle)
                return;

            if (pass.Handle->Specification.Type == Specifications::RenderPassType::GRAPHIC)
            {
                auto* graphics = static_cast<RenderPasses::GraphicPass*>(pass.Handle);
                if (graphics->Pipeline)
                    graphics->Pipeline->EnsureCurrent();
                return;
            }
            if (pass.Handle->Specification.Type == Specifications::RenderPassType::COMPUTE)
            {
                auto* compute = static_cast<RenderPasses::ComputePass*>(pass.Handle);
                if (compute->Pipeline)
                    compute->Pipeline->EnsureCurrent();
            }
        }

        struct RenderGraphSecondaryRecordTask
        {
            RenderGraph*           Graph      = nullptr;
            const RGTopologyLevel* Level      = nullptr;
            uint32_t               Worker     = UINT32_MAX;
            uint8_t                FrameIndex = 0;
            std::latch*            Completion = nullptr;
        };

        void RecordGraphSecondary(RenderGraph* graph, RGPass& pass, uint8_t frame_index)
        {
            if (!graph || !CanRecordSecondary(graph->Device, pass) || pass.SecondaryWorker == UINT32_MAX)
                return;

            auto* secondary = graph->Device->CommandBufferMgr->AcquireWorkerSecondary(pass.Queue, frame_index, pass.SecondaryWorker, pass.SecondaryOrdinal);
            pass.Secondary  = secondary;

            if (IsGraphicPass(pass))
            {
                secondary->BeginSecondary(static_cast<RenderPasses::GraphicPass*>(pass.Handle), ResolveGraphFramebuffer(graph->Device, pass));
                pass.SecondaryRecorded = pass.Callback->RecordDraw(graph->Device, graph->ResourceInspector, graph->SceneData, pass.Handle, pass.Framebuffer, secondary);
            }
            else
            {
                secondary->BeginSecondaryCompute();
                pass.Callback->Execute(graph->Device, graph->ResourceInspector, graph->SceneData, pass.Handle, pass.Framebuffer, secondary);
                pass.SecondaryRecorded = true;
            }
            secondary->End();
        }

        void RecordGraphSecondaryWorkerBatch(void* context)
        {
            auto& task = *static_cast<RenderGraphSecondaryRecordTask*>(context);
            if (task.Graph && task.Level)
            {
                for (uint32_t pass_index : task.Level->PassIndices)
                {
                    if (pass_index < task.Graph->Passes.size())
                    {
                        RGPass& pass = task.Graph->Passes[pass_index];
                        if (pass.SecondaryWorker == task.Worker)
                            RecordGraphSecondary(task.Graph, pass, task.FrameIndex);
                    }
                }
            }
            if (task.Completion)
                task.Completion->count_down();
        }

        void AppendUnsigned(Core::Containers::String& output, uint64_t value)
        {
            char text[32] = {};
            std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
            output.append(text);
        }

        void AppendFloat(Core::Containers::String& output, float value)
        {
            char text[32] = {};
            std::snprintf(text, sizeof(text), "%.3f", value);
            output.append(text);
        }

        void AppendEscaped(Core::Containers::String& output, cstring value)
        {
            if (!value)
                return;
            for (const char* current = value; *current; ++current)
            {
                switch (*current)
                {
                    case '\\':
                        output.append("\\\\");
                        break;
                    case '\"':
                        output.append("\\\"");
                        break;
                    case '\n':
                        output.append("\\n");
                        break;
                    case '\r':
                        output.append("\\r");
                        break;
                    case '\t':
                        output.append("\\t");
                        break;
                    default:
                        output.append(*current);
                        break;
                }
            }
        }

        cstring QueueName(Rendering::QueueType queue)
        {
            switch (queue)
            {
                case Rendering::QueueType::COMPUTE_QUEUE:
                    return "compute";
                case Rendering::QueueType::TRANSFER_QUEUE:
                    return "transfer";
                case Rendering::QueueType::GRAPHIC_QUEUE:
                default:
                    return "graphics";
            }
        }

        cstring QueueColor(Rendering::QueueType queue)
        {
            switch (queue)
            {
                case Rendering::QueueType::COMPUTE_QUEUE:
                    return "#c7e9c0";
                case Rendering::QueueType::TRANSFER_QUEUE:
                    return "#fdd49e";
                case Rendering::QueueType::GRAPHIC_QUEUE:
                default:
                    return "#c6dbef";
            }
        }

        cstring QueueDebugLabel(Rendering::QueueType queue)
        {
            switch (queue)
            {
                case Rendering::QueueType::COMPUTE_QUEUE:
                    return "RenderGraph / compute queue";
                case Rendering::QueueType::TRANSFER_QUEUE:
                    return "RenderGraph / transfer queue";
                case Rendering::QueueType::GRAPHIC_QUEUE:
                default:
                    return "RenderGraph / graphics queue";
            }
        }

        cstring ResourceKindName(RGResourceKind kind)
        {
            switch (kind)
            {
                case RGResourceKind::Attachment:
                    return "attachment";
                case RGResourceKind::Texture:
                    return "texture";
                case RGResourceKind::Buffer:
                    return "buffer";
                case RGResourceKind::Swapchain:
                    return "swapchain";
                default:
                    return "unknown";
            }
        }

        /// @brief Returns whether any active graph declaration writes an occlusion query.
        bool HasQueryWrites(ArrayView<RGPass> passes)
        {
            for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
            {
                const RGPass& pass = passes[pass_index];
                if (!pass.QueryWrites.empty())
                    return true;
            }
            return false;
        }
    } // namespace

    void BuildQueueBatches(ArrayView<RGPass> passes, ArrayView<uint32_t> order, bool has_separate_transfer_queue, bool has_separate_compute_queue, Array<RGQueueBatch>& out_batches)
    {
        out_batches.clear();

        for (uint32_t order_index = 0; order_index < order.size(); ++order_index)
        {
            const uint32_t pass_index = order[order_index];
            if (pass_index >= passes.size() || !passes[pass_index].IsActive())
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

    void BuildQueueDependencies(Core::Memory::ArenaAllocator* scratch_arena, ArrayView<RGPass> passes, ArrayView<uint32_t> order, ArrayView<RGQueueBatch> batches, ArrayView<RGPassDependency> pass_dependencies, Array<RGQueueDependency>& out_dependencies)
    {
        out_dependencies.clear();
        if (batches.size() == 0 || pass_dependencies.size() == 0)
            return;

        Array<uint32_t> batch_for_pass;
        batch_for_pass.init(scratch_arena, passes.size(), passes.size());
        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
            batch_for_pass[pass_index] = UINT32_MAX;
        for (uint32_t batch_index = 0; batch_index < batches.size(); ++batch_index)
        {
            const auto& batch = batches[batch_index];
            for (uint32_t pass_offset = 0; pass_offset < batch.PassCount; ++pass_offset)
            {
                const uint32_t order_index = batch.FirstPassOrder + pass_offset;
                if (order_index < order.size() && order[order_index] < passes.size())
                    batch_for_pass[order[order_index]] = batch_index;
            }
        }

        for (uint32_t dependency_index = 0; dependency_index < pass_dependencies.size(); ++dependency_index)
        {
            const auto& dependency = pass_dependencies[dependency_index];
            if (dependency.From >= passes.size() || dependency.To >= passes.size() || !passes[dependency.From].IsActive() || !passes[dependency.To].IsActive())
                continue;
            const uint32_t from_batch = batch_for_pass[dependency.From];
            const uint32_t to_batch   = batch_for_pass[dependency.To];
            if (from_batch == UINT32_MAX || to_batch == UINT32_MAX || from_batch == to_batch || batches[from_batch].Queue == batches[to_batch].Queue)
                continue;

            bool already_recorded = false;
            for (const auto& existing : out_dependencies)
            {
                if (existing.FromBatch == from_batch && existing.ToBatch == to_batch)
                {
                    already_recorded = true;
                    break;
                }
            }
            if (!already_recorded)
                out_dependencies.push({from_batch, to_batch});
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
            if (pass_index >= passes.size() || !passes[pass_index].IsActive())
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
        // ColorReadWrite
        {                                           VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        // DepthWrite
        {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        // DepthRead — the read-only layout prevents pipeline depth writes, but
        // STORE preserves the attachment at vkCmdEndRendering. Synchronization
        // must therefore include the late depth/stencil attachment write.
        {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,  VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
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
        // BufferWrite
        {                                                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,                                                                   VK_ACCESS_2_SHADER_WRITE_BIT,                        VK_IMAGE_LAYOUT_UNDEFINED},
        // BufferReadWrite
        {                                                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,                                     VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,                        VK_IMAGE_LAYOUT_UNDEFINED},
        // IndirectRead
        {                                                     VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,                                                          VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,                        VK_IMAGE_LAYOUT_UNDEFINED},
        // ConditionalRead
        {                                         VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT,                                                 VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT,                        VK_IMAGE_LAYOUT_UNDEFINED},
        // StorageWrite
        {                                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,                                                                   VK_ACCESS_2_SHADER_WRITE_BIT,                          VK_IMAGE_LAYOUT_GENERAL},
    };

    RGResourceState GetRGAccessState(RGAccess access)
    {
        const uint32_t index = static_cast<uint32_t>(access);
        if (index >= static_cast<uint32_t>(RGAccess::Count_))
            return {};

        const auto& access_info = kAccessTable[index];
        return {access_info.Stage, access_info.Access, access_info.Layout};
    }

    static VkPipelineStageFlags2 GetBindlessShaderStages(RGShaderStages stages)
    {
        VkPipelineStageFlags2 result = VK_PIPELINE_STAGE_2_NONE;
        if (HasRGShaderStage(stages, RGShaderStages::Vertex))
            result |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        if (HasRGShaderStage(stages, RGShaderStages::TessCtrl))
            result |= VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT;
        if (HasRGShaderStage(stages, RGShaderStages::TessEval))
            result |= VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT;
        if (HasRGShaderStage(stages, RGShaderStages::Geometry))
            result |= VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
        if (HasRGShaderStage(stages, RGShaderStages::Fragment))
            result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        if (HasRGShaderStage(stages, RGShaderStages::Compute))
            result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        return result;
    }

    static RGResourceState GetPassResourceState(const RGPassResource& resource)
    {
        return resource.HasStateOverride ? resource.StateOverride : GetRGAccessState(resource.Access);
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

    static VkImageAspectFlags GetResourceAspectMask(const RGResource& resource, Hardwares::VulkanDevice* device)
    {
        bool depth = resource.Spec.Format == Rendering::Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE;
        if (!depth && resource.TextureHandle.Valid())
        {
            const auto* texture = device->GlobalTextures.Access(resource.TextureHandle);
            depth               = texture && texture->IsDepthTexture;
        }
        return depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }

    static uint32_t GetResourceMipLevelCount(const RGResource& resource, Hardwares::VulkanDevice* device)
    {
        if (resource.TextureHandle.Valid())
        {
            const auto* texture = device->GlobalTextures.Access(resource.TextureHandle);
            if (texture)
                return texture->Specification.MipLevelCount;
        }
        return resource.Spec.MipLevelCount;
    }

    static uint32_t GetResourceLayerCount(const RGResource& resource, Hardwares::VulkanDevice* device)
    {
        if (resource.TextureHandle.Valid())
        {
            const auto* texture = device->GlobalTextures.Access(resource.TextureHandle);
            if (texture)
                return texture->Specification.LayerCount;
        }
        return resource.Spec.LayerCount;
    }

    static bool ResolveSubresourceRange(const RGResource& resource, const RGSubresourceRange& declared, Hardwares::VulkanDevice* device, RGSubresourceRange* resolved)
    {
        const VkImageAspectFlags available_aspects = GetResourceAspectMask(resource, device);
        const VkImageAspectFlags aspects           = declared.AspectMask == 0 ? available_aspects : declared.AspectMask;
        const uint32_t           mip_count         = GetResourceMipLevelCount(resource, device);
        const uint32_t           layer_count       = GetResourceLayerCount(resource, device);
        if (aspects == 0 || (aspects & ~available_aspects) != 0 || mip_count == 0 || layer_count == 0 || declared.BaseMipLevel >= mip_count || declared.BaseArrayLayer >= layer_count)
            return false;

        const uint32_t resolved_mip_count   = declared.LevelCount == VK_REMAINING_MIP_LEVELS ? mip_count - declared.BaseMipLevel : declared.LevelCount;
        const uint32_t resolved_layer_count = declared.LayerCount == VK_REMAINING_ARRAY_LAYERS ? layer_count - declared.BaseArrayLayer : declared.LayerCount;
        if (resolved_mip_count == 0 || resolved_mip_count > mip_count - declared.BaseMipLevel || resolved_layer_count == 0 || resolved_layer_count > layer_count - declared.BaseArrayLayer)
            return false;

        *resolved = {.AspectMask = aspects, .BaseMipLevel = declared.BaseMipLevel, .LevelCount = resolved_mip_count, .BaseArrayLayer = declared.BaseArrayLayer, .LayerCount = resolved_layer_count};
        return true;
    }

    static RGSubresourceState* FindSubresourceState(Core::Containers::Array<RGSubresourceState>& states, const RGSubresourceRange& range)
    {
        for (auto& state : states)
        {
            if (state.Range.AspectMask == range.AspectMask && state.Range.BaseMipLevel == range.BaseMipLevel && state.Range.LevelCount == range.LevelCount && state.Range.BaseArrayLayer == range.BaseArrayLayer && state.Range.LayerCount == range.LayerCount)
                return &state;
        }
        return nullptr;
    }

    static RGSubresourceState& GetSubresourceState(Core::Containers::Array<RGSubresourceState>& states, const RGSubresourceRange& range, const RGResourceState& initial_state)
    {
        if (auto* state = FindSubresourceState(states, range))
            return *state;
        return states.push_use({.Range = range, .State = initial_state});
    }

    static VkImageSubresourceRange ToVkSubresourceRange(const RGSubresourceRange& range)
    {
        return {.aspectMask = range.AspectMask, .baseMipLevel = range.BaseMipLevel, .levelCount = range.LevelCount, .baseArrayLayer = range.BaseArrayLayer, .layerCount = range.LayerCount};
    }

    struct RGDynamicRenderingBeginResult
    {
        bool Began         = false;
        bool UsesSwapchain = false;
    };

    static VkClearValue GetTextureClearValue(const Textures::Texture& texture)
    {
        VkClearValue clear_value = {};
        if (texture.IsDepthTexture)
        {
            clear_value.depthStencil = {.depth = texture.Specification.ClearDepth, .stencil = texture.Specification.ClearStencil};
        }
        else
        {
            clear_value.color.float32[0] = texture.Specification.ClearColor[0];
            clear_value.color.float32[1] = texture.Specification.ClearColor[1];
            clear_value.color.float32[2] = texture.Specification.ClearColor[2];
            clear_value.color.float32[3] = texture.Specification.ClearColor[3];
        }
        return clear_value;
    }

    static bool GetGraphAttachmentView(RenderGraph* graph, const RGResource& resource, const RGPassResource& use, VkImageView* image_view, VkFormat* image_format, uint32_t* width, uint32_t* height, uint32_t* layer_count, VkClearValue* clear_value)
    {
        if (resource.Kind == RGResourceKind::Swapchain)
        {
            const auto* swapchain = graph->Device->SwapchainPtr;
            if (!swapchain || !swapchain->CurrentFrame)
                return false;
            const uint32_t image_index = swapchain->CurrentFrame->ImageIndex;
            if (image_index >= swapchain->SwapchainImageViews.size())
                return false;
            *image_view   = swapchain->SwapchainImageViews[image_index];
            *image_format = graph->Device->SurfaceFormat.format;
            *width        = swapchain->SwapchainImageWidth;
            *height       = swapchain->SwapchainImageHeight;
            *layer_count  = 1;
            *clear_value  = {};
            return *image_view != VK_NULL_HANDLE;
        }

        RGSubresourceRange range;
        if (!ResolveSubresourceRange(resource, use.Range, graph->Device, &range))
            return false;
        const auto* texture = graph->Device->GlobalTextures.Access(resource.TextureHandle);
        if (!texture)
            return false;
        auto* image_buffer = graph->Device->ImageBufferManager.Access(texture->BufferHandle);
        if (!image_buffer)
            return false;

        const VkImageSubresourceRange vk_range  = ToVkSubresourceRange(range);
        const VkImageViewType         view_type = range.LayerCount == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        *image_view                             = image_buffer->GetImageViewHandle(view_type, vk_range);
        *image_format                           = image_buffer->Specification.ImageFormat;
        *width                                  = std::max(1u, texture->Width >> range.BaseMipLevel);
        *height                                 = std::max(1u, texture->Height >> range.BaseMipLevel);
        *layer_count                            = range.LayerCount;
        *clear_value                            = GetTextureClearValue(*texture);
        return *image_view != VK_NULL_HANDLE;
    }

    static RGDynamicRenderingBeginResult BeginGraphDynamicRendering(RenderGraph* graph, RGPass& pass, Hardwares::CommandBuffer* command_buffer, bool use_secondary_commands)
    {
        RGDynamicRenderingBeginResult result = {};
        if (!graph || !pass.Handle || !command_buffer)
            return result;

        auto* const               graphic_pass           = static_cast<RenderPasses::GraphicPass*>(pass.Handle);
        VkRenderingAttachmentInfo color_attachments[16]  = {};
        uint32_t                  color_attachment_count = 0;

        VkRenderingAttachmentInfo depth_attachment       = {};
        bool                      has_depth_attachment   = false;
        bool                      has_stencil_attachment = false;
        uint32_t                  render_width           = 0;
        uint32_t                  render_height          = 0;
        uint32_t                  render_layer_count     = 0;

        auto                      establish_render_area  = [&](uint32_t width, uint32_t height, uint32_t layer_count) {
            if (render_width == 0)
            {
                render_width       = width;
                render_height      = height;
                render_layer_count = layer_count;
                return true;
            }
            return render_width == width && render_height == height && render_layer_count == layer_count;
        };
        auto add_attachment = [&](const RGPassResource& use, bool is_depth) {
            if (!use.Handle.Valid() || use.Handle.Index >= graph->Resources.size())
                return false;

            const RGResource& resource     = graph->Resources[use.Handle.Index];
            VkImageView       image_view   = VK_NULL_HANDLE;
            VkFormat          image_format = VK_FORMAT_UNDEFINED;
            VkClearValue      clear_value  = {};
            uint32_t          width        = 0;
            uint32_t          height       = 0;
            uint32_t          layer_count  = 0;
            if (!GetGraphAttachmentView(graph, resource, use, &image_view, &image_format, &width, &height, &layer_count, &clear_value) || !establish_render_area(width, height, layer_count))
                return false;

            VkRenderingAttachmentInfo attachment = {};
            attachment.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView                 = image_view;
            attachment.imageLayout               = GetRGAccessState(use.Access).Layout;
            attachment.resolveMode               = VK_RESOLVE_MODE_NONE;
            attachment.resolveImageView          = VK_NULL_HANDLE;
            attachment.resolveImageLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.loadOp                    = is_depth && use.Access == RGAccess::DepthRead ? VK_ATTACHMENT_LOAD_OP_LOAD : Specifications::AttachmentLoadOperationMap[VALUE_FROM_SPEC_MAP(use.LoadOp)];
            attachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.clearValue                = clear_value;

            if (is_depth)
            {
                if (has_depth_attachment)
                    return false;
                depth_attachment       = attachment;
                has_depth_attachment   = true;
                has_stencil_attachment = image_format == VK_FORMAT_D16_UNORM_S8_UINT || image_format == VK_FORMAT_D24_UNORM_S8_UINT || image_format == VK_FORMAT_D32_SFLOAT_S8_UINT || image_format == VK_FORMAT_S8_UINT;
            }
            else
            {
                if (color_attachment_count >= 16)
                    return false;
                color_attachments[color_attachment_count++] = attachment;
            }
            result.UsesSwapchain = result.UsesSwapchain || resource.Kind == RGResourceKind::Swapchain;
            return true;
        };

        for (const auto& read : pass.Reads)
        {
            if (read.Access == RGAccess::DepthRead && !add_attachment(read, true))
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Dynamic rendering could not resolve depth attachment for pass '{}'", pass.Name ? pass.Name : "?")
                return {};
            }
        }
        for (const auto& write : pass.Writes)
        {
            const bool is_depth = write.Access == RGAccess::DepthWrite;
            if ((is_depth || write.Access == RGAccess::ColorWrite || write.Access == RGAccess::ColorReadWrite) && !add_attachment(write, is_depth))
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Dynamic rendering could not resolve attachment for pass '{}'", pass.Name ? pass.Name : "?")
                return {};
            }
        }

        if (color_attachment_count == 0 && !has_depth_attachment)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Dynamic graphics pass '{}' has no declared attachments", pass.Name ? pass.Name : "?")
            return {};
        }

        if (result.UsesSwapchain)
            command_buffer->TransitionSwapchainImageToColorAttachment();

        graphic_pass->RenderAreaWidth  = render_width;
        graphic_pass->RenderAreaHeight = render_height;
        VkRenderingInfo rendering_info = {};
        rendering_info.sType           = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.flags           = use_secondary_commands ? VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT : 0;
        rendering_info.renderArea      = {
            .offset = {                    0,                       0},
              .extent = {.width = render_width, .height = render_height}
        };
        rendering_info.layerCount           = render_layer_count;
        rendering_info.viewMask             = 0;
        rendering_info.colorAttachmentCount = color_attachment_count;
        rendering_info.pColorAttachments    = color_attachments;
        rendering_info.pDepthAttachment     = has_depth_attachment ? &depth_attachment : nullptr;
        rendering_info.pStencilAttachment   = has_stencil_attachment ? &depth_attachment : nullptr;
        command_buffer->BeginDynamicRendering(rendering_info);
        result.Began = true;
        return result;
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
        m_arena = arena;
        Slots.init(arena, 32);
    }

    static bool HasCompatibleImageLayout(const Specifications::TextureSpecification& left, const Specifications::TextureSpecification& right)
    {
        return left.Format == right.Format && left.Width == right.Width && left.Height == right.Height && left.MipLevelCount == right.MipLevelCount && left.LayerCount == right.LayerCount && left.IsCubemap == right.IsCubemap;
    }

    static bool HasImageUsageSuperset(const Specifications::TextureSpecification& allocation, const Specifications::TextureSpecification& request)
    {
        return (!request.IsUsageSampled || allocation.IsUsageSampled) && (!request.IsUsageStorage || allocation.IsUsageStorage) && (!request.IsUsageTransfert || allocation.IsUsageTransfert) && (!request.IsUsageTransferSource || allocation.IsUsageTransferSource);
    }

    static bool LifetimesOverlap(uint32_t first_a, uint32_t last_a, uint32_t first_b, uint32_t last_b)
    {
        return first_a <= last_b && first_b <= last_a;
    }

    void RGTransientPool::BeginFrame()
    {
        for (auto& slot : Slots)
        {
            for (auto& alias : slot.Aliases)
                alias.Active = false;
        }
    }

    Textures::TextureHandle RGTransientPool::TryAlias(const Specifications::TextureSpecification& spec, uint32_t first_pass)
    {
        for (auto& slot : Slots)
        {
            if (slot.FreeAfterPass >= first_pass)
                continue;
            const auto& s = slot.Spec;
            if (!HasCompatibleImageLayout(s, spec))
                continue;
            // Reuse is legal only when the existing image was created with every
            // usage bit required by the new logical resource.
            if (!HasImageUsageSuperset(s, spec))
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
        slot.Aliases.init(m_arena, 4);
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

    Textures::TextureHandle RGTransientPool::FindNamedAlias(cstring name, const Specifications::TextureSpecification& spec, uint32_t first_pass, uint32_t last_pass)
    {
        for (auto& slot : Slots)
        {
            for (uint32_t alias_index = static_cast<uint32_t>(slot.Aliases.size()); alias_index > 0; --alias_index)
            {
                auto& alias = slot.Aliases[alias_index - 1];
                if (alias.Active || Helpers::secure_strcmp(alias.Name, name) != 0 || !HasCompatibleImageLayout(alias.Spec, spec) || !HasImageUsageSuperset(alias.Spec, spec))
                    continue;

                bool overlaps = false;
                for (const auto& other : slot.Aliases)
                {
                    if (&other == &alias || !other.Active)
                        continue;
                    if (LifetimesOverlap(first_pass, last_pass, other.FirstPass, other.LastPass))
                    {
                        overlaps = true;
                        break;
                    }
                }
                if (overlaps)
                    continue;
                alias.FirstPass = first_pass;
                alias.LastPass  = last_pass;
                alias.Active    = true;
                return alias.Handle;
            }
        }
        return {};
    }

    RGTransientSlot* RGTransientPool::FindAliasingSlot(const Specifications::TextureSpecification& spec, uint32_t first_pass, uint32_t last_pass)
    {
        for (auto& slot : Slots)
        {
            if (!slot.Handle.Valid() || !HasCompatibleImageLayout(slot.Spec, spec))
                continue;

            bool overlaps = false;
            for (const auto& alias : slot.Aliases)
            {
                if (alias.Active && LifetimesOverlap(first_pass, last_pass, alias.FirstPass, alias.LastPass))
                {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps)
                return &slot;
        }
        return nullptr;
    }

    void RGTransientPool::RegisterAlias(RGTransientSlot* slot, cstring name, Textures::TextureHandle handle, const Specifications::TextureSpecification& spec, uint32_t first_pass, uint32_t last_pass)
    {
        if (!slot)
            return;
        slot->Aliases.push({.Name = name, .Handle = handle, .Spec = spec, .FirstPass = first_pass, .LastPass = last_pass, .Active = true});
    }

    void RGTransientPool::Clear()
    {
        Slots.clear();
    }

    void RGTransientBufferPool::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        m_arena = arena;
        Slots.init(arena, 16);
    }

    void RGTransientBufferPool::BeginFrame()
    {
        for (auto& slot : Slots)
        {
            for (auto& alias : slot.Aliases)
                alias.Active = false;
        }
    }

    Core::Memory::BufferView* RGTransientBufferPool::TryAlias(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t first_pass)
    {
        for (auto& slot : Slots)
        {
            if (slot.FreeAfterPass >= first_pass || slot.Size < size || (slot.Usage & usage) != usage)
                continue;
            return slot.Buffer;
        }
        return nullptr;
    }

    void RGTransientBufferPool::Register(Core::Memory::BufferView* buffer, VkDeviceSize size, VkBufferUsageFlags usage, uint32_t last_pass)
    {
        auto& slot         = Slots.push_use({});
        slot.Buffer        = buffer;
        slot.Size          = size;
        slot.Usage         = usage;
        slot.FreeAfterPass = last_pass;
        slot.Aliases.init(m_arena, 4);
    }

    void RGTransientBufferPool::MarkInUse(Core::Memory::BufferView* buffer, uint32_t last_pass)
    {
        for (auto& slot : Slots)
        {
            if (slot.Buffer == buffer)
            {
                slot.FreeAfterPass = last_pass;
                return;
            }
        }
    }

    Core::Memory::BufferView* RGTransientBufferPool::FindNamedAlias(cstring name, VkDeviceSize size, VkBufferUsageFlags usage, uint32_t first_pass, uint32_t last_pass)
    {
        for (auto& slot : Slots)
        {
            for (uint32_t alias_index = static_cast<uint32_t>(slot.Aliases.size()); alias_index > 0; --alias_index)
            {
                auto& alias = slot.Aliases[alias_index - 1];
                if (alias.Active || Helpers::secure_strcmp(alias.Name, name) != 0 || alias.Size < size || (alias.Usage & usage) != usage)
                    continue;

                bool overlaps = false;
                for (const auto& other : slot.Aliases)
                {
                    if (&other == &alias || !other.Active)
                        continue;
                    if (LifetimesOverlap(first_pass, last_pass, other.FirstPass, other.LastPass))
                    {
                        overlaps = true;
                        break;
                    }
                }
                if (overlaps)
                    continue;
                alias.FirstPass = first_pass;
                alias.LastPass  = last_pass;
                alias.Active    = true;
                return alias.Buffer;
            }
        }
        return nullptr;
    }

    RGTransientBufferSlot* RGTransientBufferPool::FindAliasingSlot(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t first_pass, uint32_t last_pass)
    {
        for (auto& slot : Slots)
        {
            if (!slot.Buffer || slot.Size < size || (slot.Usage & usage) != usage)
                continue;

            bool overlaps = false;
            for (const auto& alias : slot.Aliases)
            {
                if (alias.Active && LifetimesOverlap(first_pass, last_pass, alias.FirstPass, alias.LastPass))
                {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps)
                return &slot;
        }
        return nullptr;
    }

    void RGTransientBufferPool::RegisterAlias(RGTransientBufferSlot* slot, cstring name, Core::Memory::BufferView* buffer, VkDeviceSize size, VkBufferUsageFlags usage, uint32_t first_pass, uint32_t last_pass)
    {
        if (!slot)
            return;
        slot->Aliases.push({.Name = name, .Buffer = buffer, .Size = size, .Usage = usage, .FirstPass = first_pass, .LastPass = last_pass, .Active = true});
    }

    void RGTransientBufferPool::Clear()
    {
        Slots.clear();
    }

    void RenderGraph::Initialize(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr data)
    {
        Device       = device;
        SceneData    = data;
        RenderWidth  = Device && Device->SwapchainPtr ? Device->SwapchainPtr->SwapchainImageWidth : 0;
        RenderHeight = Device && Device->SwapchainPtr ? Device->SwapchainPtr->SwapchainImageHeight : 0;

        // RenderGraph has a bounded number of virtual passes/resources per frame.
        // Persistent pipelines, framebuffers, and transient images stay in the
        // device arena; this sub-arena can therefore be rewound after recording.
        Device->Arena->CreateSubArena(ZMega(2), &FrameArena);
        InitializeFrameStorage();
        PersistentPasses.init(Device->Arena, 16);
        ImportedResources.init(Device->Arena, 32);
        ImportedResourceIndex.init(Device->Arena, 64);
        QueryPools.init(Device->Arena, 8);
        QueryPoolIndex.init(Device->Arena, 16);
        TransientPool.Initialize(Device->Arena);
        TransientBufferPool.Initialize(Device->Arena);
        ReadbackRing.Initialize(Device);
        InitializeTimestampFrames();

        for (uint32_t i = 0; i < QueueTimelineCount; ++i)
            QueueTimelines[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Primitives::Semaphore, Device, true);

        ResourceBuilder   = ZPushStruct(Device->Arena, RenderGraphResourceBuilder);
        ResourceInspector = ZPushStruct(Device->Arena, RenderGraphResourceInspector);

        ResourceBuilder->Initialize(this);
        ResourceInspector->Initialize(this);
    }

    void RenderGraph::AddCallbackPass(cstring pass_name, IRenderGraphCallbackPass* const cb)
    {
        ZENGINE_VALIDATE_ASSERT(pass_name != nullptr && cb != nullptr, "Render graph callback pass is invalid")
        auto& pass    = PersistentPasses.push_use({});
        pass.Name     = pass_name;
        pass.Callback = cb;
    }

    Specifications::RenderPassSpecification RenderGraph::BuildRenderPassSpecification(const RGPass& pass) const
    {
        Specifications::RenderPassSpecification specification = {};
        specification.DebugName                               = pass.Name;

        for (const auto& read : pass.Reads)
        {
            if (!read.Handle.Valid() || read.Handle.Index >= Resources.size())
                continue;
            if (read.Access != RGAccess::DepthRead && read.Access != RGAccess::DepthWrite)
                continue;

            const RGResource& resource = Resources[read.Handle.Index];
            if (!resource.TextureHandle.Valid())
                continue;
            if (specification.Inputs.capacity() == 0)
                specification.Inputs.init(Device->Arena, 4);
            specification.Inputs.push(resource.TextureHandle);
        }

        for (const auto& write : pass.Writes)
        {
            if (!write.Handle.Valid() || write.Handle.Index >= Resources.size())
                continue;

            const RGResource& resource = Resources[write.Handle.Index];
            if (resource.Kind == RGResourceKind::Swapchain)
            {
                specification.SwapchainAsRenderTarget = true;
                continue;
            }
            if (!resource.TextureHandle.Valid())
                continue;
            if (specification.ExternalOutputs.capacity() == 0)
                specification.ExternalOutputs.init(Device->Arena, 4);
            if (specification.ExternalOutputLoadOps.capacity() == 0)
                specification.ExternalOutputLoadOps.init(Device->Arena, 4);
            specification.ExternalOutputs.push(resource.TextureHandle);
            specification.ExternalOutputLoadOps.push(write.LoadOp);
        }
        return specification;
    }

    void RenderGraph::InitializeFrameStorage()
    {
        Passes.init(&FrameArena, 16);
        Resources.init(&FrameArena, 32);
        SortedPassIndices.init(&FrameArena, 16);
        PassDependencies.init(&FrameArena, 32);
        TopologyLevels.init(&FrameArena, 8);
        QueueBatches.init(&FrameArena, 8);
        QueueDependencies.init(&FrameArena, 8);
        QueueOwnershipTransfers.init(&FrameArena, 16);
        ExportedResources.init(&FrameArena, 8);
        StreamingUploadTickets.init(&FrameArena, 16);
        ReadbackRequests.init(&FrameArena, 4);
        QueryReadbackRequests.init(&FrameArena, 4);
        ResourceIndex.init(&FrameArena, 64);
        PassIndex.init(&FrameArena, 32);
    }

    void RenderGraph::AddImportedResourceToFrame(const RGImportedResource& resource)
    {
        auto& destination              = Resources.push_use({});
        destination.Name               = resource.Name;
        destination.Kind               = resource.Kind;
        destination.External           = true;
        destination.Transient          = false;
        destination.TextureHandle      = resource.TextureHandle;
        destination.Buffer             = resource.Buffer;
        destination.BufferSize         = resource.Buffer ? resource.Buffer->Size : 0;
        destination.BufferUsage        = resource.Buffer ? resource.Buffer->Usage : 0;
        destination.InitialState       = resource.InitialState;
        destination.CurrentState       = resource.InitialState;
        destination.RuntimeState       = resource.InitialState;
        destination.HasStreamingTicket = resource.HasStreamingTicket;
        destination.StreamingTicket    = resource.StreamingTicket;
        InitializeResourceVersions(destination, &FrameArena);
        ResourceIndex[resource.Name] = static_cast<uint32_t>(Resources.size() - 1);
    }

    RGResourceHandle RenderGraph::SetImportedResource(const RGImportedResource& resource)
    {
        RGImportedResource* persistent = nullptr;
        if (auto* index = ImportedResourceIndex.find(resource.Name))
        {
            persistent  = &ImportedResources[*index];
            *persistent = resource;
        }
        else
        {
            const uint32_t new_index             = static_cast<uint32_t>(ImportedResources.size());
            persistent                           = &ImportedResources.push_use(resource);
            ImportedResourceIndex[resource.Name] = new_index;
        }

        if (auto* index = ResourceIndex.find(resource.Name))
        {
            RGResource& destination        = Resources[*index];
            destination.Name               = persistent->Name;
            destination.Kind               = persistent->Kind;
            destination.External           = true;
            destination.Transient          = false;
            destination.TextureHandle      = persistent->TextureHandle;
            destination.Buffer             = persistent->Buffer;
            destination.BufferSize         = persistent->Buffer ? persistent->Buffer->Size : 0;
            destination.BufferUsage        = persistent->Buffer ? persistent->Buffer->Usage : 0;
            destination.InitialState       = persistent->InitialState;
            destination.CurrentState       = persistent->InitialState;
            destination.RuntimeState       = persistent->InitialState;
            destination.HasStreamingTicket = persistent->HasStreamingTicket;
            destination.StreamingTicket    = persistent->StreamingTicket;
            destination.LatestVersion      = 0;
            InitializeResourceVersions(destination, &FrameArena);
            return {*index, 0};
        }

        AddImportedResourceToFrame(*persistent);
        return {static_cast<uint32_t>(Resources.size() - 1), 0};
    }

    void RenderGraph::Setup()
    {
        Register({.Scene = SceneData, .FrameIndex = static_cast<uint8_t>(Device->SwapchainPtr->CurrentFrame ? Device->SwapchainPtr->CurrentFrame->Index : 0), .RenderWidth = RenderWidth, .RenderHeight = RenderHeight});
    }

    void RenderGraph::Register(const RenderGraphFrameContext& frame_context)
    {
        RenderGraphFrameContext resolved_context = frame_context;
        if (resolved_context.RenderWidth == 0)
            resolved_context.RenderWidth = RenderWidth;
        if (resolved_context.RenderHeight == 0)
            resolved_context.RenderHeight = RenderHeight;

        m_compile_valid     = false;
        TransientStatistics = {};

        // A retained graphics batch may have been discarded when Present() aborted.
        // Its reservation has no GPU submission and is therefore safe to reuse now.
        ReadbackRing.Poll();
        ReadbackRing.CancelUnsubmitted();

        // All virtual handles from the preceding frame expire here. Persistent
        // state lives in PersistentPasses, ImportedResources, and TransientPool.
        FrameArena.Clear();
        TransientPool.BeginFrame();
        TransientBufferPool.BeginFrame();
        InitializeFrameStorage();
        if (Device->RRM)
        {
            const auto& tickets = static_cast<Rendering::RenderResourceManager*>(Device->RRM)->GetStreamingUploadTickets();
            for (const auto& ticket : tickets)
                StreamingUploadTickets.push(ticket);
        }
        for (const auto& resource : ImportedResources)
            AddImportedResourceToFrame(resource);

        for (uint32_t i = 0; i < PersistentPasses.size(); ++i)
        {
            const auto& persistent     = PersistentPasses[i];
            auto&       pass           = Passes.push_use({});
            pass.Name                  = persistent.Name;
            pass.Enabled               = false;
            pass.Persistent            = &PersistentPasses[i];
            pass.Callback              = persistent.Callback;
            pass.Flags                 = pass.Callback ? pass.Callback->GetPassFlags() : RGPassFlags::None;
            pass.RequiresRenderPass    = !pass.Callback || pass.Callback->RequiresRenderPass();
            pass.Handle                = persistent.Handle;
            pass.Framebuffer           = persistent.Framebuffer;
            pass.FramebufferRenderPass = persistent.FramebufferRenderPass;
            pass.FramebufferViewCount  = persistent.FramebufferViewCount;
            pass.FramebufferWidth      = persistent.FramebufferWidth;
            pass.FramebufferHeight     = persistent.FramebufferHeight;
            pass.FramebufferLayers     = persistent.FramebufferLayers;
            for (uint32_t view_index = 0; view_index < pass.FramebufferViewCount && view_index < 16; ++view_index)
                pass.FramebufferViews[view_index] = persistent.FramebufferViews[view_index];
            pass.Reads.init(&FrameArena, 8);
            pass.Writes.init(&FrameArena, 8);
            pass.QueryWrites.init(&FrameArena, 2);
            pass.QueryResets.init(&FrameArena, 2);
            pass.BarrierPlans.init(&FrameArena, 8);
            pass.BufferBarrierPlans.init(&FrameArena, 8);
            pass.AliasingBarrierPlans.init(&FrameArena, 4);
            pass.StreamingAcquirePlans.init(&FrameArena, 4);
            PassIndex[pass.Name]         = i;
            ResourceBuilder->CurrentPass = i;
            if (pass.Callback)
                pass.Enabled = pass.Callback->Register(Device, pass.Name, resolved_context, ResourceBuilder, ResourceInspector);
        }
        ResourceBuilder->CurrentPass = UINT32_MAX;
        AddReadbackPasses();
        AddQueryReadbackPasses();
    }

    void RenderGraph::Compile()
    {
        if (!ValidateDeclarations())
        {
            m_compile_valid = false;
            return;
        }
        // BuildTopology includes the reverse-reachability culling pass. Lifetimes
        // are indexed by its final live execution order, never declaration order.
        if (!BuildTopology())
        {
            m_compile_valid = false;
            return;
        }
        BuildLifetimes();
        AllocateTransientResources();
        UpdateTransientStatistics();
        BuildAliasingBarriers();
        BuildBarriers();

        for (uint32_t i = 0; i < SortedPassIndices.size(); ++i)
        {
            uint32_t pi   = SortedPassIndices[i];
            RGPass&  pass = Passes[pi];
            if (!pass.IsActive() || !pass.Callback)
                continue;

            if (pass.RequiresRenderPass && !pass.Handle)
            {
                auto pass_spec = BuildRenderPassSpecification(pass);
                pass_spec.Type = pass.Callback->GetPipelineType();
                if (pass_spec.Type == Specifications::RenderPassType::GRAPHIC)
                {
                    pass_spec.PipelineDescription = pass.Callback->BuildGraphicsPipelineDescription(Device->Arena);
                    if (!pass_spec.PipelineDescription.ShaderSpecificationValue.Name)
                    {
                        ZENGINE_CORE_ERROR("[RenderGraph] Graphics pass '{}' did not provide a shader", pass.Name ? pass.Name : "?")
                        m_compile_valid = false;
                        return;
                    }
                }
                else if (pass_spec.Type == Specifications::RenderPassType::COMPUTE)
                {
                    pass_spec.ComputeShaderName       = pass.Callback->GetComputeShaderName();
                    pass_spec.ComputePushConstantSize = pass.Callback->GetComputePushConstantSize();
                    if (!pass_spec.ComputeShaderName)
                    {
                        ZENGINE_CORE_ERROR("[RenderGraph] Compute pass '{}' did not provide a shader", pass.Name ? pass.Name : "?")
                        m_compile_valid = false;
                        return;
                    }
                }
                else
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Pass '{}' requested an unsupported backend pass type", pass.Name ? pass.Name : "?")
                    m_compile_valid = false;
                    return;
                }

                pass.Handle = Device->CreateRenderPass(std::move(pass_spec));
                pass.Handle->Bake();
            }

            SynchronizeCompiledPassResources(pass);

            pass.Callback->Prepare(Device, SceneData, ResourceInspector, pass.Handle);
            if (pass.Persistent)
                pass.Persistent->Handle = pass.Handle;

            // Pipeline hot-reload/replay can allocate from Device->Arena and
            // update descriptor sets. Complete it on the render thread before
            // any worker records this pass into a secondary command buffer.
            EnsurePassPipelineOnRenderThread(pass);
            BindDeclaredBufferResources(pass);
            if (IsGraphicPass(pass))
                static_cast<RenderPasses::GraphicPass*>(pass.Handle)->Verify();
        }

        if (!ValidateCallbackContracts())
        {
            m_compile_valid = false;
            return;
        }

        AllocateFramebuffers();
        BuildQueueSchedule();
        if (!PrepareReadbacks() || !PrepareQueryReadbacks())
        {
            m_compile_valid = false;
            return;
        }
        m_compile_valid = true;
    }

    Hardwares::CommandBuffer* RenderGraph::Execute(Hardwares::CommandBufferPtr const cb)
    {
        Register({.Scene = SceneData, .FrameIndex = static_cast<uint8_t>(Device->SwapchainPtr->CurrentFrame->Index), .RenderWidth = RenderWidth, .RenderHeight = RenderHeight});
        Compile();
        if (!m_compile_valid)
        {
            CancelUnsubmittedReadbacks();
            return cb;
        }

        // Compile can create a pass whose bindless set has never received the
        // already-live viewport texture. Publish pending texture descriptors only
        // after all passes registered their descriptor destinations, and before
        // any graph commands consume them.
        Device->FlushBindlessTextureUpdates();

        RGTimestampFrame* const timestamp_frame = PrepareTimestampFrame();

        // Imported graph buffers currently model the scene's host-visible buffers.
        // They may be updated by the CPU before every frame, so restore their
        // declared external producer state before stamping this frame's barriers.
        for (auto& resource : Resources)
        {
            if (resource.Kind == RGResourceKind::Buffer && resource.External)
                resource.RuntimeState = resource.InitialState;
        }

        auto scratch = ZGetScratch(Device->Arena);

        // The application-owned primary buffer stays open for overlay rendering.
        // Therefore only the final graphics batch is retained for it; all earlier
        // batches are submitted immediately on their resolved queue.
        if (QueueBatches.empty())
        {
            CancelUnsubmittedReadbacks();
            ZReleaseScratch(scratch);
            return cb;
        }

        ZENGINE_VALIDATE_ASSERT(QueueBatches.size() <= Device->CommandBufferMgr->MaxGraphBatchesPerPool, "Render graph batch command buffer capacity exceeded")

        const uint32_t external_async_operation_count = static_cast<uint32_t>(Device->SwapchainPtr->FrameAsyncOperations.size());
        const uint32_t final_batch_index              = static_cast<uint32_t>(QueueBatches.size() - 1);
        const uint8_t  frame_index                    = static_cast<uint8_t>(Device->SwapchainPtr->CurrentFrame->Index);

        // Secondary pools are reset and grown exclusively on the render thread.
        // Every worker subsequently touches only its own command pool and buffers.
        for (auto& pass : Passes)
        {
            pass.Secondary         = nullptr;
            pass.SecondaryWorker   = UINT32_MAX;
            pass.SecondaryOrdinal  = UINT32_MAX;
            pass.SecondaryRecorded = false;
        }

        const uint32_t available_workers = Helpers::ThreadPoolHelper::Pool ? static_cast<uint32_t>(Helpers::ThreadPoolHelper::Pool->WorkerCount) : 0;
        const uint32_t worker_count      = std::min(Device->CommandBufferMgr->TotalThreadCount, available_workers);
        if (worker_count > 0 && !TopologyLevels.empty())
        {
            ZENGINE_VALIDATE_ASSERT(worker_count <= Helpers::ThreadPool::MAX_WORKERS, "Render graph worker count exceeds thread-pool capacity")
            Device->CommandBufferMgr->BeginWorkerSecondaryFrame(frame_index);

            Array<uint32_t> secondary_counts;
            secondary_counts.init(scratch.Arena, worker_count * QueueTimelineCount, worker_count * QueueTimelineCount);
            for (uint32_t i = 0; i < secondary_counts.size(); ++i)
                secondary_counts[i] = 0;

            // Assign globally monotonic per-(worker, queue) ordinals before
            // growing pools. Worker tasks only acquire pre-existing buffers.
            for (const auto& level : TopologyLevels)
            {
                uint32_t recordable_index = 0;
                for (uint32_t pass_index : level.PassIndices)
                {
                    if (pass_index >= Passes.size())
                        continue;
                    RGPass& pass = Passes[pass_index];
                    if (!CanRecordSecondary(Device, pass))
                        continue;

                    const uint32_t queue_index = static_cast<uint32_t>(pass.Queue);
                    ZENGINE_VALIDATE_ASSERT(queue_index < QueueTimelineCount, "Invalid render graph queue type")
                    pass.SecondaryWorker   = recordable_index++ % worker_count;
                    const uint32_t counter = pass.SecondaryWorker * QueueTimelineCount + queue_index;
                    pass.SecondaryOrdinal  = secondary_counts[counter]++;
                }
            }

            for (uint32_t worker = 0; worker < worker_count; ++worker)
            {
                for (uint32_t queue = 0; queue < QueueTimelineCount; ++queue)
                {
                    const uint32_t count = secondary_counts[worker * QueueTimelineCount + queue];
                    if (count > 0)
                        Device->CommandBufferMgr->PrepareWorkerSecondary(static_cast<Rendering::QueueType>(queue), frame_index, worker, count);
                }
            }

            // Record one dependency level at a time. Playback is deliberately
            // deferred until every level has been recorded: queue batches may
            // span levels, and their sorted-order submission plan remains the
            // authoritative source of timeline waits and ownership transfers.
            for (const auto& level : TopologyLevels)
            {
                bool     worker_has_work[Helpers::ThreadPool::MAX_WORKERS] = {};
                uint32_t task_count                                        = 0;
                for (uint32_t pass_index : level.PassIndices)
                {
                    if (pass_index >= Passes.size())
                        continue;
                    const uint32_t worker = Passes[pass_index].SecondaryWorker;
                    if (worker < worker_count && !worker_has_work[worker])
                    {
                        worker_has_work[worker] = true;
                        ++task_count;
                    }
                }
                if (task_count == 0)
                    continue;

                std::latch                     completion(task_count);
                RenderGraphSecondaryRecordTask tasks[Helpers::ThreadPool::MAX_WORKERS]                   = {};
                bool                           record_on_render_thread[Helpers::ThreadPool::MAX_WORKERS] = {};
                uint32_t                       task_index                                                = 0;
                for (uint32_t worker = 0; worker < worker_count; ++worker)
                {
                    if (!worker_has_work[worker])
                        continue;

                    auto& task      = tasks[task_index++];
                    task.Graph      = this;
                    task.Level      = &level;
                    task.Worker     = worker;
                    task.FrameIndex = frame_index;
                    task.Completion = &completion;
                    if (!Helpers::ThreadPoolHelper::SubmitToWorker(worker, &task, &RecordGraphSecondaryWorkerBatch))
                    {
                        // SubmitToWorker never executes inline. Once the jobs
                        // already accepted for this level finish, this worker's
                        // otherwise-exclusive pool can be recorded safely here.
                        record_on_render_thread[worker] = true;
                        completion.count_down();
                    }
                }
                completion.wait();

                for (uint32_t worker = 0; worker < worker_count; ++worker)
                {
                    if (!record_on_render_thread[worker])
                        continue;
                    RenderGraphSecondaryRecordTask fallback = {.Graph = this, .Level = &level, .Worker = worker, .FrameIndex = frame_index};
                    RecordGraphSecondaryWorkerBatch(&fallback);
                }
            }
        }

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
                    RGSubresourceRange range = transfer.Range;
                    if (range.AspectMask == 0 && !ResolveSubresourceRange(resource, {}, Device, &range))
                        continue;
                    auto&                 state   = GetSubresourceState(resource.RuntimeSubresourceStates, range, resource.InitialState);
                    VkImageMemoryBarrier2 barrier = {
                        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask        = release ? state.State.Stage : VK_PIPELINE_STAGE_2_NONE,
                        .srcAccessMask       = release ? state.State.Access : 0,
                        .dstStageMask        = release ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        .dstAccessMask       = release ? 0 : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                        .oldLayout           = state.State.Layout,
                        .newLayout           = state.State.Layout,
                        .srcQueueFamilyIndex = Device->GetQueue(source.Queue).FamilyIndex,
                        .dstQueueFamilyIndex = Device->GetQueue(dest.Queue).FamilyIndex,
                        .image               = image,
                        .subresourceRange    = ToVkSubresourceRange(range)};
                    image_barriers.push(barrier);
                    if (!release)
                    {
                        state.State           = {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, state.State.Layout};
                        resource.RuntimeState = state.State;
                    }
                }
                if (!release && resource.Kind == RGResourceKind::Buffer)
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
            target->BeginDebugLabel(QueueDebugLabel(batch.Queue));
            emit_ownership_barriers(batch_index, false, target);

            const uint32_t batch_end = batch.FirstPassOrder + batch.PassCount;
            for (uint32_t i = batch.FirstPassOrder; i < batch_end; ++i)
            {
                if (i >= SortedPassIndices.size())
                    break;

                RGPass& pass = Passes[SortedPassIndices[i]];
                if (!pass.IsActive())
                    continue;

                // Every path below, including a pass skipped for missing runtime
                // objects, closes this label before advancing to the next pass.
                target->BeginDebugLabel(pass.Name);
                const uint32_t timestamp_query = timestamp_frame ? AllocateTimestampPair(*timestamp_frame, pass.Name, pass.Queue) : UINT32_MAX;
                if (timestamp_query != UINT32_MAX)
                    target->WriteTimestamp2(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamp_frame->QueryPool, timestamp_query);

                // Stamp compile-time transition intents with this frame's actual image
                // and old state. Imported resources may change backing image and layouts
                // are execution-history dependent, so neither belongs in Compile().
                Core::Containers::Array<VkMemoryBarrier2> aliasing_barriers;
                aliasing_barriers.init(scratch.Arena, 4);
                Core::Containers::Array<VkImageMemoryBarrier2> barriers;
                barriers.init(scratch.Arena, 8);
                Core::Containers::Array<VkBufferMemoryBarrier2> buffer_barriers;
                buffer_barriers.init(scratch.Arena, 8);

                for (auto& plan : pass.StreamingAcquirePlans)
                {
                    RGResource  standalone_resource = {};
                    RGResource* resource            = nullptr;
                    if (plan.ResourceIndex < Resources.size())
                        resource = &Resources[plan.ResourceIndex];
                    else
                    {
                        standalone_resource.TextureHandle = plan.Ticket.Texture;
                        resource                          = &standalone_resource;
                    }

                    RGSubresourceRange range = {};
                    if (!ResolveSubresourceRange(*resource, {}, Device, &range))
                        continue;
                    VkImage image = GetVkImage(Device, plan.Ticket.Texture);
                    if (image == VK_NULL_HANDLE)
                        continue;

                    const uint32_t consumer_family     = Device->GetQueue(pass.Queue).FamilyIndex;
                    const bool     transfers_ownership = plan.Ticket.ProducerQueueFamily != VK_QUEUE_FAMILY_IGNORED && plan.Ticket.ProducerQueueFamily != consumer_family;
                    barriers.push({
                        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
                        .srcAccessMask       = 0,
                        .dstStageMask        = plan.DestinationState.Stage,
                        .dstAccessMask       = plan.DestinationState.Access,
                        .oldLayout           = plan.Ticket.PostReleaseLayout,
                        .newLayout           = plan.Ticket.PostReleaseLayout,
                        .srcQueueFamilyIndex = transfers_ownership ? plan.Ticket.ProducerQueueFamily : VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = transfers_ownership ? consumer_family : VK_QUEUE_FAMILY_IGNORED,
                        .image               = image,
                        .subresourceRange    = ToVkSubresourceRange(range),
                    });
                    plan.BarrierRecorded = true;
                    if (plan.ResourceIndex < Resources.size())
                        Resources[plan.ResourceIndex].RuntimeState = plan.DestinationState;
                }

                for (const auto& plan : pass.AliasingBarrierPlans)
                {
                    if (plan.SourceResourceIndex >= Resources.size() || plan.DestinationResourceIndex >= Resources.size())
                        continue;

                    // The two resources are separate VkImage/VkBuffer objects, so
                    // an image or buffer barrier cannot describe this dependency.
                    // A full memory barrier orders the previous alias lifetime and
                    // this object's first use; its regular image barrier supplies
                    // oldLayout = UNDEFINED to discard the previous contents.
                    aliasing_barriers.push({
                        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                        .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                        .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                    });
                }

                for (const auto& plan : pass.BarrierPlans)
                {
                    if (plan.ResourceIndex >= Resources.size())
                        continue;

                    RGResource& res   = Resources[plan.ResourceIndex];
                    const auto& dst   = plan.DestinationState;
                    auto&       state = GetSubresourceState(res.RuntimeSubresourceStates, plan.Range, res.InitialState);
                    if (!plan.DiscardContents && !NeedsImageBarrier(state.State, dst))
                    {
                        MergeReadState(state.State, dst);
                        continue;
                    }

                    VkImageMemoryBarrier2 b = {};
                    b.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    b.oldLayout             = plan.DiscardContents ? VK_IMAGE_LAYOUT_UNDEFINED : state.State.Layout;
                    b.newLayout             = dst.Layout;
                    b.srcStageMask          = plan.DiscardContents ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : state.State.Stage;
                    b.srcAccessMask         = plan.DiscardContents ? 0 : state.State.Access;
                    b.dstStageMask          = dst.Stage;
                    b.dstAccessMask         = dst.Access;
                    b.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
                    b.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
                    b.image                 = GetVkImage(Device, res.TextureHandle);
                    b.subresourceRange      = ToVkSubresourceRange(plan.Range);
                    if (b.image == VK_NULL_HANDLE)
                        continue;

                    barriers.push(b);
                    state.State      = {dst.Stage, dst.Access, dst.Layout};
                    res.RuntimeState = state.State;
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

                if (!aliasing_barriers.empty() || !barriers.empty() || !buffer_barriers.empty())
                {
                    VkDependencyInfo dependency         = {};
                    dependency.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dependency.memoryBarrierCount       = static_cast<uint32_t>(aliasing_barriers.size());
                    dependency.pMemoryBarriers          = aliasing_barriers.data();
                    dependency.imageMemoryBarrierCount  = static_cast<uint32_t>(barriers.size());
                    dependency.pImageMemoryBarriers     = barriers.data();
                    dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(buffer_barriers.size());
                    dependency.pBufferMemoryBarriers    = buffer_barriers.data();
                    target->PipelineBarrier2(dependency);
                }

                bool query_pools_valid = true;
                for (const RGQueryWrite& write : pass.QueryWrites)
                {
                    if (GetCurrentQueryPool(this, write.Pool) == VK_NULL_HANDLE)
                    {
                        ZENGINE_CORE_ERROR("[RenderGraph] Query-writing pass '{}' has no query-pool storage for this frame", pass.Name ? pass.Name : "?")
                        query_pools_valid = false;
                        break;
                    }
                }
                for (const RGQueryHandle pool_handle : pass.QueryResets)
                {
                    const VkQueryPool pool = GetCurrentQueryPool(this, pool_handle);
                    if (pool == VK_NULL_HANDLE || pool_handle.Index >= QueryPools.size())
                    {
                        ZENGINE_CORE_ERROR("[RenderGraph] Query reset has no query-pool storage for this frame")
                        query_pools_valid = false;
                        break;
                    }
                    target->ResetQueryPool(pool, 0, QueryPools[pool_handle.Index].QueryCount);
                }
                if (!query_pools_valid)
                {
                    if (timestamp_query != UINT32_MAX)
                        target->WriteTimestamp2(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamp_frame->QueryPool, timestamp_query + 1);
                    target->EndDebugLabel();
                    continue;
                }

                if (pass.InternalOperation == RGInternalPassOperation::Readback || pass.InternalOperation == RGInternalPassOperation::QueryReadback)
                {
                    if (pass.InternalOperation == RGInternalPassOperation::Readback)
                    {
                        const RGReadbackRequest*        request     = pass.InternalRequestIndex < ReadbackRequests.size() ? &ReadbackRequests[pass.InternalRequestIndex] : nullptr;
                        const Core::Memory::BufferView* source      = request && request->Source.Valid() && request->Source.Index < Resources.size() ? Resources[request->Source.Index].Buffer : nullptr;
                        const Core::Memory::BufferView* destination = request ? ReadbackRing.GetBuffer(request->AllocationIndex) : nullptr;
                        if (!request || !source || !destination || source->Handle == VK_NULL_HANDLE || destination->Handle == VK_NULL_HANDLE)
                        {
                            ZENGINE_CORE_ERROR("[RenderGraph] Readback pass '{}' has an invalid source or staging buffer", pass.Name ? pass.Name : "?")
                        }
                        else
                        {
                            target->CopyBuffer(source->Handle, destination->Handle, request->Size, request->Offset);
                            ReadbackRequests[pass.InternalRequestIndex].Recorded = true;
                        }
                    }
                    else
                    {
                        RGQueryReadbackRequest*         request     = pass.InternalRequestIndex < QueryReadbackRequests.size() ? &QueryReadbackRequests[pass.InternalRequestIndex] : nullptr;
                        const Core::Memory::BufferView* destination = request ? ReadbackRing.GetBuffer(request->AllocationIndex) : nullptr;
                        const VkQueryPool               pool        = GetCurrentQueryPool(this, pass.InternalQueryPool);
                        if (!request || !destination || destination->Handle == VK_NULL_HANDLE || pool == VK_NULL_HANDLE || !pass.InternalQueryPool.Valid() || pass.InternalQueryPool.Index >= QueryPools.size())
                        {
                            ZENGINE_CORE_ERROR("[RenderGraph] Query readback pass has an invalid pool or staging buffer")
                        }
                        else if (request->RecordedWriterCount != request->WriterCount)
                        {
                            ZENGINE_CORE_WARN("[RenderGraph] Query readback skipped because a query-writing pass was not recorded")
                            ReadbackRing.Cancel(request->AllocationIndex);
                            request->AllocationIndex = UINT32_MAX;
                        }
                        else
                        {
                            const uint32_t query_count = QueryPools[pass.InternalQueryPool.Index].QueryCount;
                            target->CopyQueryPoolResults(pool, 0, query_count, destination->Handle, 0, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
                            request->Recorded = true;
                        }
                    }
                    if (timestamp_query != UINT32_MAX)
                        target->WriteTimestamp2(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamp_frame->QueryPool, timestamp_query + 1);
                    target->EndDebugLabel();
                    continue;
                }

                bool                query_writer_executed       = pass.QueryWrites.empty();
                const bool          is_graphics                 = IsGraphicPass(pass);
                const VkFramebuffer framebuffer                 = ResolveGraphFramebuffer(Device, pass);
                const bool          requires_legacy_framebuffer = is_graphics && !Device->PhysicalDeviceSupportDynamicRendering;
                if (!pass.Callback || (pass.RequiresRenderPass && !pass.Handle) || (requires_legacy_framebuffer && framebuffer == VK_NULL_HANDLE))
                {
                    if (timestamp_query != UINT32_MAX)
                        target->WriteTimestamp2(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamp_frame->QueryPool, timestamp_query + 1);
                    target->EndDebugLabel();
                    continue;
                }

                bool conditional_began = false;
                if (pass.Conditional.Enabled && !pass.Conditional.UsesFallback)
                {
                    const RGResourceHandle          condition = pass.Conditional.Condition;
                    const Core::Memory::BufferView* buffer    = condition.Valid() && condition.Index < Resources.size() ? Resources[condition.Index].Buffer : nullptr;
                    if (!buffer || buffer->Handle == VK_NULL_HANDLE)
                    {
                        ZENGINE_CORE_ERROR("[RenderGraph] Conditional pass '{}' has no valid condition buffer", pass.Name ? pass.Name : "?")
                        if (timestamp_query != UINT32_MAX)
                            target->WriteTimestamp2(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamp_frame->QueryPool, timestamp_query + 1);
                        target->EndDebugLabel();
                        continue;
                    }
                    target->BeginConditionalRendering(buffer->Handle, pass.Conditional.Specification.Offset, pass.Conditional.Specification.Invert);
                    conditional_began = true;
                }

                const bool secondary_was_assigned = pass.SecondaryWorker != UINT32_MAX;
                if (is_graphics && pass.Callback->SupportsSecondaryRecording())
                {
                    if (Device->PhysicalDeviceSupportDynamicRendering)
                    {
                        const bool use_secondary_commands = secondary_was_assigned && pass.Secondary && pass.SecondaryRecorded;
                        const auto dynamic_rendering      = BeginGraphDynamicRendering(this, pass, target, use_secondary_commands);
                        if (dynamic_rendering.Began)
                        {
                            if (use_secondary_commands)
                            {
                                target->ExecuteSecondaryCommandBuffer(pass.Secondary);
                            }
                            else if (!secondary_was_assigned)
                            {
                                // The body-only contract works directly on the primary too.
                                // This keeps subresource attachments correct when worker pools
                                // are unavailable during startup or teardown.
                                pass.Callback->RecordDraw(Device, ResourceInspector, SceneData, pass.Handle, pass.Framebuffer, target);
                            }
                            query_writer_executed = true;
                            target->EndDynamicRendering();
                            if (dynamic_rendering.UsesSwapchain)
                                target->TransitionSwapchainImageToPresent();
                        }
                    }
                    else if (secondary_was_assigned && pass.Secondary && pass.SecondaryRecorded)
                    {
                        target->BeginRenderPass(static_cast<RenderPasses::GraphicPass*>(pass.Handle), framebuffer, true);
                        target->ExecuteSecondaryCommandBuffer(pass.Secondary);
                        target->EndRenderPass();
                    }
                    else if (!secondary_was_assigned)
                    {
                        // The legacy compatibility path retains the callback's
                        // BeginRenderPass()/EndRenderPass() ownership.
                        pass.Callback->Execute(Device, ResourceInspector, SceneData, pass.Handle, pass.Framebuffer, target);
                        query_writer_executed = true;
                    }
                }
                else if (pass.Handle && pass.Handle->Specification.Type == Specifications::RenderPassType::COMPUTE && secondary_was_assigned)
                {
                    if (pass.Secondary && pass.SecondaryRecorded)
                        target->ExecuteSecondaryCommandBuffer(pass.Secondary);
                }
                else
                {
                    pass.Callback->Execute(Device, ResourceInspector, SceneData, pass.Handle, pass.Framebuffer, target);
                    query_writer_executed = true;
                }
                if (conditional_began)
                    target->EndConditionalRendering();
                if (query_writer_executed && !pass.QueryWrites.empty())
                    MarkQueryWritesRecorded(pass);
                if (timestamp_query != UINT32_MAX)
                    target->WriteTimestamp2(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamp_frame->QueryPool, timestamp_query + 1);
                target->EndDebugLabel();
            }
            emit_ownership_barriers(batch_index, true, target);
            target->EndDebugLabel();
        };

        bool graph_submission_failed = false;
        for (uint32_t batch_index = 0; batch_index < QueueBatches.size(); ++batch_index)
        {
            const auto&               batch              = QueueBatches[batch_index];
            const bool                retain_for_overlay = batch_index == final_batch_index && batch.Queue == Rendering::QueueType::GRAPHIC_QUEUE;
            Hardwares::CommandBuffer* target             = retain_for_overlay ? cb : Device->CommandBufferMgr->GetGraphBatchCommandBuffer(batch.Queue, frame_index, 0, static_cast<uint8_t>(batch_index), true);

            record_batch(batch_index, target);

            if (retain_for_overlay)
            {
                for (uint32_t order_index = batch.FirstPassOrder; order_index < batch.FirstPassOrder + batch.PassCount && order_index < SortedPassIndices.size(); ++order_index)
                {
                    const RGPass& pass = Passes[SortedPassIndices[order_index]];
                    for (const auto& plan : pass.StreamingAcquirePlans)
                        if (plan.BarrierRecorded)
                            Device->SwapchainPtr->FrameAsyncOperations.push({plan.DestinationState.Stage, plan.Ticket.CompletionValue, plan.Ticket.CompletionTimeline});
                }
                // This command buffer is submitted by Present(). Do not retire its
                // tickets until vkQueueSubmit2 has accepted that submission.
                Device->SwapchainPtr->EnqueueRenderWorkSubmittedCallback(&RenderGraph::OnRenderWorkSubmitted, this);
                continue;
            }

            target->End();

            Array<VkSemaphoreSubmitInfo> wait_infos;
            wait_infos.init(scratch.Arena, external_async_operation_count + QueueDependencies.size() + StreamingUploadTickets.size());
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
            for (uint32_t order_index = batch.FirstPassOrder; order_index < batch.FirstPassOrder + batch.PassCount && order_index < SortedPassIndices.size(); ++order_index)
            {
                const RGPass& pass = Passes[SortedPassIndices[order_index]];
                for (const auto& plan : pass.StreamingAcquirePlans)
                    if (plan.BarrierRecorded)
                        add_wait(wait_infos, plan.Ticket.CompletionTimeline, plan.Ticket.CompletionValue, plan.DestinationState.Stage);
            }

            const uint32_t timeline_index = static_cast<uint32_t>(batch.Queue);
            ZENGINE_VALIDATE_ASSERT(timeline_index < QueueTimelineCount, "Invalid render graph queue type")
            auto* const    timeline     = QueueTimelines[timeline_index];
            const uint64_t signal_value = ++QueueTimelineValues[timeline_index];
            if (!Device->QueueSubmit(target, timeline, signal_value, wait_infos.data(), static_cast<uint32_t>(wait_infos.size())))
            {
                graph_submission_failed = true;
                break;
            }

            batch_timelines[batch_index]     = timeline;
            batch_signal_values[batch_index] = signal_value;
            AcknowledgeStreamingAcquires(batch.FirstPassOrder, batch.PassCount);
            SubmitReadbacks(batch.FirstPassOrder, batch.PassCount, timeline, signal_value);
            // RenderGraph and DeviceSwapchain run on the render thread. Appending
            // directly avoids re-draining the MPSC producer queue while ensuring
            // Present waits on every independently-signalled queue timeline.
            Device->SwapchainPtr->FrameAsyncOperations.push({VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, signal_value, timeline});
        }

        if (timestamp_frame)
            timestamp_frame->Submitted = !graph_submission_failed && timestamp_frame->QueryCount > 0;

        if (graph_submission_failed)
            CancelUnsubmittedReadbacks();

        ZReleaseScratch(scratch);
        return cb;
    }

    void RenderGraph::Resize(uint32_t width, uint32_t height)
    {
        // A minimized or not-yet-laid-out viewport has no valid Vulkan extent.
        // Keep the previous render targets alive until it reports a real size.
        if (width == 0 || height == 0)
            return;

        // Every physical render target and descriptor update below must outlive
        // every graph batch that used the old image. Graph batches may run on
        // graphics, compute, or transfer queues, so waiting only for the current
        // swapchain fence is insufficient. Resize is a deliberately slow path:
        // drain all device queues before replacing the backing images and views.
        if (width == RenderWidth && height == RenderHeight)
            return;
        if (Device->IsDeviceLost.load(std::memory_order_acquire))
            return;
        const VkResult idle_result = vkDeviceWaitIdle(Device->LogicalDevice);
        if (Device->CheckDeviceLost(idle_result, "RenderGraph resize"))
            return;
        ZENGINE_VALIDATE_ASSERT(idle_result == VK_SUCCESS, "RenderGraph resize failed to wait for the Vulkan device")

        RenderWidth                = width;
        RenderHeight               = height;

        // Phase 1 — collect old framebuffer handles. Enqueue them before image
        // views so deferred destruction preserves Vulkan's framebuffer-before-view
        // lifetime order. No Vulkan handle is destroyed during Resize().
        VkFramebuffer old_fbs[16]  = {};
        uint32_t      old_fb_count = 0;

        for (auto& pass : PersistentPasses)
        {
            if (pass.Framebuffer && pass.Framebuffer->Handle)
            {
                if (old_fb_count < 16)
                    old_fbs[old_fb_count++] = pass.Framebuffer->Handle;
                pass.Framebuffer->Handle = VK_NULL_HANDLE;
            }
        }

        for (uint32_t i = 0; i < old_fb_count; ++i)
        {
            Hardwares::DeferredFreeEntry e = {};
            e.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
            e.Data.Vk                      = {reinterpret_cast<void*>(old_fbs[i]), Rendering::DeviceResourceType::FRAMEBUFFER, nullptr};
            Device->DeferFree(e);
        }

        // Reconstruct each physical slot exactly once. A slot may back several
        // non-overlapping virtual resources, so iterating Resources would recreate
        // the same image repeatedly and lose transient-pool reuse after a resize.
        for (auto& slot : TransientPool.Slots)
        {
            if (!slot.Handle.Valid())
                continue;
            slot.Spec.Width  = width;
            slot.Spec.Height = height;
            Device->ReconstructTexture(slot.Handle, slot.Spec);
            for (auto& alias : slot.Aliases)
            {
                if (alias.Handle.Index == slot.Handle.Index && alias.Handle.Generation == slot.Handle.Generation)
                    continue;
                alias.Spec.Width  = width;
                alias.Spec.Height = height;
                Device->ReconstructAliasingTexture(alias.Handle, alias.Spec, slot.Handle);
            }
        }

        // Phase 2 — rebuild framebuffers and re-bind descriptors with new ImageBuffers.
        // All TextureHandles remain valid (in-place swap) so AllocateTransientResources
        // is a no-op for existing resources; call it only for safety (skips valid handles).

        if (const auto* idx = ResourceIndex.find(RendererResourceName::FrameColorRenderTargetName))
            Device->RequestDescriptorUpdate(Resources[*idx].TextureHandle);

        // Dynamic-rendering attachment lists point at the reconstructed image
        // views. Legacy attachment compatibility remains stable across resize.
        for (auto& pass : Passes)
            SynchronizeCompiledPassResources(pass);

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
    }

    void RenderGraph::Dispose()
    {
        ReadbackRing.Dispose();
        DisposeQueryPools();
        DisposeTimestampFrames();

        for (uint32_t i = 0; i < QueueTimelineCount; ++i)
        {
            if (QueueTimelines[i])
            {
                QueueTimelines[i]->~Semaphore();
                QueueTimelines[i] = nullptr;
            }
        }

        // Aliased objects must retire before their backing allocation. The root
        // image/buffer owns VMA memory; every other object only owns its Vulkan handle.
        for (auto& slot : TransientPool.Slots)
        {
            for (uint32_t alias_index = static_cast<uint32_t>(slot.Aliases.size()); alias_index > 0; --alias_index)
            {
                const auto& alias = slot.Aliases[alias_index - 1];
                if (alias.Handle.Index == slot.Handle.Index && alias.Handle.Generation == slot.Handle.Generation)
                    continue;
                Device->DestroyTexture(alias.Handle);
            }
            Device->DestroyTexture(slot.Handle);
        }

        for (auto& slot : TransientBufferPool.Slots)
        {
            for (uint32_t alias_index = static_cast<uint32_t>(slot.Aliases.size()); alias_index > 0; --alias_index)
            {
                const auto* const alias = &slot.Aliases[alias_index - 1];
                if (alias->Buffer == slot.Buffer || !alias->Buffer || alias->Buffer->Handle == VK_NULL_HANDLE)
                    continue;
                Hardwares::DeferredFreeEntry entry = {};
                entry.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::Buffer;
                entry.Data.Buffer                  = *alias->Buffer;
                Device->DeferFree(entry);
                *alias->Buffer = {};
            }

            if (slot.Buffer && slot.Buffer->Handle != VK_NULL_HANDLE)
            {
                Hardwares::DeferredFreeEntry entry = {};
                entry.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::Buffer;
                entry.Data.Buffer                  = *slot.Buffer;
                Device->DeferFree(entry);
                *slot.Buffer = {};
            }
        }

        for (auto& pass : PersistentPasses)
        {
            if (pass.Callback)
                pass.Callback->Deinitialize(Device);
        }

        FrameArena.Shutdown();
    }

    RGResourceHandle RenderGraph::ImportRenderTarget(cstring name, Textures::TextureHandle handle)
    {
        return SetImportedResource({.Name = name, .Kind = RGResourceKind::Attachment, .TextureHandle = handle});
    }

    RGResourceHandle RenderGraph::ImportTexture(cstring name, Textures::TextureHandle handle, VkImageLayout initial_layout)
    {
        const RGResourceState initial_state = initial_layout == VK_IMAGE_LAYOUT_UNDEFINED ? RGResourceState{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED} : RGResourceState{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, initial_layout};
        return SetImportedResource({.Name = name, .Kind = RGResourceKind::Texture, .TextureHandle = handle, .InitialState = initial_state});
    }

    RGResourceHandle RenderGraph::ImportStreamingTexture(cstring name, const Hardwares::StreamingUploadTicket& ticket)
    {
        if (!ticket.Texture.Valid() || !ticket.CompletionTimeline || ticket.CompletionValue == 0 || ticket.PostReleaseLayout == VK_IMAGE_LAYOUT_UNDEFINED)
            return {};

        // The ticket's producer performed the TRANSFER_DST -> final-layout release.
        // The graph emits the matching acquire immediately before the first consumer.
        return SetImportedResource({
            .Name               = name,
            .Kind               = RGResourceKind::Texture,
            .TextureHandle      = ticket.Texture,
            .InitialState       = {VK_PIPELINE_STAGE_2_NONE, 0, ticket.PostReleaseLayout},
            .HasStreamingTicket = true,
            .StreamingTicket    = ticket,
        });
    }

    RGResourceHandle RenderGraph::ImportBuffer(cstring name, const Core::Memory::BufferView* buffer)
    {
        return SetImportedResource({
            .Name = name, .Kind = RGResourceKind::Buffer, .Buffer = buffer, .InitialState = {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
        });
    }

    bool RenderGraph::UpdateImportedBuffer(cstring name, const Core::Memory::BufferView* buffer)
    {
        if (!buffer || !*buffer)
            return false;

        if (const auto* persistent_index = ImportedResourceIndex.find(name))
        {
            RGImportedResource& persistent = ImportedResources[*persistent_index];
            if (persistent.Kind != RGResourceKind::Buffer)
                return false;
            persistent.Buffer = buffer;
            if (const auto* frame_index = ResourceIndex.find(name))
            {
                Resources[*frame_index].Buffer      = buffer;
                Resources[*frame_index].BufferSize  = buffer->Size;
                Resources[*frame_index].BufferUsage = buffer->Usage;
            }
            return true;
        }

        // Supports isolated topology tests that construct Resources directly.
        const auto* frame_index = ResourceIndex.find(name);
        if (!frame_index || *frame_index >= Resources.size() || Resources[*frame_index].Kind != RGResourceKind::Buffer || !Resources[*frame_index].External)
            return false;
        Resources[*frame_index].Buffer      = buffer;
        Resources[*frame_index].BufferSize  = buffer->Size;
        Resources[*frame_index].BufferUsage = buffer->Usage;
        return true;
    }

    const RGTransientStatistics& RenderGraph::GetTransientStatistics() const
    {
        return TransientStatistics;
    }

    const Core::Containers::Array<RGPassTiming>& RenderGraph::GetLatestPassTimings() const
    {
        return LatestPassTimings;
    }

    bool RenderGraph::IsTimestampProfilingEnabled() const
    {
        return TimestampProfilingEnabled;
    }

    void RenderGraph::WriteDebugDump(Core::Containers::String& output, RGDebugDumpFormat format) const
    {
        output.clear();
        auto append_pass_id = [&](uint32_t pass_index) {
            output.append("p");
            AppendUnsigned(output, pass_index);
        };
        auto append_resource_id = [&](uint32_t resource_index) {
            output.append("r");
            AppendUnsigned(output, resource_index);
        };
        auto append_json_string = [&](cstring value) {
            output.append("\"");
            AppendEscaped(output, value);
            output.append("\"");
        };
        auto append_first_pass = [&](uint32_t first_pass) {
            if (first_pass == UINT32_MAX)
                output.append("null");
            else
                AppendUnsigned(output, first_pass);
        };

        if (format == RGDebugDumpFormat::Dot)
        {
            output.append("digraph RenderGraph {\n  rankdir=LR;\n  graph [fontname=\"Helvetica\", label=\"RenderGraph\\nvirtual bytes: ");
            AppendUnsigned(output, TransientStatistics.VirtualImageBytes + TransientStatistics.VirtualBufferBytes);
            output.append("\\nphysical bytes: ");
            AppendUnsigned(output, TransientStatistics.PhysicalImageBytes + TransientStatistics.PhysicalBufferBytes);
            output.append("\\nalias savings: ");
            AppendUnsigned(output, TransientStatistics.EstimatedAliasingSavings());
            output.append("\\npeak heap pressure: ");
            AppendFloat(output, TransientStatistics.PeakHeapPressure);
            output.append("\\ntimestamp profiling: ");
            output.append(TimestampProfilingEnabled ? "enabled" : "disabled");
            output.append("\\ncompleted pass timings: ");
            AppendUnsigned(output, LatestPassTimings.size());
            output.append("\"];\n");

            for (uint32_t resource_index = 0; resource_index < Resources.size(); ++resource_index)
            {
                const auto& resource = Resources[resource_index];
                output.append("  ");
                append_resource_id(resource_index);
                output.append(" [shape=ellipse, label=\"");
                AppendEscaped(output, resource.Name);
                output.append("\\n");
                output.append(ResourceKindName(resource.Kind));
                output.append("\\nlifetime: ");
                append_first_pass(resource.FirstPassIndex);
                output.append("..");
                AppendUnsigned(output, resource.LastPassIndex);
                if (resource.External)
                    output.append("\\nexternal");
                output.append("\"];\n");
            }

            for (uint32_t pass_index = 0; pass_index < Passes.size(); ++pass_index)
            {
                const auto& pass = Passes[pass_index];
                output.append("  ");
                append_pass_id(pass_index);
                output.append(" [shape=box, style=filled, fillcolor=\"");
                output.append(QueueColor(pass.Queue));
                output.append("\", label=\"");
                AppendEscaped(output, pass.Name);
                output.append("\\nqueue: ");
                output.append(QueueName(pass.Queue));
                output.append("\\nbarriers: ");
                AppendUnsigned(output, pass.BarrierPlans.size() + pass.BufferBarrierPlans.size() + pass.AliasingBarrierPlans.size());
                if (!pass.IsActive())
                    output.append("\\ninactive");
                output.append("\"];\n");

                for (const auto& read : pass.Reads)
                {
                    if (!read.Handle.Valid() || read.Handle.Index >= Resources.size())
                        continue;
                    output.append("  ");
                    append_resource_id(read.Handle.Index);
                    output.append(" -> ");
                    append_pass_id(pass_index);
                    output.append(" [label=\"read v");
                    AppendUnsigned(output, read.Handle.Version);
                    output.append("\"];\n");
                }
                for (const auto& write : pass.Writes)
                {
                    if (!write.Handle.Valid() || write.Handle.Index >= Resources.size())
                        continue;
                    output.append("  ");
                    append_pass_id(pass_index);
                    output.append(" -> ");
                    append_resource_id(write.Handle.Index);
                    output.append(" [label=\"write v");
                    AppendUnsigned(output, write.Handle.Version);
                    output.append("\"];\n");
                }
            }

            for (const auto& dependency : PassDependencies)
            {
                if (dependency.From >= Passes.size() || dependency.To >= Passes.size())
                    continue;
                output.append("  ");
                append_pass_id(dependency.From);
                output.append(" -> ");
                append_pass_id(dependency.To);
                output.append(" [style=dashed, color=\"#636363\", constraint=false];\n");
            }

            for (uint32_t batch_index = 0; batch_index < QueueBatches.size(); ++batch_index)
            {
                const auto& batch = QueueBatches[batch_index];
                output.append("  subgraph cluster_batch_");
                AppendUnsigned(output, batch_index);
                output.append(" { label=\"batch ");
                AppendUnsigned(output, batch_index);
                output.append(" / ");
                output.append(QueueName(batch.Queue));
                output.append("\"; color=\"#bdbdbd\";\n");
                for (uint32_t offset = 0; offset < batch.PassCount; ++offset)
                {
                    const uint32_t order_index = batch.FirstPassOrder + offset;
                    if (order_index >= SortedPassIndices.size())
                        continue;
                    output.append("    ");
                    append_pass_id(SortedPassIndices[order_index]);
                    output.append(";\n");
                }
                output.append("  }\n");
            }
            output.append("}\n");
            return;
        }

        output.append("{\"statistics\":{\"image_backings\":");
        AppendUnsigned(output, TransientStatistics.ImageBackingAllocationCount);
        output.append(",\"image_alias_objects\":");
        AppendUnsigned(output, TransientStatistics.ImageAliasObjectCount);
        output.append(",\"buffer_backings\":");
        AppendUnsigned(output, TransientStatistics.BufferBackingAllocationCount);
        output.append(",\"buffer_alias_objects\":");
        AppendUnsigned(output, TransientStatistics.BufferAliasObjectCount);
        output.append(",\"virtual_bytes\":");
        AppendUnsigned(output, TransientStatistics.VirtualImageBytes + TransientStatistics.VirtualBufferBytes);
        output.append(",\"physical_bytes\":");
        AppendUnsigned(output, TransientStatistics.PhysicalImageBytes + TransientStatistics.PhysicalBufferBytes);
        output.append(",\"aliasing_savings\":");
        AppendUnsigned(output, TransientStatistics.EstimatedAliasingSavings());
        output.append(",\"peak_heap_pressure\":");
        AppendFloat(output, TransientStatistics.PeakHeapPressure);
        output.append(",\"timestamp_profiling\":");
        output.append(TimestampProfilingEnabled ? "true" : "false");
        output.append("},\"resources\":[");
        for (uint32_t resource_index = 0; resource_index < Resources.size(); ++resource_index)
        {
            const auto& resource = Resources[resource_index];
            if (resource_index > 0)
                output.append(",");
            output.append("{\"index\":");
            AppendUnsigned(output, resource_index);
            output.append(",\"name\":");
            append_json_string(resource.Name);
            output.append(",\"kind\":");
            append_json_string(ResourceKindName(resource.Kind));
            output.append(",\"external\":");
            output.append(resource.External ? "true" : "false");
            output.append(",\"first_pass\":");
            append_first_pass(resource.FirstPassIndex);
            output.append(",\"last_pass\":");
            AppendUnsigned(output, resource.LastPassIndex);
            output.append("}");
        }
        output.append("],\"passes\":[");
        for (uint32_t pass_index = 0; pass_index < Passes.size(); ++pass_index)
        {
            const auto& pass = Passes[pass_index];
            if (pass_index > 0)
                output.append(",");
            output.append("{\"index\":");
            AppendUnsigned(output, pass_index);
            output.append(",\"name\":");
            append_json_string(pass.Name);
            output.append(",\"active\":");
            output.append(pass.IsActive() ? "true" : "false");
            output.append(",\"queue\":");
            append_json_string(QueueName(pass.Queue));
            output.append(",\"reads\":");
            AppendUnsigned(output, pass.Reads.size());
            output.append(",\"writes\":");
            AppendUnsigned(output, pass.Writes.size());
            output.append(",\"image_barriers\":");
            AppendUnsigned(output, pass.BarrierPlans.size());
            output.append(",\"buffer_barriers\":");
            AppendUnsigned(output, pass.BufferBarrierPlans.size());
            output.append(",\"alias_handoffs\":");
            AppendUnsigned(output, pass.AliasingBarrierPlans.size());
            output.append("}");
        }
        output.append("],\"dependencies\":[");
        for (uint32_t dependency_index = 0; dependency_index < PassDependencies.size(); ++dependency_index)
        {
            const auto& dependency = PassDependencies[dependency_index];
            if (dependency_index > 0)
                output.append(",");
            output.append("{\"from\":");
            AppendUnsigned(output, dependency.From);
            output.append(",\"to\":");
            AppendUnsigned(output, dependency.To);
            output.append("}");
        }
        output.append("],\"batches\":[");
        for (uint32_t batch_index = 0; batch_index < QueueBatches.size(); ++batch_index)
        {
            const auto& batch = QueueBatches[batch_index];
            if (batch_index > 0)
                output.append(",");
            output.append("{\"index\":");
            AppendUnsigned(output, batch_index);
            output.append(",\"queue\":");
            append_json_string(QueueName(batch.Queue));
            output.append(",\"first_pass_order\":");
            AppendUnsigned(output, batch.FirstPassOrder);
            output.append(",\"pass_count\":");
            AppendUnsigned(output, batch.PassCount);
            output.append("}");
        }
        output.append("],\"timings\":[");
        for (uint32_t timing_index = 0; timing_index < LatestPassTimings.size(); ++timing_index)
        {
            const auto& timing = LatestPassTimings[timing_index];
            if (timing_index > 0)
                output.append(",");
            output.append("{\"name\":");
            append_json_string(timing.Name);
            output.append(",\"queue\":");
            append_json_string(QueueName(timing.Queue));
            output.append(",\"begin\":");
            AppendUnsigned(output, timing.BeginTimestamp);
            output.append(",\"end\":");
            AppendUnsigned(output, timing.EndTimestamp);
            output.append(",\"duration_ms\":");
            AppendFloat(output, timing.DurationMilliseconds);
            output.append(",\"available\":");
            output.append(timing.Available ? "true" : "false");
            output.append("}");
        }
        output.append("]}\n");
    }

    RGPass* RenderGraph::GetPass(cstring name)
    {
        if (auto* idx = PassIndex.find(name))
            return &Passes[*idx];
        return nullptr;
    }

    void RenderGraph::SynchronizeCompiledPassResources(RGPass& pass)
    {
        if (!Device->PhysicalDeviceSupportDynamicRendering || !pass.Handle || pass.Handle->Specification.Type != Specifications::RenderPassType::GRAPHIC)
            return;

        auto* graphic_pass  = static_cast<RenderPasses::GraphicPass*>(pass.Handle);
        auto& specification = graphic_pass->Specification;
        if (specification.SwapchainAsRenderTarget)
            return;

        // Dynamic rendering reads this list at record time. Refresh it after the
        // transient pool has rebound this frame's virtual resources; no Vulkan
        // object or pipeline is rebuilt here.
        specification.Inputs.clear();
        specification.ExternalOutputs.clear();
        specification.ExternalOutputLoadOps.clear();
        for (const auto& read : pass.Reads)
        {
            if (!read.Handle.Valid() || read.Handle.Index >= Resources.size())
                continue;
            if (read.Access != RGAccess::DepthRead && read.Access != RGAccess::DepthWrite)
                continue;
            const auto& resource = Resources[read.Handle.Index];
            if (resource.TextureHandle.Valid())
                specification.Inputs.push(resource.TextureHandle);
        }
        for (const auto& write : pass.Writes)
        {
            if (!write.Handle.Valid() || write.Handle.Index >= Resources.size())
                continue;
            const auto& resource = Resources[write.Handle.Index];
            if (!resource.TextureHandle.Valid())
                continue;
            specification.ExternalOutputs.push(resource.TextureHandle);
            specification.ExternalOutputLoadOps.push(write.LoadOp);
        }
        graphic_pass->UpdateRenderTargets();
    }

    void RenderGraph::BindDeclaredBufferResources(RGPass& pass)
    {
        if (!Device || !Device->SwapchainPtr || !Device->SwapchainPtr->CurrentFrame || !pass.Handle)
            return;

        const uint32_t frame_index = Device->SwapchainPtr->CurrentFrame->Index;
        auto           bind_use    = [&](const RGPassResource& use) {
            if (!use.BindingKey || !use.Handle.Valid() || use.Handle.Index >= Resources.size())
                return;

            const RGResource& resource = Resources[use.Handle.Index];
            if (resource.Kind != RGResourceKind::Buffer || !resource.Buffer || resource.Buffer->Handle == VK_NULL_HANDLE)
                return;

            switch (pass.Handle->Specification.Type)
            {
                case Specifications::RenderPassType::GRAPHIC:
                    static_cast<RenderPasses::GraphicPass*>(pass.Handle)->SetStorageBufferForFrame(use.BindingKey, frame_index, resource.Buffer);
                    break;
                case Specifications::RenderPassType::COMPUTE:
                    static_cast<RenderPasses::ComputePass*>(pass.Handle)->SetStorageBufferForFrame(use.BindingKey, frame_index, resource.Buffer);
                    break;
                default:
                    break;
            }
        };

        for (const RGPassResource& read : pass.Reads)
            bind_use(read);
        for (const RGPassResource& write : pass.Writes)
            bind_use(write);
    }

    bool RenderGraph::ValidateDeclarations()
    {
        auto                          scratch = ZGetScratch(Device->Arena);
        RGDeclarationValidationResult result;
        const bool                    valid = ValidatePassDeclarations(scratch.Arena, Passes, Resources, &result);
        ZReleaseScratch(scratch);

        if (valid)
        {
            for (const auto& pass : Passes)
            {
                if (!pass.Enabled)
                    continue;
                auto valid_range = [&](const RGPassResource& use) {
                    if (!use.Handle.Valid() || use.Handle.Index >= Resources.size())
                        return true;
                    const RGResource& resource = Resources[use.Handle.Index];
                    if (resource.Kind == RGResourceKind::Buffer || resource.Kind == RGResourceKind::Swapchain)
                        return true;
                    RGSubresourceRange resolved;
                    if (ResolveSubresourceRange(resource, use.Range, Device, &resolved))
                        return true;
                    ZENGINE_CORE_ERROR("[RenderGraph] Pass '{}' declares an invalid subresource range for resource '{}'", pass.Name ? pass.Name : "?", resource.Name ? resource.Name : "?")
                    return false;
                };
                for (const auto& read : pass.Reads)
                {
                    if (!valid_range(read))
                        return false;
                }
                for (const auto& write : pass.Writes)
                {
                    if (!valid_range(write))
                        return false;
                }
                for (const RGQueryWrite& query_write : pass.QueryWrites)
                {
                    if (!query_write.Pool.Valid() || query_write.Pool.Index >= QueryPools.size())
                    {
                        ZENGINE_CORE_ERROR("[RenderGraph] Pass '{}' declares an invalid query pool", pass.Name ? pass.Name : "?")
                        return false;
                    }
                    const RGQueryPoolStorage& pool = QueryPools[query_write.Pool.Index];
                    if (query_write.Count == 0 || query_write.FirstQuery >= pool.QueryCount || query_write.Count > pool.QueryCount - query_write.FirstQuery)
                    {
                        ZENGINE_CORE_ERROR("[RenderGraph] Pass '{}' declares an invalid range for query pool '{}'", pass.Name ? pass.Name : "?", pool.Name ? pool.Name : "?")
                        return false;
                    }
                }

                if (pass.Conditional.Enabled)
                {
                    const RGResourceHandle condition = pass.Conditional.Condition;
                    if (!condition.Valid() || condition.Index >= Resources.size() || Resources[condition.Index].Kind != RGResourceKind::Buffer)
                    {
                        ZENGINE_CORE_ERROR("[RenderGraph] Conditional pass '{}' has an invalid condition buffer", pass.Name ? pass.Name : "?")
                        return false;
                    }
                    if (pass.Conditional.UsesFallback)
                    {
                        if (pass.Conditional.Specification.Fallback != RGConditionalFallback::Unconditional)
                        {
                            ZENGINE_CORE_ERROR("[RenderGraph] Conditional pass '{}' requires VK_EXT_conditional_rendering but declares no fallback", pass.Name ? pass.Name : "?")
                            return false;
                        }
                    }
                    else
                    {
                        bool declared_read = false;
                        for (const RGPassResource& read : pass.Reads)
                        {
                            if (read.Access == RGAccess::ConditionalRead && read.Handle.Index == condition.Index && read.Handle.Version == condition.Version)
                            {
                                declared_read = true;
                                break;
                            }
                        }
                        if (!Device->PhysicalDeviceSupportConditionalRendering || !declared_read)
                        {
                            ZENGINE_CORE_ERROR("[RenderGraph] Conditional pass '{}' has no valid conditional-rendering declaration", pass.Name ? pass.Name : "?")
                            return false;
                        }
                    }
                }
            }

            for (const RGReadbackRequest& request : ReadbackRequests)
            {
                if (!request.Name || !request.Callback || !request.Source.Valid() || request.Source.Index >= Resources.size())
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Readback declaration is incomplete")
                    return false;
                }
                const RGResource& source = Resources[request.Source.Index];
                if (source.Kind != RGResourceKind::Buffer || request.Source.Version >= source.Versions.size())
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Readback '{}' references an invalid buffer version", request.Name)
                    return false;
                }
            }

            for (const RGQueryReadbackRequest& request : QueryReadbackRequests)
            {
                if (!request.Pool.Valid() || request.Pool.Index >= QueryPools.size() || !request.Callback)
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Query readback declaration is incomplete")
                    return false;
                }

                bool has_writer = false;
                for (const RGPass& pass : Passes)
                {
                    if (!pass.Enabled)
                        continue;
                    for (const RGQueryWrite& write : pass.QueryWrites)
                    {
                        if (write.Pool.Index == request.Pool.Index)
                        {
                            has_writer = true;
                            break;
                        }
                    }
                    if (has_writer)
                        break;
                }
                if (!has_writer)
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Query readback has no declared writer")
                    return false;
                }
            }

            for (const auto& exported : ExportedResources)
            {
                if (!exported.Handle.Valid() || exported.Handle.Index >= Resources.size() || exported.PassIndex >= Passes.size())
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Export declaration has an invalid pass or resource handle")
                    return false;
                }

                const RGResource& resource = Resources[exported.Handle.Index];
                if (exported.Handle.Version >= resource.Versions.size())
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Exported resource '{}' references undeclared version {}", resource.Name ? resource.Name : "?", exported.Handle.Version)
                    return false;
                }

                bool produced_by_exporter = false;
                for (const auto& write : Passes[exported.PassIndex].Writes)
                {
                    if (write.Handle.Index == exported.Handle.Index && write.Handle.Version == exported.Handle.Version)
                    {
                        produced_by_exporter = true;
                        break;
                    }
                }
                if (!produced_by_exporter)
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Pass '{}' exports resource '{}' version {} without producing it", Passes[exported.PassIndex].Name ? Passes[exported.PassIndex].Name : "?", resource.Name ? resource.Name : "?", exported.Handle.Version)
                    return false;
                }
            }
            return true;
        }

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

    bool RenderGraph::ValidateCallbackContracts() const
    {
        for (const RGPass& pass : Passes)
        {
            if (!pass.IsActive() || pass.QueryWrites.empty())
                continue;
            if (!IsGraphicPass(pass))
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Query-writing pass '{}' must be a graphics pass", pass.Name ? pass.Name : "?")
                return false;
            }
        }

        if (!Device->PhysicalDeviceSupportDynamicRendering)
            return true;

        for (const auto& pass : Passes)
        {
            if (!pass.IsActive() || !IsGraphicPass(pass))
                continue;

            if (pass.Callback && pass.Callback->SupportsSecondaryRecording())
                continue;

            ZENGINE_CORE_ERROR("[RenderGraph] Dynamic graphics pass '{}' must implement body-only RecordDraw() and return true from SupportsSecondaryRecording(); the graph owns attachment views and rendering scope", pass.Name ? pass.Name : "?")
            return false;
        }
        return true;
    }

    void RenderGraph::BuildLifetimes()
    {
        for (auto& res : Resources)
        {
            res.FirstPassIndex = UINT32_MAX;
            res.LastPassIndex  = 0;
            res.CurrentState   = res.InitialState;
            res.RuntimeState   = res.InitialState;
            res.CompileSubresourceStates.clear();
            res.RuntimeSubresourceStates.clear();
            for (auto& version : res.Versions)
            {
                version.FirstPassIndex = UINT32_MAX;
                version.LastPassIndex  = 0;
            }
        }

        // Indexed by position in SortedPassIndices (real execution order), not by raw
        // declaration order — BuildTopology() must run before this so transient-resource
        // lifetimes reflect when a pass actually executes, not where it was declared.
        for (uint32_t order_pos = 0; order_pos < SortedPassIndices.size(); ++order_pos)
        {
            const auto& pass            = Passes[SortedPassIndices[order_pos]];
            auto        record_lifetime = [&](const RGPassResource& use) {
                if (!use.Handle.Valid() || use.Handle.Index >= Resources.size())
                    return;
                auto& resource = Resources[use.Handle.Index];
                if (order_pos < resource.FirstPassIndex)
                    resource.FirstPassIndex = order_pos;
                if (order_pos > resource.LastPassIndex)
                    resource.LastPassIndex = order_pos;

                if (auto* version = FindResourceVersion(resource, use.Handle.Version))
                {
                    if (order_pos < version->FirstPassIndex)
                        version->FirstPassIndex = order_pos;
                    if (order_pos > version->LastPassIndex)
                        version->LastPassIndex = order_pos;
                }
            };

            for (const auto& w : pass.Writes)
            {
                record_lifetime(w);
            }
            for (const auto& r : pass.Reads)
            {
                record_lifetime(r);
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

            // Every graph-owned image can become an allocation backing image in
            // a later frame. Mark both prospective backing and alias images at
            // creation time; Vulkan requires the alias create flag before two
            // distinct images bind overlapping memory.
            if (res.Kind != RGResourceKind::Buffer)
                res.Spec.IsAliasable = true;

            if (res.Kind == RGResourceKind::Buffer)
            {
                for (const auto& pass : Passes)
                {
                    if (!pass.IsActive())
                        continue;
                    auto derive_usage = [&](const RGPassResource& use) {
                        if (!use.Handle.Valid() || use.Handle.Index != res_idx)
                            return;
                        if (use.Access == RGAccess::BufferRead || use.Access == RGAccess::BufferWrite || use.Access == RGAccess::BufferReadWrite)
                            res.BufferUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                        if (use.Access == RGAccess::IndirectRead)
                            res.BufferUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                        if (use.Access == RGAccess::ConditionalRead && Device->PhysicalDeviceSupportConditionalRendering)
                            res.BufferUsage |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
                        if (use.Access == RGAccess::TransferRead)
                            res.BufferUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                        if (use.Access == RGAccess::TransferWrite)
                            res.BufferUsage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                    };
                    for (const auto& read : pass.Reads)
                        derive_usage(read);
                    for (const auto& write : pass.Writes)
                        derive_usage(write);
                }

                if (res.Buffer || res.BufferSize == 0 || res.BufferUsage == 0)
                    continue;
                if (auto* reused = TransientBufferPool.FindNamedAlias(res.Name, res.BufferSize, res.BufferUsage, res.FirstPassIndex, res.LastPassIndex))
                {
                    res.Buffer = reused;
                }
                else
                {
                    auto* buffer = ZPushStruct(Device->Arena, Core::Memory::BufferView);
                    if (auto* backing = TransientBufferPool.FindAliasingSlot(res.BufferSize, res.BufferUsage, res.FirstPassIndex, res.LastPassIndex))
                    {
                        *buffer = Device->CreateAliasingBuffer(*backing->Buffer, res.BufferSize, res.BufferUsage, res.Name);
                        if (*buffer)
                        {
                            res.Buffer = buffer;
                            TransientBufferPool.RegisterAlias(backing, res.Name, buffer, res.BufferSize, res.BufferUsage, res.FirstPassIndex, res.LastPassIndex);
                        }
                    }
                    if (!res.Buffer)
                    {
                        *buffer    = Device->CreateBuffer(res.BufferSize, res.BufferUsage, Core::Memory::GpuMemoryDomain::DeviceGeometry, res.Name);
                        res.Buffer = buffer;
                        TransientBufferPool.Register(buffer, res.BufferSize, res.BufferUsage, res.LastPassIndex);
                        TransientBufferPool.RegisterAlias(&TransientBufferPool.Slots.back(), res.Name, buffer, res.BufferSize, res.BufferUsage, res.FirstPassIndex, res.LastPassIndex);
                    }
                }
                continue;
            }

            for (const auto& pass : Passes)
            {
                if (!pass.IsActive())
                    continue;
                auto derive_usage = [&](const RGPassResource& use) {
                    if (!use.Handle.Valid() || use.Handle.Index != res_idx)
                        return;
                    if (use.Access == RGAccess::ShaderRead || use.Access == RGAccess::ShaderReadWrite)
                        res.Spec.IsUsageSampled = true;
                    if (use.Access == RGAccess::ShaderReadWrite || use.Access == RGAccess::StorageWrite)
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
            if (auto existing = TransientPool.FindNamedAlias(res.Name, res.Spec, res.FirstPassIndex, res.LastPassIndex); existing.Valid())
            {
                res.TextureHandle = existing;
            }
            else
            {
                if (auto* backing = TransientPool.FindAliasingSlot(res.Spec, res.FirstPassIndex, res.LastPassIndex))
                {
                    res.TextureHandle = Device->CreateAliasingTexture(res.Spec, backing->Handle, res.Name);
                    if (res.TextureHandle.Valid())
                        TransientPool.RegisterAlias(backing, res.Name, res.TextureHandle, res.Spec, res.FirstPassIndex, res.LastPassIndex);
                }
                if (!res.TextureHandle.Valid())
                {
                    res.TextureHandle = Device->CreateTexture(res.Spec, res.Name);
                    if (res.TextureHandle.Valid())
                    {
                        TransientPool.Register(res.TextureHandle, res.Spec, res.LastPassIndex);
                        TransientPool.RegisterAlias(&TransientPool.Slots.back(), res.Name, res.TextureHandle, res.Spec, res.FirstPassIndex, res.LastPassIndex);
                    }
                }
            }
        }
    }

    void RenderGraph::UpdateTransientStatistics()
    {
        TransientStatistics = {};

        auto image_bytes    = [&](Textures::TextureHandle handle) {
            const auto* texture = Device->GlobalTextures.Access(handle);
            return texture ? texture->BufferSize : VkDeviceSize{0};
        };
        auto image_allocation_bytes = [&](Textures::TextureHandle handle) {
            const auto* texture = Device->GlobalTextures.Access(handle);
            const auto* image   = texture ? Device->ImageBufferManager.Access(texture->BufferHandle) : nullptr;
            if (!image)
                return VkDeviceSize{0};
            const VkDeviceSize allocation_bytes = Device->GpuMem.GetAllocationSize(image->GetBuffer().Allocation);
            return allocation_bytes > 0 ? allocation_bytes : texture->BufferSize;
        };

        for (const auto& slot : TransientPool.Slots)
        {
            bool active_this_frame = false;
            for (const auto& alias : slot.Aliases)
            {
                if (!alias.Active)
                    continue;
                active_this_frame                      = true;
                TransientStatistics.VirtualImageBytes += image_bytes(alias.Handle);
                if (alias.Handle.Index != slot.Handle.Index || alias.Handle.Generation != slot.Handle.Generation)
                    ++TransientStatistics.ImageAliasObjectCount;
            }
            if (!active_this_frame)
                continue;

            ++TransientStatistics.ImageBackingAllocationCount;
            TransientStatistics.PhysicalImageBytes += image_allocation_bytes(slot.Handle);
        }

        for (const auto& slot : TransientBufferPool.Slots)
        {
            bool active_this_frame = false;
            for (const auto& alias : slot.Aliases)
            {
                if (!alias.Active)
                    continue;
                active_this_frame                       = true;
                TransientStatistics.VirtualBufferBytes += alias.Size;
                if (alias.Buffer != slot.Buffer)
                    ++TransientStatistics.BufferAliasObjectCount;
            }
            if (!active_this_frame)
                continue;

            ++TransientStatistics.BufferBackingAllocationCount;
            const VkDeviceSize allocation_bytes      = slot.Buffer ? Device->GpuMem.GetAllocationSize(slot.Buffer->Allocation) : 0;
            TransientStatistics.PhysicalBufferBytes += allocation_bytes > 0 ? allocation_bytes : slot.Size;
        }

        for (uint32_t heap_index = 0; heap_index < Device->GpuMem.HeapCount; ++heap_index)
            TransientStatistics.PeakHeapPressure = std::max(TransientStatistics.PeakHeapPressure, Device->GpuMem.HeapPressure(heap_index));
    }

    void RenderGraph::InitializeTimestampFrames()
    {
        TimestampProfilingEnabled      = false;
        TimestampCapacityWarningIssued = false;

        if (!Device || !Device->SwapchainPtr || !Device->PhysicalDeviceSupportHostQueryReset || Device->PhysicalDeviceProperties.properties.limits.timestampPeriod <= 0.0f)
            return;

        bool has_timestamp_queue = false;
        for (uint32_t queue_index = 0; queue_index < QueueTimelineCount; ++queue_index)
        {
            if (Device->QueueTimestampValidBits[queue_index] > 0)
            {
                has_timestamp_queue = true;
                break;
            }
        }
        if (!has_timestamp_queue)
            return;

        const uint32_t frame_count = static_cast<uint32_t>(Device->SwapchainPtr->FrameContexts.size());
        if (frame_count == 0)
            return;

        TimestampFrames.init(Device->Arena, frame_count, frame_count);
        LatestPassTimings.init(Device->Arena, TimestampPassCapacity);

        VkQueryPoolCreateInfo create_info = {};
        create_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        create_info.queryType             = VK_QUERY_TYPE_TIMESTAMP;
        create_info.queryCount            = TimestampPassCapacity * 2;

        for (auto& frame : TimestampFrames)
        {
            frame.RecordedTimings.init(Device->Arena, TimestampPassCapacity);
            if (vkCreateQueryPool(Device->LogicalDevice, &create_info, nullptr, &frame.QueryPool) != VK_SUCCESS)
            {
                ZENGINE_CORE_WARN("[RenderGraph] Timestamp profiling is disabled because the query pool could not be created")
                for (auto& created_frame : TimestampFrames)
                {
                    if (created_frame.QueryPool != VK_NULL_HANDLE)
                    {
                        vkDestroyQueryPool(Device->LogicalDevice, created_frame.QueryPool, nullptr);
                        created_frame.QueryPool = VK_NULL_HANDLE;
                    }
                }
                TimestampFrames.clear();
                return;
            }
        }

        TimestampProfilingEnabled = true;
    }

    void RenderGraph::DisposeTimestampFrames()
    {
        for (auto& frame : TimestampFrames)
        {
            if (frame.QueryPool == VK_NULL_HANDLE)
                continue;

            Hardwares::DeferredFreeEntry entry = {};
            entry.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
            entry.Data.Vk                      = {reinterpret_cast<void*>(frame.QueryPool), Rendering::DeviceResourceType::QUERYPOOL, nullptr};
            Device->DeferFree(entry);
            frame.QueryPool = VK_NULL_HANDLE;
        }
        TimestampFrames.clear();
        LatestPassTimings.clear();
        TimestampProfilingEnabled = false;
    }

    void RenderGraph::DisposeQueryPools()
    {
        for (RGQueryPoolStorage& storage : QueryPools)
        {
            for (VkQueryPool& frame_pool : storage.FramePools)
            {
                if (frame_pool == VK_NULL_HANDLE)
                    continue;

                Hardwares::DeferredFreeEntry entry = {};
                entry.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
                entry.Data.Vk                      = {reinterpret_cast<void*>(frame_pool), Rendering::DeviceResourceType::QUERYPOOL, nullptr};
                Device->DeferFree(entry);
                frame_pool = VK_NULL_HANDLE;
            }
            storage.FramePools.clear();
        }
        QueryPools.clear();
        QueryPoolIndex.clear();
    }

    RGTimestampFrame* RenderGraph::PrepareTimestampFrame()
    {
        if (!TimestampProfilingEnabled || !Device || !Device->SwapchainPtr || !Device->SwapchainPtr->CurrentFrame)
            return nullptr;

        const uint32_t frame_index = FindCurrentFrameContextSlot(Device->SwapchainPtr);
        if (frame_index == UINT32_MAX || frame_index >= TimestampFrames.size())
            return nullptr;

        RGTimestampFrame& frame = TimestampFrames[frame_index];
        if (frame.QueryPool == VK_NULL_HANDLE)
            return nullptr;

        // AcquireNextImage() waited this exact context's fence before Execute(),
        // including the final graphics submission that waits every graph batch.
        if (frame.Submitted && frame.QueryCount > 0)
        {
            uint64_t       timestamps[TimestampPassCapacity * 2] = {};
            const VkResult result                                = vkGetQueryPoolResults(Device->LogicalDevice, frame.QueryPool, 0, frame.QueryCount, sizeof(uint64_t) * frame.QueryCount, timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
            if (result == VK_SUCCESS)
            {
                LatestPassTimings.clear();
                const uint32_t timing_count = std::min(static_cast<uint32_t>(frame.RecordedTimings.size()), frame.QueryCount / 2);
                for (uint32_t timing_index = 0; timing_index < timing_count; ++timing_index)
                {
                    RGPassTiming timing         = frame.RecordedTimings[timing_index];
                    timing.BeginTimestamp       = timestamps[timing_index * 2];
                    timing.EndTimestamp         = timestamps[timing_index * 2 + 1];
                    const uint32_t queue_index  = static_cast<uint32_t>(timing.Queue);
                    const uint32_t valid_bits   = queue_index < QueueTimelineCount ? Device->QueueTimestampValidBits[queue_index] : 0;
                    timing.DurationMilliseconds = RGTimestampDurationMilliseconds(timing.BeginTimestamp, timing.EndTimestamp, valid_bits, Device->PhysicalDeviceProperties.properties.limits.timestampPeriod);
                    timing.Available            = valid_bits > 0;
                    LatestPassTimings.push(timing);
                }
            }
            else
            {
                ZENGINE_CORE_WARN("[RenderGraph] Timestamp result retrieval failed with VkResult {}", static_cast<int32_t>(result))
            }
        }

        frame.RecordedTimings.clear();
        frame.QueryCount = 0;
        frame.Submitted  = false;
        vkResetQueryPool(Device->LogicalDevice, frame.QueryPool, 0, TimestampPassCapacity * 2);
        return &frame;
    }

    uint32_t RenderGraph::AllocateTimestampPair(RGTimestampFrame& frame, cstring pass_name, Rendering::QueueType queue)
    {
        const uint32_t queue_index = static_cast<uint32_t>(queue);
        if (queue_index >= QueueTimelineCount || Device->QueueTimestampValidBits[queue_index] == 0)
            return UINT32_MAX;

        if (frame.QueryCount + 2 > TimestampPassCapacity * 2)
        {
            if (!TimestampCapacityWarningIssued)
            {
                ZENGINE_CORE_WARN("[RenderGraph] Timestamp pass capacity ({}) reached; additional pass timings are omitted", TimestampPassCapacity)
                TimestampCapacityWarningIssued = true;
            }
            return UINT32_MAX;
        }

        const uint32_t query  = frame.QueryCount;
        frame.QueryCount     += 2;
        frame.RecordedTimings.push({.Name = pass_name, .Queue = queue});
        return query;
    }

    void RenderGraph::BuildAliasingBarriers()
    {
        for (auto& pass : Passes)
            pass.AliasingBarrierPlans.clear();

        auto find_image_resource = [&](Textures::TextureHandle handle) {
            for (uint32_t resource_index = 0; resource_index < Resources.size(); ++resource_index)
            {
                const auto& resource = Resources[resource_index];
                if (!resource.Transient || resource.External || resource.Kind == RGResourceKind::Buffer)
                    continue;
                if (resource.TextureHandle.Index == handle.Index && resource.TextureHandle.Generation == handle.Generation)
                    return resource_index;
            }
            return UINT32_MAX;
        };
        auto find_buffer_resource = [&](const Core::Memory::BufferView* buffer) {
            for (uint32_t resource_index = 0; resource_index < Resources.size(); ++resource_index)
            {
                const auto& resource = Resources[resource_index];
                if (!resource.Transient || resource.External || resource.Kind != RGResourceKind::Buffer)
                    continue;
                if (resource.Buffer == buffer)
                    return resource_index;
            }
            return UINT32_MAX;
        };
        auto add_dependency = [&](uint32_t source_resource, uint32_t destination_resource) {
            if (source_resource >= Resources.size() || destination_resource >= Resources.size())
                return;

            const RGResource& source      = Resources[source_resource];
            const RGResource& destination = Resources[destination_resource];
            if (source.LastPassIndex >= destination.FirstPassIndex || destination.FirstPassIndex >= SortedPassIndices.size())
                return;

            const uint32_t source_pass      = SortedPassIndices[source.LastPassIndex];
            const uint32_t destination_pass = SortedPassIndices[destination.FirstPassIndex];
            if (source_pass >= Passes.size() || destination_pass >= Passes.size())
                return;

            auto& destination_plans = Passes[destination_pass].AliasingBarrierPlans;
            for (const auto& plan : destination_plans)
            {
                if (plan.SourceResourceIndex == source_resource && plan.DestinationResourceIndex == destination_resource)
                    return;
            }
            destination_plans.push({.SourceResourceIndex = source_resource, .DestinationResourceIndex = destination_resource});

            for (const auto& dependency : PassDependencies)
            {
                if (dependency.From == source_pass && dependency.To == destination_pass)
                    return;
            }
            PassDependencies.push({source_pass, destination_pass});
        };

        auto add_image_aliasing_boundaries = [&](const RGTransientSlot& slot) {
            for (const auto& destination_alias : slot.Aliases)
            {
                if (!destination_alias.Active)
                    continue;

                const RGTransientSlot::Alias* source_alias = nullptr;
                for (const auto& candidate : slot.Aliases)
                {
                    if (!candidate.Active || candidate.LastPass >= destination_alias.FirstPass)
                        continue;
                    if (!source_alias || candidate.LastPass > source_alias->LastPass)
                        source_alias = &candidate;
                }
                if (!source_alias)
                    continue;

                add_dependency(find_image_resource(source_alias->Handle), find_image_resource(destination_alias.Handle));
            }
        };
        for (const auto& slot : TransientPool.Slots)
            add_image_aliasing_boundaries(slot);

        auto add_buffer_aliasing_boundaries = [&](const RGTransientBufferSlot& slot) {
            for (const auto& destination_alias : slot.Aliases)
            {
                if (!destination_alias.Active)
                    continue;

                const RGTransientBufferSlot::Alias* source_alias = nullptr;
                for (const auto& candidate : slot.Aliases)
                {
                    if (!candidate.Active || candidate.LastPass >= destination_alias.FirstPass)
                        continue;
                    if (!source_alias || candidate.LastPass > source_alias->LastPass)
                        source_alias = &candidate;
                }
                if (!source_alias)
                    continue;

                add_dependency(find_buffer_resource(source_alias->Buffer), find_buffer_resource(destination_alias.Buffer));
            }
        };
        for (const auto& slot : TransientBufferPool.Slots)
            add_buffer_aliasing_boundaries(slot);

        // Aliasing creates an execution relationship not present in the logical
        // version DAG. Rebuild only recording levels; SortedPassIndices already
        // obeys each new edge because the source lifetime ends before destination.
        auto scratch = ZGetScratch(Device->Arena);
        BuildTopologyLevels(scratch.Arena, &FrameArena, SortedPassIndices, PassDependencies, static_cast<uint32_t>(Passes.size()), TopologyLevels);
        ZReleaseScratch(scratch);
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
            if (!pass.IsActive())
                continue;

            auto emit = [&](const RGPassResource& pr) {
                if (!pr.Handle.Valid() || pr.Access == RGAccess::None)
                    return;
                RGResource&           res = Resources[pr.Handle.Index];
                const RGResourceState dst = GetPassResourceState(pr);

                // The acquired image is frame-local and CommandBuffer performs
                // its PRESENT_SRC_KHR <-> COLOR_ATTACHMENT_OPTIMAL transitions.
                // Keep this logical access for topology ordering only.
                if (res.Kind == RGResourceKind::Swapchain)
                    return;

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

                RGSubresourceRange range;
                if (!ResolveSubresourceRange(res, pr.Range, Device, &range))
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Pass '{}' declares an invalid subresource range for resource '{}'", pass.Name ? pass.Name : "?", res.Name ? res.Name : "?")
                    return;
                }

                constexpr VkImageAspectFlags aspect_bits[] = {VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_STENCIL_BIT};
                for (VkImageAspectFlags aspect : aspect_bits)
                {
                    if ((range.AspectMask & aspect) == 0)
                        continue;
                    for (uint32_t mip = range.BaseMipLevel; mip < range.BaseMipLevel + range.LevelCount; ++mip)
                    {
                        for (uint32_t layer = range.BaseArrayLayer; layer < range.BaseArrayLayer + range.LayerCount; ++layer)
                        {
                            const RGSubresourceRange cell      = {.AspectMask = aspect, .BaseMipLevel = mip, .LevelCount = 1, .BaseArrayLayer = layer, .LayerCount = 1};
                            const bool               first_use = FindSubresourceState(res.CompileSubresourceStates, cell) == nullptr;
                            auto&                    state     = GetSubresourceState(res.CompileSubresourceStates, cell, res.InitialState);
                            // First use of graph-owned transient storage discards
                            // the old alias owner's contents for this exact cell.
                            const bool               discard   = res.Transient && !res.External && first_use;
                            if (!discard && !NeedsImageBarrier(state.State, dst))
                            {
                                MergeReadState(state.State, dst);
                                continue;
                            }

                            pass.BarrierPlans.push({.ResourceIndex = pr.Handle.Index, .DestinationState = dst, .DiscardContents = discard, .Range = cell});
                            state.State      = dst;
                            res.CurrentState = dst;
                        }
                    }
                }
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
            const bool        valid_import = resource.Kind == RGResourceKind::Swapchain || (resource.Kind == RGResourceKind::Buffer ? resource.Buffer && resource.Buffer->Handle != VK_NULL_HANDLE : resource.TextureHandle.Valid());
            if (entry.Version == 0 && (!resource.External || !valid_import))
                return fail(RGDeclarationError::MissingImport, entry.ResourceIndex, entry.Version, UINT32_MAX);
        }
        return true;
    }

    bool BuildPassTopology(Core::Memory::ArenaAllocator* scratch_arena, ArrayView<RGPass> passes, Array<uint32_t>& out_order, uint32_t* out_cycle_pass_index, Array<RGPassDependency>* out_dependencies)
    {
        out_order.clear();
        if (out_dependencies)
            out_dependencies->clear();
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
            enabled_pass_count += passes[i].IsActive() ? 1u : 0u;
        }
        if (enabled_pass_count == 0)
        {
            return true;
        }

        // Flatten every Read/Write into one event log — reads before writes within each
        // pass, passes in declaration order. The arena-backed stable merge sort below
        // groups each producer with precisely the readers it satisfies.
        uint32_t total_events = 0;
        for (uint32_t i = 0; i < pass_count; ++i)
        {
            if (!passes[i].IsActive())
                continue;
            total_events += static_cast<uint32_t>(passes[i].Reads.size() + passes[i].Writes.size());
        }

        Array<RGEvent> events;
        events.init(scratch_arena, total_events > 0 ? total_events : 1);
        for (uint32_t i = 0; i < pass_count; ++i)
        {
            const auto& pass = passes[i];
            if (!pass.IsActive())
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
        if (events.size() > 1)
        {
            Array<RGEvent> sorted_events;
            sorted_events.init(scratch_arena, events.size(), events.size());

            auto event_precedes = [](const RGEvent& left, const RGEvent& right) { return left.ResourceIndex != right.ResourceIndex ? left.ResourceIndex < right.ResourceIndex : left.Version < right.Version; };

            for (uint32_t width = 1; width < events.size();)
            {
                for (uint32_t first = 0; first < events.size();)
                {
                    const uint32_t middle = std::min(first + width, static_cast<uint32_t>(events.size()));
                    const uint32_t last   = std::min(middle + width, static_cast<uint32_t>(events.size()));
                    uint32_t       left   = first;
                    uint32_t       right  = middle;
                    uint32_t       output = first;

                    while (left < middle && right < last)
                        sorted_events[output++] = event_precedes(events[right], events[left]) ? events[right++] : events[left++];
                    while (left < middle)
                        sorted_events[output++] = events[left++];
                    while (right < last)
                        sorted_events[output++] = events[right++];

                    first = last;
                }

                for (uint32_t index = 0; index < events.size(); ++index)
                    events[index] = sorted_events[index];

                if (width > events.size() / 2)
                    break;
                width *= 2;
            }
        }

        Array<uint32_t> indegree;
        indegree.init(scratch_arena, pass_count, pass_count);
        for (uint32_t i = 0; i < pass_count; ++i)
        {
            indegree[i] = 0;
        }

        Array<RGPassDependency> edges;
        edges.init(scratch_arena, events.size() * 2 + 1);

        auto add_edge = [&](uint32_t from, uint32_t to) {
            if (from == to || from >= pass_count || to >= pass_count)
                return;
            for (const auto& edge : edges)
            {
                if (edge.From == from && edge.To == to)
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

        // A later logical version currently shares the same physical allocation.
        // Preserve the required write-after-write and write-after-read hazards
        // until the transient allocator can give overlapping versions distinct
        // storage. This is version-chain ordering, never declaration ordering.
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

                for (uint32_t event = start; event < resource_idx; ++event)
                {
                    if (!events[event].IsWrite)
                        prior_readers.push(events[event].PassIndex);
                }
            }
        }

        // Query pools are non-resource Vulkan objects, so their result-copy
        // pass has no RGResource read to infer dependencies from. Every
        // explicit WriteQueryPool declaration therefore feeds the matching
        // synthetic query readback. These edges participate in culling, queue
        // batches, and cross-queue timeline waits exactly like resource edges.
        for (uint32_t reader_index = 0; reader_index < pass_count; ++reader_index)
        {
            const RGPass& reader = passes[reader_index];
            if (!reader.IsActive() || reader.InternalOperation != RGInternalPassOperation::QueryReadback || !reader.InternalQueryPool.Valid())
                continue;

            for (uint32_t writer_index = 0; writer_index < pass_count; ++writer_index)
            {
                const RGPass& writer = passes[writer_index];
                if (!writer.IsActive())
                    continue;
                for (const RGQueryWrite& write : writer.QueryWrites)
                {
                    if (write.Pool.Index == reader.InternalQueryPool.Index)
                        add_edge(writer_index, reader_index);
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
                if (!passes[i].IsActive() || emitted[i] || indegree[i] != 0)
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
                        if (passes[i].IsActive() && !emitted[i])
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
        if (out_dependencies)
        {
            for (const auto& edge : edges)
                out_dependencies->push(edge);
        }
        return true;
    }

    void CullPasses(Core::Memory::ArenaAllocator* scratch_arena, ArrayView<RGPass> passes, ArrayView<RGResource> resources, ArrayView<RGPassDependency> dependencies, ArrayView<RGExportedResource> exports)
    {
        Array<bool> live;
        live.init(scratch_arena, passes.size(), passes.size());
        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
            live[pass_index] = false;

        Array<uint32_t> worklist;
        worklist.init(scratch_arena, passes.size());
        auto mark_live = [&](uint32_t pass_index) {
            if (pass_index >= passes.size() || !passes[pass_index].IsActive() || live[pass_index])
                return;
            live[pass_index] = true;
            worklist.push(pass_index);
        };

        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            const RGPass& pass = passes[pass_index];
            if (!pass.IsActive())
                continue;
            if (HasRGPassFlag(pass.Flags, RGPassFlags::NeverCull))
                mark_live(pass_index);
            for (const auto& write : pass.Writes)
            {
                if (write.Handle.Valid() && write.Handle.Index < resources.size() && resources[write.Handle.Index].Kind == RGResourceKind::Swapchain)
                    mark_live(pass_index);
            }
        }

        for (uint32_t export_index = 0; export_index < exports.size(); ++export_index)
            mark_live(exports[export_index].PassIndex);

        while (!worklist.empty())
        {
            const uint32_t consumer = worklist.back();
            worklist.pop();
            for (uint32_t dependency_index = 0; dependency_index < dependencies.size(); ++dependency_index)
            {
                const auto& dependency = dependencies[dependency_index];
                if (dependency.To == consumer)
                    mark_live(dependency.From);
            }
        }

        for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            if (passes[pass_index].Enabled)
                passes[pass_index].Culled = !live[pass_index];
        }
    }

    void BuildTopologyLevels(Core::Memory::ArenaAllocator* scratch_arena, Core::Memory::ArenaAllocator* output_arena, ArrayView<uint32_t> order, ArrayView<RGPassDependency> dependencies, uint32_t pass_count, Array<RGTopologyLevel>& out_levels)
    {
        out_levels.clear();
        if (order.size() == 0 || pass_count == 0)
            return;

        Array<uint32_t> pass_levels;
        pass_levels.init(scratch_arena, pass_count, pass_count);
        for (uint32_t i = 0; i < pass_count; ++i)
            pass_levels[i] = 0;

        for (uint32_t order_index = 0; order_index < order.size(); ++order_index)
        {
            const uint32_t pass_index = order[order_index];
            if (pass_index >= pass_count)
                continue;
            for (uint32_t dependency_index = 0; dependency_index < dependencies.size(); ++dependency_index)
            {
                const auto& dependency = dependencies[dependency_index];
                if (dependency.To == pass_index && dependency.From < pass_count)
                    pass_levels[pass_index] = std::max(pass_levels[pass_index], pass_levels[dependency.From] + 1);
            }

            while (out_levels.size() <= pass_levels[pass_index])
            {
                auto& level = out_levels.push_use({});
                level.PassIndices.init(output_arena, 8);
            }
            out_levels[pass_levels[pass_index]].PassIndices.push(pass_index);
        }
    }

    bool RenderGraph::BuildTopology()
    {
        for (auto& pass : Passes)
            pass.Culled = false;

        auto     scratch   = ZGetScratch(Device->Arena);
        uint32_t cycle_idx = UINT32_MAX;
        if (!BuildPassTopology(scratch.Arena, Passes, SortedPassIndices, &cycle_idx, &PassDependencies))
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Cycle detected in enabled resource dependency graph at pass '{}' — compile rejected", cycle_idx < Passes.size() ? Passes[cycle_idx].Name : "?")
            SortedPassIndices.clear();
            PassDependencies.clear();
            TopologyLevels.clear();
            ZReleaseScratch(scratch);
            return false;
        }

        // Derive liveness from the complete version DAG, then rebuild every
        // derived schedule from live passes only. The retained dependencies are
        // consequently also the exact graph that queue planning and recording use.
        CullPasses(scratch.Arena, Passes, Resources, PassDependencies, ExportedResources);
        if (!BuildPassTopology(scratch.Arena, Passes, SortedPassIndices, &cycle_idx, &PassDependencies))
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Cycle detected after pass culling at pass '{}' — compile rejected", cycle_idx < Passes.size() ? Passes[cycle_idx].Name : "?")
            SortedPassIndices.clear();
            PassDependencies.clear();
            TopologyLevels.clear();
            ZReleaseScratch(scratch);
            return false;
        }
        BuildTopologyLevels(scratch.Arena, &FrameArena, SortedPassIndices, PassDependencies, static_cast<uint32_t>(Passes.size()), TopologyLevels);
        ZReleaseScratch(scratch);
        return true;
    }

    void RenderGraph::BuildQueueSchedule()
    {
        for (auto& pass : Passes)
        {
            pass.RequestedQueue = (pass.InternalOperation == RGInternalPassOperation::Readback || pass.InternalOperation == RGInternalPassOperation::QueryReadback) ? Rendering::QueueType::TRANSFER_QUEUE : pass.Callback ? pass.Callback->GetRequestedQueue() : Rendering::QueueType::GRAPHIC_QUEUE;
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
            // Occlusion queries are graphics work. Do not let a callback's
            // preferred queue or a stale pass type move vkCmdBeginQuery onto
            // compute or transfer.
            if (!pass.QueryWrites.empty())
                pass.RequestedQueue = Rendering::QueueType::GRAPHIC_QUEUE;
        }
        BuildQueueBatches(Passes, SortedPassIndices, Device->HasSeparateTransferQueue, Device->HasSeparateComputeQueue, QueueBatches);
        auto scratch = ZGetScratch(Device->Arena);
        BuildQueueDependencies(scratch.Arena, Passes, SortedPassIndices, QueueBatches, PassDependencies, QueueDependencies);

        QueueOwnershipTransfers.clear();
        Array<uint32_t> batch_for_order;
        batch_for_order.init(scratch.Arena, SortedPassIndices.size(), SortedPassIndices.size());
        for (uint32_t order_index = 0; order_index < SortedPassIndices.size(); ++order_index)
            batch_for_order[order_index] = UINT32_MAX;
        for (uint32_t batch_index = 0; batch_index < QueueBatches.size(); ++batch_index)
        {
            const auto& batch = QueueBatches[batch_index];
            for (uint32_t pass_offset = 0; pass_offset < batch.PassCount; ++pass_offset)
                batch_for_order[batch.FirstPassOrder + pass_offset] = batch_index;
        }

        struct ImageLastUse
        {
            uint32_t           ResourceIndex = UINT32_MAX;
            RGSubresourceRange Range         = {};
            uint32_t           Batch         = UINT32_MAX;
        };
        Array<uint32_t> buffer_last_batch;
        buffer_last_batch.init(scratch.Arena, Resources.size(), Resources.size());
        for (uint32_t resource_index = 0; resource_index < Resources.size(); ++resource_index)
            buffer_last_batch[resource_index] = UINT32_MAX;
        Array<ImageLastUse> image_last_uses;
        image_last_uses.init(scratch.Arena, 16);

        auto record_ownership_transfer = [&](uint32_t resource_index, const RGSubresourceRange& range, uint32_t previous_batch, uint32_t current_batch) {
            if (previous_batch == UINT32_MAX || previous_batch == current_batch || QueueBatches[previous_batch].Queue == QueueBatches[current_batch].Queue)
                return;
            QueueOwnershipTransfers.push({.ResourceIndex = resource_index, .FromBatch = previous_batch, .ToBatch = current_batch, .Range = range});
        };
        auto record_access = [&](const RGPassResource& use, uint32_t current_batch) {
            if (!use.Handle.Valid() || use.Handle.Index >= Resources.size() || current_batch == UINT32_MAX)
                return;
            const uint32_t resource_index = use.Handle.Index;
            const auto&    resource       = Resources[resource_index];
            if (resource.Kind == RGResourceKind::Swapchain)
                return;
            if (resource.Kind == RGResourceKind::Buffer)
            {
                record_ownership_transfer(resource_index, {}, buffer_last_batch[resource_index], current_batch);
                buffer_last_batch[resource_index] = current_batch;
                return;
            }

            RGSubresourceRange range;
            if (!ResolveSubresourceRange(resource, use.Range, Device, &range))
                return;
            constexpr VkImageAspectFlags aspect_bits[] = {VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_STENCIL_BIT};
            for (VkImageAspectFlags aspect : aspect_bits)
            {
                if ((range.AspectMask & aspect) == 0)
                    continue;
                for (uint32_t mip = range.BaseMipLevel; mip < range.BaseMipLevel + range.LevelCount; ++mip)
                {
                    for (uint32_t layer = range.BaseArrayLayer; layer < range.BaseArrayLayer + range.LayerCount; ++layer)
                    {
                        const RGSubresourceRange cell = {.AspectMask = aspect, .BaseMipLevel = mip, .LevelCount = 1, .BaseArrayLayer = layer, .LayerCount = 1};
                        ImageLastUse*            last = nullptr;
                        for (auto& candidate : image_last_uses)
                        {
                            if (candidate.ResourceIndex == resource_index && candidate.Range.AspectMask == cell.AspectMask && candidate.Range.BaseMipLevel == cell.BaseMipLevel && candidate.Range.BaseArrayLayer == cell.BaseArrayLayer)
                            {
                                last = &candidate;
                                break;
                            }
                        }
                        if (last)
                        {
                            record_ownership_transfer(resource_index, cell, last->Batch, current_batch);
                            last->Batch = current_batch;
                        }
                        else
                        {
                            image_last_uses.push({.ResourceIndex = resource_index, .Range = cell, .Batch = current_batch});
                        }
                    }
                }
            }
        };

        for (uint32_t order_index = 0; order_index < SortedPassIndices.size(); ++order_index)
        {
            const RGPass& pass = Passes[SortedPassIndices[order_index]];
            if (!pass.IsActive())
                continue;
            const uint32_t batch_index = batch_for_order[order_index];
            for (const auto& read : pass.Reads)
                record_access(read, batch_index);
            for (const auto& write : pass.Writes)
                record_access(write, batch_index);
        }
        ZReleaseScratch(scratch);
        BuildStreamingAcquirePlans();
    }

    void RenderGraph::BuildStreamingAcquirePlans()
    {
        for (auto& pass : Passes)
            pass.StreamingAcquirePlans.clear();

        auto matches_ticket  = [](const Hardwares::StreamingUploadTicket& left, const Hardwares::StreamingUploadTicket& right) { return left.Texture.Index == right.Texture.Index && left.Texture.Generation == right.Texture.Generation && left.CompletionTimeline == right.CompletionTimeline && left.CompletionValue == right.CompletionValue; };
        auto already_planned = [&](const Hardwares::StreamingUploadTicket& ticket) {
            for (const auto& pass : Passes)
                for (const auto& plan : pass.StreamingAcquirePlans)
                    if (matches_ticket(plan.Ticket, ticket))
                        return true;
            return false;
        };
        auto plan_ticket = [&](const Hardwares::StreamingUploadTicket& ticket, uint32_t preferred_resource) {
            if (!ticket.Texture.Valid() || !ticket.CompletionTimeline || ticket.CompletionValue == 0 || ticket.PostReleaseLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                return;

            for (uint32_t order_index = 0; order_index < SortedPassIndices.size(); ++order_index)
            {
                RGPass& pass = Passes[SortedPassIndices[order_index]];
                if (!pass.IsActive())
                    continue;

                uint32_t        resource_index    = UINT32_MAX;
                RGResourceState destination_state = {};
                for (const auto& use : pass.Reads)
                {
                    if (!use.Handle.Valid() || use.Handle.Index >= Resources.size())
                        continue;
                    const RGResource& resource = Resources[use.Handle.Index];
                    if (use.Handle.Index != preferred_resource && (!resource.HasStreamingTicket || !matches_ticket(resource.StreamingTicket, ticket)))
                        continue;
                    resource_index    = use.Handle.Index;
                    destination_state = GetPassResourceState(use);
                    break;
                }
                if (resource_index == UINT32_MAX)
                {
                    for (const auto& use : pass.Writes)
                    {
                        if (!use.Handle.Valid() || use.Handle.Index >= Resources.size())
                            continue;
                        const RGResource& resource = Resources[use.Handle.Index];
                        if (use.Handle.Index != preferred_resource && (!resource.HasStreamingTicket || !matches_ticket(resource.StreamingTicket, ticket)))
                            continue;
                        resource_index    = use.Handle.Index;
                        destination_state = GetPassResourceState(use);
                        break;
                    }
                }
                if (resource_index == UINT32_MAX && pass.ReadsBindless)
                    destination_state = GetRGAccessState(RGAccess::ShaderRead);
                else if (resource_index == UINT32_MAX)
                    continue;

                pass.StreamingAcquirePlans.push({.Ticket = ticket, .ResourceIndex = resource_index, .DestinationState = destination_state});
                return;
            }
        };

        // Explicit streaming imports retain their ticket even when it predates the
        // current RRM snapshot (for example, an isolated graph integration test).
        for (uint32_t resource_index = 0; resource_index < Resources.size(); ++resource_index)
        {
            const auto& resource = Resources[resource_index];
            if (resource.HasStreamingTicket && !already_planned(resource.StreamingTicket))
                plan_ticket(resource.StreamingTicket, resource_index);
        }
        // Bindless readers acquire all other published textures before their first
        // descriptor-array access. One plan owns one ticket even when a texture is
        // also explicitly imported by another pass.
        for (const auto& ticket : StreamingUploadTickets)
            if (!already_planned(ticket))
                plan_ticket(ticket, UINT32_MAX);
    }

    void RenderGraph::AcknowledgeStreamingAcquires(uint32_t first_pass_order, uint32_t pass_count)
    {
        auto* const    rrm       = Device && Device->RRM ? static_cast<Rendering::RenderResourceManager*>(Device->RRM) : nullptr;
        const uint32_t end_order = std::min(first_pass_order + pass_count, static_cast<uint32_t>(SortedPassIndices.size()));
        for (uint32_t order_index = first_pass_order; order_index < end_order; ++order_index)
        {
            RGPass& pass = Passes[SortedPassIndices[order_index]];
            for (RGStreamingAcquirePlan& plan : pass.StreamingAcquirePlans)
            {
                if (!plan.BarrierRecorded || plan.Acknowledged)
                    continue;

                if (rrm)
                    rrm->AcknowledgeStreamingUploadTicket(plan.Ticket);
                for (RGImportedResource& imported : ImportedResources)
                {
                    const auto& ticket = imported.StreamingTicket;
                    if (!imported.HasStreamingTicket || ticket.Texture.Index != plan.Ticket.Texture.Index || ticket.Texture.Generation != plan.Ticket.Texture.Generation || ticket.CompletionTimeline != plan.Ticket.CompletionTimeline || ticket.CompletionValue != plan.Ticket.CompletionValue)
                        continue;
                    imported.HasStreamingTicket = false;
                    imported.StreamingTicket    = {};
                }
                plan.Acknowledged = true;
            }
        }
    }

    void RenderGraph::OnRenderWorkSubmitted(void* context, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value)
    {
        auto* const graph = static_cast<RenderGraph*>(context);
        if (graph)
        {
            graph->AcknowledgeStreamingAcquires(0, static_cast<uint32_t>(graph->SortedPassIndices.size()));
            graph->SubmitReadbacks(0, static_cast<uint32_t>(graph->SortedPassIndices.size()), timeline, timeline_value);
        }
    }

    void RenderGraph::AllocateFramebuffers()
    {
        for (uint32_t i = 0; i < Passes.size(); ++i)
        {
            RGPass& pass = Passes[i];
            if (!pass.IsActive() || !pass.Handle)
                continue;
            if (pass.Handle->Specification.Type == Specifications::RenderPassType::COMPUTE)
                continue;
            auto* gp = static_cast<RenderPasses::GraphicPass*>(pass.Handle);
            if (Device->PhysicalDeviceSupportDynamicRendering)
            {
                gp->UpdateRenderTargets();
                if (pass.Persistent)
                    pass.Persistent->Framebuffer = pass.Framebuffer;
                continue;
            }

            // Swapchain framebuffers are owned by DeviceSwapchain, not the graph.
            if (pass.Handle->Specification.SwapchainAsRenderTarget)
                continue;

            VkImageView view_buf[16] = {};
            uint32_t    view_count   = 0;
            uint32_t    w            = 0;
            uint32_t    h            = 0;
            uint32_t    layers       = 0;
            bool        valid_views  = true;
            auto        push_view    = [&](const RGPassResource& use) {
                if (view_count >= 16 || !use.Handle.Valid() || use.Handle.Index >= Resources.size())
                    return false;
                VkImageView  view        = VK_NULL_HANDLE;
                VkFormat     format      = VK_FORMAT_UNDEFINED;
                VkClearValue clear_value = {};
                uint32_t     view_width  = 0;
                uint32_t     view_height = 0;
                uint32_t     view_layers = 0;
                if (!GetGraphAttachmentView(this, Resources[use.Handle.Index], use, &view, &format, &view_width, &view_height, &view_layers, &clear_value) || view == VK_NULL_HANDLE)
                    return false;
                if (w != 0 && (w != view_width || h != view_height || layers != view_layers))
                    return false;
                view_buf[view_count++] = view;
                w                      = view_width;
                h                      = view_height;
                layers                 = view_layers;
                return true;
            };

            for (const auto& r : pass.Reads)
            {
                if (r.Access != RGAccess::DepthRead)
                    continue;
                if (!push_view(r))
                {
                    valid_views = false;
                    break;
                }
            }
            for (const auto& wr : pass.Writes)
            {
                if (!valid_views)
                    break;
                if (wr.Access != RGAccess::ColorWrite && wr.Access != RGAccess::ColorReadWrite && wr.Access != RGAccess::DepthWrite)
                    continue;
                if (!push_view(wr))
                {
                    valid_views = false;
                    break;
                }
            }

            if (!valid_views || view_count == 0 || w == 0 || layers == 0)
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Framebuffer attachments for pass '{}' are missing or have incompatible extents", pass.Name ? pass.Name : "?")
                continue;
            }

            gp->RenderAreaWidth    = w;
            gp->RenderAreaHeight   = h;

            auto* const attachment = gp->GetAttachment();
            if (!attachment)
                continue;
            const VkRenderPass rp               = attachment->GetHandle();

            bool               matches_existing = pass.Framebuffer && pass.Framebuffer->Handle && pass.FramebufferRenderPass == rp && pass.FramebufferViewCount == view_count && pass.FramebufferWidth == w && pass.FramebufferHeight == h && pass.FramebufferLayers == layers;
            for (uint32_t view_index = 0; matches_existing && view_index < view_count; ++view_index)
                matches_existing = pass.FramebufferViews[view_index] == view_buf[view_index];
            if (matches_existing)
            {
                if (pass.Persistent)
                    pass.Persistent->Framebuffer = pass.Framebuffer;
                continue;
            }

            VkFramebuffer vk_fb = Device->CreateFramebuffer(Core::Containers::ArrayView<VkImageView>{view_buf, view_count}, rp, w, h, layers);

            if (vk_fb == VK_NULL_HANDLE)
            {
                ZENGINE_CORE_ERROR("[RenderGraph] AllocateFramebuffers: CreateFramebuffer returned null for pass '{}' (views={} rp={} w={} h={})", pass.Name ? pass.Name : "?", view_count, (void*) rp, w, h)
                continue;
            }

            if (!pass.Framebuffer)
                pass.Framebuffer = ZPushStructCtorArgs(Device->Arena, Buffers::FramebufferVNext, Device);
            else
                pass.Framebuffer->Dispose();
            pass.Framebuffer->Reset(vk_fb, w, h);
            pass.FramebufferRenderPass = rp;
            pass.FramebufferViewCount  = view_count;
            pass.FramebufferWidth      = w;
            pass.FramebufferHeight     = h;
            pass.FramebufferLayers     = layers;
            for (uint32_t view_index = 0; view_index < view_count; ++view_index)
                pass.FramebufferViews[view_index] = view_buf[view_index];
            if (pass.Persistent)
            {
                pass.Persistent->Framebuffer           = pass.Framebuffer;
                pass.Persistent->FramebufferRenderPass = pass.FramebufferRenderPass;
                pass.Persistent->FramebufferViewCount  = pass.FramebufferViewCount;
                pass.Persistent->FramebufferWidth      = pass.FramebufferWidth;
                pass.Persistent->FramebufferHeight     = pass.FramebufferHeight;
                pass.Persistent->FramebufferLayers     = pass.FramebufferLayers;
                for (uint32_t view_index = 0; view_index < view_count; ++view_index)
                    pass.Persistent->FramebufferViews[view_index] = view_buf[view_index];
            }
        }
    }

    void RenderGraphResourceBuilder::Initialize(RenderGraph* graph)
    {
        Graph = graph;
    }

    static bool CreateOcclusionQueryPoolFrames(RenderGraph* graph, RGQueryPoolStorage* storage)
    {
        if (!graph || !storage || !graph->Device)
            return graph != nullptr && storage != nullptr;

        auto* const swapchain = graph->Device->SwapchainPtr;
        if (!swapchain)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Cannot create query pool '{}' before the swapchain frame contexts exist", storage->Name ? storage->Name : "?")
            return false;
        }

        const uint32_t frame_count = static_cast<uint32_t>(swapchain->FrameContexts.size());
        if (frame_count == 0)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Cannot create query pool '{}' without frame contexts", storage->Name ? storage->Name : "?")
            return false;
        }

        storage->FramePools.init(graph->Device->Arena, frame_count, frame_count);
        for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
            storage->FramePools[frame_index] = VK_NULL_HANDLE;

        VkQueryPoolCreateInfo create_info = {};
        create_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        create_info.queryType             = VK_QUERY_TYPE_OCCLUSION;
        create_info.queryCount            = storage->QueryCount;
        for (VkQueryPool& frame_pool : storage->FramePools)
        {
            if (vkCreateQueryPool(graph->Device->LogicalDevice, &create_info, nullptr, &frame_pool) == VK_SUCCESS)
                continue;

            ZENGINE_CORE_ERROR("[RenderGraph] Failed to create occlusion query pool '{}'", storage->Name ? storage->Name : "?")
            for (VkQueryPool& created_pool : storage->FramePools)
            {
                if (created_pool != VK_NULL_HANDLE)
                {
                    vkDestroyQueryPool(graph->Device->LogicalDevice, created_pool, nullptr);
                    created_pool = VK_NULL_HANDLE;
                }
            }
            storage->FramePools.clear();
            return false;
        }
        return true;
    }

    RGQueryHandle RenderGraphResourceBuilder::DeclareOcclusionQueryPool(cstring name, uint32_t query_count)
    {
        if (!Graph || !name || query_count == 0 || Graph->QueryPoolIndex.capacity() == 0)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] DeclareOcclusionQueryPool requires a name, non-zero count, and initialized graph")
            return {};
        }

        if (const uint32_t* index = Graph->QueryPoolIndex.find(name))
        {
            const RGQueryPoolStorage& pool = Graph->QueryPools[*index];
            if (pool.QueryCount != query_count)
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Occlusion query pool '{}' was declared with incompatible counts ({} and {})", name, pool.QueryCount, query_count)
                return {};
            }
            return {*index};
        }

        RGQueryPoolStorage storage = {};
        storage.Name               = name;
        storage.QueryCount         = query_count;
        if (!CreateOcclusionQueryPoolFrames(Graph, &storage))
            return {};

        const uint32_t index = static_cast<uint32_t>(Graph->QueryPools.size());
        Graph->QueryPools.push(std::move(storage));
        Graph->QueryPoolIndex[name] = index;
        return {index};
    }

    void RenderGraphResourceBuilder::WriteQueryPool(RGQueryHandle pool, uint32_t first_query, uint32_t count)
    {
        if (!Graph || CurrentPass == UINT32_MAX || CurrentPass >= Graph->Passes.size() || !pool.Valid() || pool.Index >= Graph->QueryPools.size() || count == 0)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] WriteQueryPool requires an active pass, valid pool, and non-empty range")
            return;
        }

        const RGQueryPoolStorage& storage = Graph->QueryPools[pool.Index];
        if (first_query >= storage.QueryCount || count > storage.QueryCount - first_query)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Query write range [{}, {}) exceeds pool '{}' capacity {}", first_query, first_query + count, storage.Name ? storage.Name : "?", storage.QueryCount)
            return;
        }

        Graph->Passes[CurrentPass].QueryWrites.push({.Pool = pool, .FirstQuery = first_query, .Count = count});
    }

    RGReadbackHandle RenderGraphResourceBuilder::DeclareQueryReadback(RGQueryHandle pool, RGReadbackFn callback, void* context)
    {
        if (!Graph || CurrentPass == UINT32_MAX || CurrentPass >= Graph->Passes.size() || !pool.Valid() || pool.Index >= Graph->QueryPools.size() || !callback)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] DeclareQueryReadback requires an active pass, valid query pool, and callback")
            return {};
        }

        const uint32_t index = static_cast<uint32_t>(Graph->QueryReadbackRequests.size());
        Graph->QueryReadbackRequests.push({.Pool = pool, .Callback = callback, .Context = context});
        return {index};
    }

    static uint32_t GetOrCreateResource(RenderGraph* graph, cstring name, RGResourceKind kind, bool external, const Specifications::TextureSpecification& spec)
    {
        if (auto* idx = graph->ResourceIndex.find(name))
            return *idx;

        uint32_t idx  = static_cast<uint32_t>(graph->Resources.size());
        auto&    res  = graph->Resources.push_use({});
        res.Name      = name;
        res.Kind      = kind;
        res.External  = external;
        res.Transient = !external;
        res.Spec      = spec;
        InitializeResourceVersions(res, &graph->FrameArena);
        graph->ResourceIndex[name] = idx;
        return idx;
    }

    static RGResourceHandle RecordAccess(RenderGraph* graph, uint32_t pass_idx, uint32_t res_idx, RGAccess access, cstring binding_key, bool is_write, uint32_t read_version = UINT32_MAX, RGSubresourceRange range = {})
    {
        if (pass_idx == UINT32_MAX)
            return {};
        RGPass&        pass     = graph->Passes[pass_idx];
        RGResource&    resource = graph->Resources[res_idx];
        RGPassResource pr;
        if (is_write)
        {
            pr.Handle = {res_idx, ++resource.LatestVersion};
            // A write always creates the sole producer for a new logical version.
            // The declaration validator diagnoses manually-constructed duplicate
            // producers used by topology-only tests.
            while (resource.Versions.size() <= pr.Handle.Version)
                resource.Versions.push({});
            resource.Versions[pr.Handle.Version].ProducerPass = pass_idx;
        }
        else
        {
            pr.Handle = {res_idx, read_version == UINT32_MAX ? resource.LatestVersion : read_version};
        }
        pr.Access     = access;
        pr.BindingKey = binding_key;
        pr.Range      = range;
        if (is_write)
            pass.Writes.push(pr);
        else
            pass.Reads.push(pr);
        return pr.Handle;
    }

    void RenderGraph::AddReadbackPasses()
    {
        for (uint32_t request_index = 0; request_index < ReadbackRequests.size(); ++request_index)
        {
            RGReadbackRequest& request = ReadbackRequests[request_index];
            if (!request.Source.Valid() || request.Source.Index >= Resources.size())
                continue;

            const uint32_t pass_index = static_cast<uint32_t>(Passes.size());
            RGPass&        pass       = Passes.push_use({});
            pass.Name                 = request.Name;
            pass.Enabled              = true;
            pass.Flags                = RGPassFlags::NeverCull;
            pass.RequiresRenderPass   = false;
            pass.RequestedQueue       = Rendering::QueueType::TRANSFER_QUEUE;
            pass.InternalOperation    = RGInternalPassOperation::Readback;
            pass.InternalRequestIndex = request_index;
            pass.Reads.init(&FrameArena, 1);
            pass.Writes.init(&FrameArena, 0);
            pass.QueryWrites.init(&FrameArena, 0);
            pass.QueryResets.init(&FrameArena, 0);
            pass.BarrierPlans.init(&FrameArena, 0);
            pass.BufferBarrierPlans.init(&FrameArena, 1);
            pass.AliasingBarrierPlans.init(&FrameArena, 0);
            pass.StreamingAcquirePlans.init(&FrameArena, 0);
            RecordAccess(this, pass_index, request.Source.Index, RGAccess::TransferRead, nullptr, false, request.Source.Version);
        }
    }

    void RenderGraph::AddQueryReadbackPasses()
    {
        static constexpr cstring kQueryReadbackPassName = "OcclusionQueryReadback";

        for (uint32_t request_index = 0; request_index < QueryReadbackRequests.size(); ++request_index)
        {
            RGQueryReadbackRequest& request = QueryReadbackRequests[request_index];
            if (!request.Pool.Valid() || request.Pool.Index >= QueryPools.size())
                continue;

            RGPass& pass              = Passes.push_use({});
            pass.Name                 = kQueryReadbackPassName;
            pass.Enabled              = true;
            pass.Flags                = RGPassFlags::NeverCull;
            pass.RequiresRenderPass   = false;
            pass.RequestedQueue       = Rendering::QueueType::TRANSFER_QUEUE;
            pass.InternalOperation    = RGInternalPassOperation::QueryReadback;
            pass.InternalRequestIndex = request_index;
            pass.InternalQueryPool    = request.Pool;
            pass.Reads.init(&FrameArena, 0);
            pass.Writes.init(&FrameArena, 0);
            pass.QueryWrites.init(&FrameArena, 0);
            pass.QueryResets.init(&FrameArena, 0);
            pass.BarrierPlans.init(&FrameArena, 0);
            pass.BufferBarrierPlans.init(&FrameArena, 0);
            pass.AliasingBarrierPlans.init(&FrameArena, 0);
            pass.StreamingAcquirePlans.init(&FrameArena, 0);
        }
    }

    bool RenderGraph::PrepareReadbacks()
    {
        for (RGReadbackRequest& request : ReadbackRequests)
        {
            request.AllocationIndex = UINT32_MAX;
            request.Recorded        = false;
            request.Submitted       = false;

            if (!request.Source.Valid() || request.Source.Index >= Resources.size())
                return false;
            const RGResource& source = Resources[request.Source.Index];
            if (source.Kind != RGResourceKind::Buffer || request.Source.Version >= source.Versions.size())
                return false;

            VkDeviceSize source_size = source.BufferSize;
            if (source_size == 0 && source.Buffer)
                source_size = source.Buffer->Size;
            if (source_size == 0 || request.Offset >= source_size)
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Readback '{}' exceeds source buffer bounds", request.Name ? request.Name : "?")
                CancelUnsubmittedReadbacks();
                return false;
            }

            const VkDeviceSize size = request.Size == VK_WHOLE_SIZE ? source_size - request.Offset : request.Size;
            if (size == 0 || size > source_size - request.Offset)
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Readback '{}' exceeds source buffer bounds", request.Name ? request.Name : "?")
                CancelUnsubmittedReadbacks();
                return false;
            }

            request.Size            = size;
            request.AllocationIndex = ReadbackRing.Acquire(size);
            if (request.AllocationIndex == UINT32_MAX)
            {
                CancelUnsubmittedReadbacks();
                return false;
            }
        }
        return true;
    }

    bool RenderGraph::PrepareQueryReadbacks()
    {
        for (RGPass& pass : Passes)
            pass.QueryResets.clear();

        const bool has_query_work = !QueryReadbackRequests.empty() || HasQueryWrites(Passes);
        if (!has_query_work)
            return true;

        const uint32_t frame_slot = FindCurrentFrameContextSlot(Device ? Device->SwapchainPtr : nullptr);
        if (frame_slot == UINT32_MAX)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Query work requires an active frame context")
            return false;
        }

        // The first writer in execution order resets a pool exactly once. All
        // query writers are forced to the graphics queue by BuildQueueSchedule.
        for (uint32_t order_index = 0; order_index < SortedPassIndices.size(); ++order_index)
        {
            RGPass& pass = Passes[SortedPassIndices[order_index]];
            for (const RGQueryWrite& write : pass.QueryWrites)
            {
                if (!write.Pool.Valid() || write.Pool.Index >= QueryPools.size())
                    return false;
                const RGQueryPoolStorage& pool = QueryPools[write.Pool.Index];
                if (frame_slot >= pool.FramePools.size() || pool.FramePools[frame_slot] == VK_NULL_HANDLE || pass.Queue != Rendering::QueueType::GRAPHIC_QUEUE)
                {
                    ZENGINE_CORE_ERROR("[RenderGraph] Query pool '{}' has no graphics-frame storage", pool.Name ? pool.Name : "?")
                    return false;
                }

                bool already_reset = false;
                for (const RGPass& previous_pass : Passes)
                {
                    for (const RGQueryHandle reset : previous_pass.QueryResets)
                    {
                        if (reset.Index == write.Pool.Index)
                        {
                            already_reset = true;
                            break;
                        }
                    }
                    if (already_reset)
                        break;
                }
                if (!already_reset)
                    pass.QueryResets.push(write.Pool);
            }
        }

        for (RGQueryReadbackRequest& request : QueryReadbackRequests)
        {
            request.AllocationIndex     = UINT32_MAX;
            request.WriterCount         = 0;
            request.RecordedWriterCount = 0;
            request.Recorded            = false;
            request.Submitted           = false;

            if (!request.Pool.Valid() || request.Pool.Index >= QueryPools.size())
                return false;
            const RGQueryPoolStorage& pool = QueryPools[request.Pool.Index];
            if (frame_slot >= pool.FramePools.size() || pool.FramePools[frame_slot] == VK_NULL_HANDLE)
                return false;

            for (const RGPass& pass : Passes)
            {
                if (!pass.IsActive())
                    continue;
                for (const RGQueryWrite& write : pass.QueryWrites)
                {
                    if (write.Pool.Index == request.Pool.Index)
                        ++request.WriterCount;
                }
            }
            if (request.WriterCount == 0)
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Query readback has no active writer")
                CancelUnsubmittedReadbacks();
                return false;
            }

            request.AllocationIndex = ReadbackRing.Acquire(static_cast<VkDeviceSize>(pool.QueryCount) * sizeof(uint64_t));
            if (request.AllocationIndex == UINT32_MAX)
            {
                CancelUnsubmittedReadbacks();
                return false;
            }
        }
        return true;
    }

    void RenderGraph::SubmitReadbacks(uint32_t first_pass_order, uint32_t pass_count, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value)
    {
        const uint32_t end_order = std::min(first_pass_order + pass_count, static_cast<uint32_t>(SortedPassIndices.size()));
        for (uint32_t order_index = first_pass_order; order_index < end_order; ++order_index)
        {
            const RGPass& pass = Passes[SortedPassIndices[order_index]];
            if (pass.InternalOperation == RGInternalPassOperation::Readback)
            {
                if (pass.InternalRequestIndex >= ReadbackRequests.size())
                    continue;
                RGReadbackRequest& request = ReadbackRequests[pass.InternalRequestIndex];
                if (!request.Recorded || request.Submitted)
                    continue;
                request.Submitted = ReadbackRing.Submit(request.AllocationIndex, timeline, timeline_value, request.Size, request.Callback, request.Context);
                continue;
            }
            if (pass.InternalOperation != RGInternalPassOperation::QueryReadback || pass.InternalRequestIndex >= QueryReadbackRequests.size() || !pass.InternalQueryPool.Valid() || pass.InternalQueryPool.Index >= QueryPools.size())
                continue;

            RGQueryReadbackRequest& request = QueryReadbackRequests[pass.InternalRequestIndex];
            if (!request.Recorded || request.Submitted)
                continue;
            const VkDeviceSize data_size = static_cast<VkDeviceSize>(QueryPools[pass.InternalQueryPool.Index].QueryCount) * sizeof(uint64_t);
            request.Submitted            = ReadbackRing.Submit(request.AllocationIndex, timeline, timeline_value, data_size, request.Callback, request.Context);
        }
    }

    void RenderGraph::CancelUnsubmittedReadbacks()
    {
        for (RGReadbackRequest& request : ReadbackRequests)
        {
            if (request.AllocationIndex != UINT32_MAX && !request.Submitted)
                ReadbackRing.Cancel(request.AllocationIndex);
        }
        for (RGQueryReadbackRequest& request : QueryReadbackRequests)
        {
            if (request.AllocationIndex != UINT32_MAX && !request.Submitted)
                ReadbackRing.Cancel(request.AllocationIndex);
        }
    }

    void RenderGraph::MarkQueryWritesRecorded(const RGPass& pass)
    {
        for (const RGQueryWrite& write : pass.QueryWrites)
        {
            for (RGQueryReadbackRequest& request : QueryReadbackRequests)
            {
                if (request.Pool.Index == write.Pool.Index)
                    ++request.RecordedWriterCount;
            }
        }
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteColorAttachment(cstring name, const Specifications::TextureSpecification& spec, RGSubresourceRange range)
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
        auto handle                                     = RecordAccess(Graph, CurrentPass, idx, RGAccess::ColorWrite, nullptr, true, UINT32_MAX, range);
        Graph->Passes[CurrentPass].Writes.back().LoadOp = spec.LoadOp;
        return handle;
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteDepthAttachment(cstring name, const Specifications::TextureSpecification& spec, RGSubresourceRange range)
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
        auto handle                                     = RecordAccess(Graph, CurrentPass, idx, RGAccess::DepthWrite, nullptr, true, UINT32_MAX, range);
        Graph->Passes[CurrentPass].Writes.back().LoadOp = spec.LoadOp;
        return handle;
    }

    RGResourceHandle RenderGraphResourceBuilder::UpdateColorAttachment(cstring name, const Specifications::TextureSpecification& spec, RGSubresourceRange range)
    {
        ZENGINE_VALIDATE_ASSERT(spec.LoadOp == Specifications::LoadOperation::LOAD, "UpdateColorAttachment requires LoadOperation::LOAD")
        const RGResourceHandle handle = WriteColorAttachment(name, spec, range);
        if (handle.Valid())
            Graph->Passes[CurrentPass].Writes.back().Access = RGAccess::ColorReadWrite;
        return handle;
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteStorageImage(cstring name, const Specifications::TextureSpecification& spec, cstring binding_key, RGSubresourceRange range)
    {
        const uint32_t index    = GetOrCreateResource(Graph, name, RGResourceKind::Texture, false, spec);
        RGResource&    resource = Graph->Resources[index];
        if (resource.External && !resource.TextureHandle.Valid())
        {
            resource.Kind      = RGResourceKind::Texture;
            resource.External  = false;
            resource.Transient = true;
            resource.Spec      = spec;
        }
        return RecordAccess(Graph, CurrentPass, index, RGAccess::StorageWrite, binding_key, true, UINT32_MAX, range);
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadWriteStorageImage(cstring name, cstring binding_key, RGSubresourceRange range)
    {
        if (const auto* index = Graph->ResourceIndex.find(name))
            return RecordAccess(Graph, CurrentPass, *index, RGAccess::ShaderReadWrite, binding_key, true, UINT32_MAX, range);
        return {};
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadTexture(cstring name, cstring binding_key, RGSubresourceRange range)
    {
        Specifications::TextureSpecification empty_spec = {};
        uint32_t                             idx        = GetOrCreateResource(Graph, name, RGResourceKind::Texture, true, empty_spec);
        return ReadTexture({idx, Graph->Resources[idx].LatestVersion}, binding_key, range);
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadTexture(RGResourceHandle handle, cstring binding_key, RGSubresourceRange range)
    {
        if (!handle.Valid() || handle.Index >= Graph->Resources.size())
            return {};
        return RecordAccess(Graph, CurrentPass, handle.Index, RGAccess::ShaderRead, binding_key, false, handle.Version, range);
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadDepth(cstring name, RGSubresourceRange range)
    {
        Specifications::TextureSpecification empty_spec = {};
        uint32_t                             idx        = GetOrCreateResource(Graph, name, RGResourceKind::Attachment, true, empty_spec);
        return ReadDepth({idx, Graph->Resources[idx].LatestVersion}, range);
    }

    RGResourceHandle RenderGraphResourceBuilder::ReadDepth(RGResourceHandle handle, RGSubresourceRange range)
    {
        if (!handle.Valid() || handle.Index >= Graph->Resources.size())
            return {};
        return RecordAccess(Graph, CurrentPass, handle.Index, RGAccess::DepthRead, nullptr, false, handle.Version, range);
    }

    RGResourceHandle RenderGraphResourceBuilder::ImportBuffer(cstring name, const Core::Memory::BufferView* buffer)
    {
        return Graph->ImportBuffer(name, buffer);
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteBuffer(cstring name, VkDeviceSize size, VkBufferUsageFlags usage, cstring binding_key)
    {
        if (size == 0 || usage == 0)
            return {};

        Specifications::TextureSpecification empty_spec = {};
        const uint32_t                       index      = GetOrCreateResource(Graph, name, RGResourceKind::Buffer, false, empty_spec);
        RGResource&                          resource   = Graph->Resources[index];
        if (resource.Kind != RGResourceKind::Buffer)
            return {};
        if (!resource.External)
        {
            if (resource.BufferSize != 0 && resource.BufferSize != size)
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Transient buffer '{}' has conflicting sizes ({} and {})", name ? name : "?", resource.BufferSize, size)
                return {};
            }
            resource.BufferSize   = size;
            resource.BufferUsage |= usage;
        }
        return RecordAccess(Graph, CurrentPass, index, RGAccess::BufferWrite, binding_key, true);
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

    void RenderGraphResourceBuilder::Export(RGResourceHandle handle)
    {
        if (CurrentPass == UINT32_MAX || !handle.Valid() || handle.Index >= Graph->Resources.size())
            return;

        auto* version = FindResourceVersion(Graph->Resources[handle.Index], handle.Version);
        if (!version)
            return;

        version->Exported = true;
        Graph->ExportedResources.push({handle, CurrentPass});
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

    RGResourceHandle RenderGraphResourceBuilder::ImportTexture(cstring name, Textures::TextureHandle handle, VkImageLayout initial_layout)
    {
        return Graph->ImportTexture(name, handle, initial_layout);
    }

    RGResourceHandle RenderGraphResourceBuilder::ImportStreamingTexture(cstring name, const Hardwares::StreamingUploadTicket& ticket)
    {
        return Graph->ImportStreamingTexture(name, ticket);
    }

    void RenderGraphResourceBuilder::ReadBindless(RGResourceHandle image, uint32_t slot, RGShaderStages stages)
    {
        if (CurrentPass == UINT32_MAX || CurrentPass >= Graph->Passes.size() || !image.Valid() || image.Index >= Graph->Resources.size())
            return;

        const RGResourceKind kind = Graph->Resources[image.Index].Kind;
        if (kind == RGResourceKind::Buffer || kind == RGResourceKind::Swapchain)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] ReadBindless requires an image resource")
            return;
        }

        const VkPipelineStageFlags2 stage_mask = GetBindlessShaderStages(stages);
        if (stage_mask == VK_PIPELINE_STAGE_2_NONE)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] ReadBindless requires at least one shader stage")
            return;
        }

        const RGResourceHandle handle = RecordAccess(Graph, CurrentPass, image.Index, RGAccess::ShaderRead, nullptr, false, image.Version);
        if (!handle.Valid())
            return;

        RGPassResource& declaration     = Graph->Passes[CurrentPass].Reads.back();
        declaration.StateOverride       = GetRGAccessState(RGAccess::ShaderRead);
        declaration.StateOverride.Stage = stage_mask;
        declaration.HasStateOverride    = true;
        declaration.BindlessSlot        = slot;
    }

    void RenderGraphResourceBuilder::ReadBindless()
    {
        if (CurrentPass != UINT32_MAX && CurrentPass < Graph->Passes.size())
            Graph->Passes[CurrentPass].ReadsBindless = true;
    }

    void RenderGraphResourceBuilder::UseConditional(RGResourceHandle condition, const ConditionalSpec& spec)
    {
        if (CurrentPass == UINT32_MAX || CurrentPass >= Graph->Passes.size())
            return;

        RGPass& pass = Graph->Passes[CurrentPass];
        if (pass.Conditional.Enabled)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Pass '{}' declares more than one conditional-rendering source", pass.Name ? pass.Name : "?")
            return;
        }

        pass.Conditional = {.Condition = condition, .Specification = spec, .Enabled = true, .UsesFallback = !Graph->Device || !Graph->Device->PhysicalDeviceSupportConditionalRendering};
        if (!condition.Valid() || condition.Index >= Graph->Resources.size() || Graph->Resources[condition.Index].Kind != RGResourceKind::Buffer)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] UseConditional requires a buffer resource")
            return;
        }

        // The unconditional fallback deliberately has no buffer access. This avoids
        // extension-only stage/access/usage bits on a device that did not enable it.
        if (pass.Conditional.UsesFallback)
            return;
        RecordAccess(Graph, CurrentPass, condition.Index, RGAccess::ConditionalRead, nullptr, false, condition.Version);
    }

    RGReadbackHandle RenderGraphResourceBuilder::DeclareReadback(cstring name, RGResourceHandle gpu_buffer, RGReadbackFn callback, void* context, VkDeviceSize size, VkDeviceSize offset)
    {
        if (CurrentPass == UINT32_MAX || CurrentPass >= Graph->Passes.size() || !name || !gpu_buffer.Valid() || gpu_buffer.Index >= Graph->Resources.size() || !callback)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] DeclareReadback requires a name, exact buffer version, and callback")
            return {};
        }
        if (Graph->Resources[gpu_buffer.Index].Kind != RGResourceKind::Buffer)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] DeclareReadback requires a buffer resource")
            return {};
        }
        if (size == 0)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] DeclareReadback size must be non-zero or VK_WHOLE_SIZE")
            return {};
        }

        RGResource& resource = Graph->Resources[gpu_buffer.Index];
        if (resource.External && resource.Buffer && (resource.Buffer->Usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0)
        {
            ZENGINE_CORE_ERROR("[RenderGraph] Readback source '{}' was not created with VK_BUFFER_USAGE_TRANSFER_SRC_BIT", resource.Name ? resource.Name : "?")
            return {};
        }
        resource.BufferUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        const uint32_t index  = static_cast<uint32_t>(Graph->ReadbackRequests.size());
        Graph->ReadbackRequests.push({.Name = name, .Source = gpu_buffer, .Offset = offset, .Size = size, .Callback = callback, .Context = context});
        return {index};
    }

    RGResourceHandle RenderGraphResourceBuilder::AttachRenderTarget(cstring name, const Textures::TextureHandle& texture)
    {
        return ImportRenderTarget(name, texture);
    }

    RGResourceHandle RenderGraphResourceBuilder::WriteSwapchain()
    {
        static constexpr cstring             kSwapchainResourceName = "g_swapchain_render_target";

        Specifications::TextureSpecification empty_spec             = {};
        const uint32_t                       idx                    = GetOrCreateResource(Graph, kSwapchainResourceName, RGResourceKind::Swapchain, true, empty_spec);
        RGResource&                          resource               = Graph->Resources[idx];
        resource.Kind                                               = RGResourceKind::Swapchain;
        resource.External                                           = true;
        resource.Transient                                          = false;
        resource.InitialState                                       = {VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
        resource.CurrentState                                       = resource.InitialState;
        resource.RuntimeState                                       = resource.InitialState;
        return RecordAccess(Graph, CurrentPass, idx, RGAccess::ColorWrite, nullptr, true);
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

    VkQueryPool RenderGraphResourceInspector::GetQueryPool(RGQueryHandle handle) const
    {
        return GetCurrentQueryPool(Graph, handle);
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
