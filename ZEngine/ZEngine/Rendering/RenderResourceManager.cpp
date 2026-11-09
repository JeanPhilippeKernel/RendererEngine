#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/ImageCodec.h>
#include <ZEngine/Profiling/Profiling.h>
#include <stb/stb_image.h>
#include <cstdlib>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Buffers/Bitmap.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Graphics/AssetPreviewPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/ThumbnailCacheMaintenance.h>
#include <ZEngine/ZEngineDef.h>
#include <stb/deprecated/stb_image_resize.h>
#include <stb/stb_image_write.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <random>
#include <thread>
#include <type_traits>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Hardwares;
using namespace ZEngine::Importers;
using namespace ZEngine::Managers;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering
{
    namespace
    {
        constexpr uint32_t       kBrdfLutResolution  = 512;
        constexpr uint32_t       kBrdfLutSampleCount = 64;
        constexpr uint32_t       kMaxThumbnailExtent = 512;

        // Compare/hash shader fields, not padding in the aligned GPU structure.
        std::array<uint32_t, 30> MaterialKey(const Meshes::MeshMaterial& material)
        {
            std::array<uint32_t, 30>  key      = {};
            const Core::Maths::Vec4f* colors[] = {&material.AmbientColor, &material.EmissiveColor, &material.AlbedoColor, &material.SpecularColor, &material.RoughnessColor, &material.Factors};
            uint32_t                  index    = 0;
            for (const auto* color : colors)
            {
                for (uint32_t component = 0; component < 4; ++component)
                {
                    key[index++] = std::bit_cast<uint32_t>((*color)[component]);
                }
            }
            key[index++] = material.AlbedoMap;
            key[index++] = material.EmissiveMap;
            key[index++] = material.NormalMap;
            key[index++] = material.OpacityMap;
            key[index++] = material.SpecularMap;
            key[index]   = static_cast<uint32_t>(material.AlphaMode);
            return key;
        }

        // A single atomically renamed file contains PNG bytes followed by a
        // versioned, little-endian validation record. No torn PNG/sidecar pairs.
        constexpr size_t kThumbnailMetadataSize = 80;
        constexpr size_t kMaxThumbnailPayload   = 2 * 1024 * 1024;
        uint64_t         ThumbnailChecksum(const uint8_t* bytes, size_t size, uint64_t hash = 14695981039346656037ull)
        {
            for (size_t i = 0; i < size; ++i)
            {
                hash = (hash ^ bytes[i]) * 1099511628211ull;
            }
            return hash;
        }
        void StoreThumbnailInteger(uint8_t* bytes, uint64_t value, size_t count)
        {
            for (size_t i = 0; i < count; ++i)
            {
                bytes[i] = static_cast<uint8_t>(value >> (i * 8));
            }
        }
        uint64_t LoadThumbnailInteger(const uint8_t* bytes, size_t count)
        {
            uint64_t value = 0;
            for (size_t i = 0; i < count; ++i)
            {
                value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
            }
            return value;
        }
        std::array<uint8_t, kThumbnailMetadataSize> ThumbnailMetadata(const RenderResourceManager::ThumbnailCacheIdentity& key, uint32_t width, uint32_t height)
        {
            std::array<uint8_t, kThumbnailMetadataSize> bytes = {};
            ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(bytes.data(), bytes.size(), "ZTHUMB03", 8) == Helpers::MEMORY_OP_SUCCESS, "Thumbnail metadata signature exceeds its buffer")
            const auto uuid_bytes = key.AssetUUID.as_bytes();
            ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(bytes.data() + 8, bytes.size() - 8, uuid_bytes.data(), uuid_bytes.size()) == Helpers::MEMORY_OP_SUCCESS, "Thumbnail metadata UUID exceeds its buffer")
            StoreThumbnailInteger(bytes.data() + 24, key.SourceHash, 8);
            StoreThumbnailInteger(bytes.data() + 32, key.DependencyHash, 8);
            StoreThumbnailInteger(bytes.data() + 40, key.SchemaRevision, 4);
            StoreThumbnailInteger(bytes.data() + 44, key.RendererRevision, 4);
            StoreThumbnailInteger(bytes.data() + 48, width, 4);
            StoreThumbnailInteger(bytes.data() + 52, height, 4);
            StoreThumbnailInteger(bytes.data() + 56, 1, 4); // display-encoded RGBA8 UNORM
            StoreThumbnailInteger(bytes.data() + 60, 1, 4); // encoder contract revision
            return bytes;
        }

        uint16_t FloatToHalf(float value)
        {
            uint32_t bits = 0;
            ZENGINE_VALIDATE_ASSERT(secure_memcpy(&bits, sizeof(bits), &value, sizeof(value)) == MEMORY_OP_SUCCESS, "FloatToHalf: failed to copy float bits")

            const uint16_t sign     = static_cast<uint16_t>((bits >> 16) & 0x8000u);
            const int32_t  exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
            uint32_t       mantissa = bits & 0x007F'FFFFu;
            if (exponent <= 0)
            {
                if (exponent < -10)
                {
                    return sign;
                }

                mantissa             |= 0x0080'0000u;
                const uint32_t shift  = static_cast<uint32_t>(14 - exponent);
                return static_cast<uint16_t>(sign | ((mantissa + (1u << (shift - 1))) >> shift));
            }
            if (exponent >= 31)
            {
                return static_cast<uint16_t>(sign | 0x7C00u);
            }

            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | ((mantissa + 0x0000'1000u) >> 13));
        }

        float RadicalInverseVdC(uint32_t bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x5555'5555u) << 1u) | ((bits & 0xAAAA'AAAAu) >> 1u);
            bits = ((bits & 0x3333'3333u) << 2u) | ((bits & 0xCCCC'CCCCu) >> 2u);
            bits = ((bits & 0x0F0F'0F0Fu) << 4u) | ((bits & 0xF0F0'F0F0u) >> 4u);
            bits = ((bits & 0x00FF'00FFu) << 8u) | ((bits & 0xFF00'FF00u) >> 8u);
            return static_cast<float>(bits) * 2.3283064365386963e-10f;
        }

        float GeometrySchlickGGX(float ndot, float roughness)
        {
            const float roughness_plus_one = roughness + 1.0f;
            const float k                  = (roughness_plus_one * roughness_plus_one) * 0.125f;
            return ndot / (ndot * (1.0f - k) + k);
        }

        void IntegrateBrdf(float ndot_view, float roughness, uint32_t sample_count, float& out_scale, float& out_bias)
        {
            constexpr float pi     = 3.14159265358979323846f;
            const float     view_x = std::sqrt(std::max(0.0f, 1.0f - ndot_view * ndot_view));
            float           scale  = 0.0f;
            float           bias   = 0.0f;

            for (uint32_t index = 0; index < sample_count; ++index)
            {
                const float xi_x       = static_cast<float>(index) / static_cast<float>(sample_count);
                const float xi_y       = RadicalInverseVdC(index);
                const float roughness2 = roughness * roughness;
                const float phi        = 2.0f * pi * xi_x;
                const float cos_theta  = std::sqrt((1.0f - xi_y) / (1.0f + (roughness2 * roughness2 - 1.0f) * xi_y));
                const float sin_theta  = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
                const float half_x     = std::cos(phi) * sin_theta;
                const float half_y     = std::sin(phi) * sin_theta;
                const float half_z     = cos_theta;
                const float vdot_half  = std::max(0.0f, view_x * half_x + ndot_view * half_z);
                const float light_x    = 2.0f * vdot_half * half_x - view_x;
                const float light_y    = 2.0f * vdot_half * half_y;
                const float light_z    = 2.0f * vdot_half * half_z - ndot_view;
                const float ndot_light = std::max(0.0f, light_z);
                const float ndot_half  = std::max(0.0f, half_z);
                if (ndot_light <= 0.0f || ndot_half <= 0.0f)
                {
                    continue;
                }

                const float geometry    = GeometrySchlickGGX(ndot_view, roughness) * GeometrySchlickGGX(ndot_light, roughness);
                const float visibility  = (geometry * vdot_half) / std::max(1.0e-5f, ndot_half * ndot_view);
                const float fresnel     = std::pow(1.0f - vdot_half, 5.0f);
                scale                  += (1.0f - fresnel) * visibility;
                bias                   += fresnel * visibility;
            }

            const float inverse_sample_count = 1.0f / static_cast<float>(sample_count);
            out_scale                        = scale * inverse_sample_count;
            out_bias                         = bias * inverse_sample_count;
        }

        void GenerateBrdfIntegrationLut(uint16_t* pixels, uint32_t resolution, uint32_t sample_count)
        {
            for (uint32_t y = 0; y < resolution; ++y)
            {
                const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(resolution);
                for (uint32_t x = 0; x < resolution; ++x)
                {
                    const float ndot_view = (static_cast<float>(x) + 0.5f) / static_cast<float>(resolution);
                    float       scale     = 0.0f;
                    float       bias      = 0.0f;
                    IntegrateBrdf(ndot_view, roughness, sample_count, scale, bias);

                    const uint32_t pixel = (y * resolution + x) * 4;
                    pixels[pixel]        = FloatToHalf(scale);
                    pixels[pixel + 1]    = FloatToHalf(bias);
                    pixels[pixel + 2]    = 0;
                    pixels[pixel + 3]    = FloatToHalf(1.0f);
                }
            }
        }
    } // namespace

    uint8_t* RenderResourceManager::ReadThumbnailCache(const char* filename, uint32_t width, uint32_t height, const ThumbnailCacheIdentity& identity)
    {
        if (!filename || !filename[0] || !width || !height || width > kMaxThumbnailExtent || height > kMaxThumbnailExtent)
        {
            return nullptr;
        }
        FILE* file = std::fopen(filename, "rb");
        if (!file)
        {
            return nullptr;
        }
        bool       valid = std::fseek(file, 0, SEEK_END) == 0;
        const long size  = valid ? std::ftell(file) : -1;
        valid            = valid && size > static_cast<long>(kThumbnailMetadataSize) && size <= static_cast<long>(kMaxThumbnailPayload + kThumbnailMetadataSize) && std::fseek(file, 0, SEEK_SET) == 0;
        auto* bytes      = valid ? static_cast<uint8_t*>(Importers::ImageCodec::Allocate(static_cast<size_t>(size))) : nullptr;
        valid            = bytes && std::fread(bytes, 1, static_cast<size_t>(size), file) == static_cast<size_t>(size);
        std::fclose(file);
        uint8_t* pixels = nullptr;
        if (valid)
        {
            const size_t   payload_size = static_cast<size_t>(size) - kThumbnailMetadataSize;
            const uint8_t* metadata     = bytes + payload_size;
            const auto     expected     = ThumbnailMetadata(identity, width, height);
            valid                       = std::memcmp(expected.data(), metadata, 64) == 0 && LoadThumbnailInteger(metadata + 64, 8) == payload_size;
            const uint64_t checksum     = ThumbnailChecksum(metadata, 72, ThumbnailChecksum(bytes, payload_size));
            valid                       = valid && checksum == LoadThumbnailInteger(metadata + 72, 8);
            int decoded_width = 0, decoded_height = 0, channels = 0;
            // Reject oversized headers before asking the decoder to allocate.
            valid = valid && stbi_info_from_memory(bytes, static_cast<int>(payload_size), &decoded_width, &decoded_height, &channels) && decoded_width == static_cast<int>(width) && decoded_height == static_cast<int>(height) && channels == 4;
            if (valid)
            {
                pixels = stbi_load_from_memory(bytes, static_cast<int>(payload_size), &decoded_width, &decoded_height, &channels, STBI_rgb_alpha);
            }
        }
        Importers::ImageCodec::Free(bytes);
        return pixels;
    }

    bool RenderResourceManager::WriteThumbnailCache(const char* filename, const uint8_t* pixels, uint32_t width, uint32_t height, const ThumbnailCacheIdentity& identity, bool bottom_up)
    {
        if (!filename || filename[0] == '\0' || !pixels || width == 0 || height == 0 || width > kMaxThumbnailExtent || height > kMaxThumbnailExtent)
        {
            return false;
        }

        const size_t                row_size  = static_cast<size_t>(width) * STBI_rgb_alpha;
        // Decode/upload pixels are bottom-up. Walk rows backwards for PNG
        // without mutating upload memory or changing STB's global flip flag.
        const uint8_t* const        first_row = bottom_up ? pixels + static_cast<size_t>(height - 1) * row_size : pixels;

        const std::filesystem::path cache_path(filename);
        std::error_code             error{};
        std::filesystem::create_directories(cache_path.parent_path(), error);
        bool published = false;
        if (!error)
        {
            // Each job owns an exclusively created sibling. A fixed .tmp name
            // lets overlapping jobs/editors truncate or unlink each other's
            // output, even though the final rename itself is atomic.
            std::random_device           entropy{};
            std::mt19937                 random(entropy());
            uuids::uuid_random_generator generator(random);
            const std::filesystem::path  temporary_path = cache_path.string() + "." + uuids::to_string(generator()) + ".tmp";
            FILE*                        temporary      = std::fopen(temporary_path.string().c_str(), "wbx");
            struct Writer
            {
                FILE*    File     = nullptr;
                bool     Success  = true;
                uint64_t Checksum = 14695981039346656037ull;
                size_t   Size     = 0;
            } writer{temporary};
            if (temporary)
            {
                const auto write = +[](void* context, void* data, int size) {
                    auto& output = *static_cast<Writer*>(context);
                    if (size < 0 || output.Size + static_cast<size_t>(size) > kMaxThumbnailPayload || std::fwrite(data, 1, static_cast<size_t>(size), output.File) != static_cast<size_t>(size))
                    {
                        output.Success = false;
                    }
                    else
                    {
                        output.Checksum  = ThumbnailChecksum(static_cast<const uint8_t*>(data), static_cast<size_t>(size), output.Checksum);
                        output.Size     += static_cast<size_t>(size);
                    }
                };
                const bool encoded  = stbi_write_png_to_func(write, &writer, static_cast<int>(width), static_cast<int>(height), STBI_rgb_alpha, first_row, (bottom_up ? -1 : 1) * static_cast<int>(row_size)) != 0;
                auto       metadata = ThumbnailMetadata(identity, width, height);
                StoreThumbnailInteger(metadata.data() + 64, writer.Size, 8);
                StoreThumbnailInteger(metadata.data() + 72, ThumbnailChecksum(metadata.data(), 72, writer.Checksum), 8);
                const bool metadata_written = std::fwrite(metadata.data(), 1, metadata.size(), temporary) == metadata.size();
                const bool flushed          = std::fflush(temporary) == 0;
                const bool closed           = std::fclose(temporary) == 0;
                if (encoded && writer.Success && metadata_written && flushed && closed)
                {
                    std::filesystem::rename(temporary_path, cache_path, error);
                    published = !error;
                }
                if (!published)
                {
                    std::filesystem::remove(temporary_path, error);
                }
            }
        }

        return published;
    }

    void RenderResourceManager::Initialize(VulkanDevice* device, Core::VFS::AssetRegistry* registry, Core::Memory::ArenaAllocator* upload_arena)
    {
        ZENGINE_VALIDATE_ASSERT(device != nullptr, "RenderResourceManager::Initialize: device must not be null")
        ZENGINE_VALIDATE_ASSERT(registry != nullptr, "RenderResourceManager::Initialize: registry must not be null")
        ZENGINE_VALIDATE_ASSERT(upload_arena != nullptr, "RenderResourceManager::Initialize: upload arena must not be null")

        m_device                       = device;
        m_scene_descriptor_preparation = device;
        m_registry                     = registry;
        m_upload_arena                 = upload_arena;
        m_pending_texture_decodes.value.store(0, std::memory_order_relaxed);
        m_accept_texture_decodes.value.store(true, std::memory_order_release);
        m_thumbnail_cache_cleanup       = {};
        m_thumbnail_maintenance_stats   = {};
        m_fallback_cubemap              = {};
        m_fallback_environment_lighting = {};
        for (TrackedTextureDecode& tracked : m_texture_decode_tracker.Entries)
        {
            tracked = {};
        }
        for (auto& producer : m_texture_producer_completions)
        {
            producer = {};
        }

        InitUploadPool();
        InitGlobalBuffers();
        InitTextureTimelines();
        InitUploadSlabs();
        m_synchronous_texture_scratch.Init(m_upload_arena, SYNCHRONOUS_TEXTURE_SCRATCH_BYTES);
        m_texture_task_slab.Init(m_upload_arena, TEXTURE_TASK_SLAB_BYTES);

        registry->SetOnReadyCallback(this, &RenderResourceManager::OnAssetReady);
        registry->SetOnStaleCallback(this, &RenderResourceManager::OnAssetStale);
        registry->SetOnRemovedCallback(this, &RenderResourceManager::OnAssetRemoved);
    }

    void RenderResourceManager::OnAssetReady(void* context, const uuids::uuid& uuid, AssetHandle handle)
    {
        auto*              manager = static_cast<RenderResourceManager*>(context);
        const AssetRecord* record  = manager->m_registry->FindByUUID(uuid);
        if (!record || record->Type != AssetType::MESH)
        {
            return;
        }

        std::lock_guard map_lock(manager->m_uuid_map_mutex);
        std::lock_guard pending_lock(manager->m_pending_mutex);
        for (uint32_t i = 0; i < manager->m_uuid_to_buffer_count; ++i)
        {
            if (manager->m_uuid_to_buffer[i].UUID == uuid)
            {
                return;
            }
        }
        for (uint32_t i = 0; i < manager->m_pending_count; ++i)
        {
            if (manager->m_pending[i].UUID == uuid)
            {
                return;
            }
        }

        if (manager->m_pending_count >= MAX_PENDING)
        {
            ZENGINE_CORE_WARN("[RRM] Pending upload queue full — dropping asset")
            return;
        }
        manager->m_pending[manager->m_pending_count++] = {handle, uuid};
    }

    void RenderResourceManager::OnAssetStale(void* context, const uuids::uuid& uuid)
    {
        auto*           manager = static_cast<RenderResourceManager*>(context);
        std::lock_guard lock(manager->m_uuid_map_mutex);
        for (uint32_t i = 0; i < manager->m_uuid_to_buffer_count; ++i)
        {
            if (manager->m_uuid_to_buffer[i].UUID != uuid)
            {
                continue;
            }

            AssetHandle new_asset = 0;
            if (const AssetRecord* record = manager->m_registry->FindByUUID(uuid))
            {
                new_asset = record->SlotHandle;
            }
            manager->ScheduleSwap(manager->m_uuid_to_buffer[i].Handle, new_asset);
            return;
        }
    }

    void RenderResourceManager::OnAssetRemoved(void* context, const uuids::uuid& uuid, AssetType type)
    {
        if (type == AssetType::TEXTURE)
        {
            static_cast<RenderResourceManager*>(context)->ReleaseTexture(uuid);
        }
    }

    void RenderResourceManager::InitUploadPool()
    {
        // Pre-signaled so the first Wait() before a submit returns immediately.
        uint32_t frame_count = m_device->SwapchainPtr->BufferredFrameCount;
        m_sync_upload_fence  = ZPushStructCtorArgs<Rendering::Primitives::Fence>(m_device->Arena, m_device, true);

        // LastSignal == 0 means "never used yet" for a given frame index.
        m_batch_timeline     = ZPushStructCtorArgs<Rendering::Primitives::Semaphore>(m_device->Arena, m_device, true);
        m_batch_frames.init(m_device->Arena, frame_count, frame_count);
        for (uint32_t i = 0; i < frame_count; ++i)
        {
            m_batch_frames[i] = BatchFrameState{};
        }

        // RenderThread-only, single thread slot — cycles through BufferedFrameCount distinct
        // command buffers instead of resetting and resubmitting the same one every call (see
        // issue #764 follow-up: reusing a single command buffer across many upload cycles was
        // suspected as a factor in an otherwise-unexplained GPU stall).
        m_upload_cmd_mgr = ZPushStructCtor<CommandBufferManager>(m_device->Arena);
        m_upload_cmd_mgr->Initialize(m_device, m_device->SwapchainPtr->BufferredFrameCount, 1);

        m_async_uploads.Initialize(m_device);
    }

    void RenderResourceManager::Shutdown()
    {
        m_scene_resources_sealed.store(true, std::memory_order_release);
        if (!m_device)
        {
            return;
        }
        {
            std::lock_guard lock(m_pending_mutex);
            m_accept_texture_decodes.value.store(false, std::memory_order_release);
        }
        CancelStoppedThumbnailCacheWrites();
        while (m_pending_texture_decodes.value.load(std::memory_order_acquire) > 0)
        {
            std::this_thread::yield();
        }

        DiscardQueuedTextureDecodes();
        m_device->QueueWaitAll();
        CancelTextureReplacements();
        m_async_uploads.Clear();
        for (auto& replacement : m_texture_replacements)
        {
            replacement.ProducerComplete = true;
        }
        // The render thread has joined; submitted consumers are idle (or the
        // device is terminally lost), and no recorded frame can be submitted later.
        ReleaseSceneAndFrameResourcesAfterIdle();
        ZENGINE_VALIDATE_ASSERT(m_pinned_mesh_geometry_count == 0, "RenderResourceManager::Shutdown: explicit geometry pins must retire before their resource owner")
        ZENGINE_VALIDATE_ASSERT(m_texture_version_pin_count == 0, "RenderResourceManager::Shutdown: material/texture pins must retire before their resource owner")
        for (uint32_t i = 0; i < m_mesh_slot_count; ++i)
        {
            RetireMeshGeometry(m_mesh_slots[i].Data);
        }
        // Idle/lost-device shutdown ends every producer's lifetime proof before
        // destroying the semaphore objects referenced by completion records.
        for (auto& producer : m_texture_producer_completions)
        {
            producer = {};
        }
        ShutdownTextureTimelines();
        DiscardTextureDeferrals();

        // No workers or queued uploads remain. Pending tracked work was cancelled
        // by shutdown, so editor-owned releases must not wait for another frame.
        {
            std::lock_guard lock(m_texture_decode_mutex);
            for (TrackedTextureDecode& tracked : m_texture_decode_tracker.Entries)
            {
                if (tracked.State == TextureDecodeState::Pending)
                {
                    tracked.State = TextureDecodeState::Failed;
                }
            }
        }
        // QueueWaitAll proved every producer complete; their semaphores were just
        // retired by ShutdownTextureTimelines and must no longer be polled.
        m_streaming_upload_tickets.clear();
        FlushPendingTextureReleases();
        PollTextureReplacements();
        FlushPendingTextureReleases();
        ZENGINE_VALIDATE_ASSERT(m_texture_replacement_count.load() == 0, "RenderResourceManager::Shutdown: texture replacements must drain before their owner")

        ZENGINE_VALIDATE_ASSERT(m_active_texture_decode_slabs.value.load(std::memory_order_acquire) == 0, "RenderResourceManager::Shutdown: texture decode slab lease was not released")
        for (uint32_t i = 0; i < MAX_CONCURRENT_TEXTURE_DECODES; ++i)
        {
            m_upload_slabs[i].Shutdown();
        }
        m_synchronous_texture_scratch.Shutdown();
        m_texture_task_slab.Shutdown();

        // Arena-allocated objects have no automatic destructor — explicit calls are required.
        if (m_upload_cmd_mgr)
        {
            m_upload_cmd_mgr->Deinitialize();
            m_upload_cmd_mgr = nullptr;
        }
        m_async_uploads.Deinitialize();
        if (m_sync_upload_fence)
        {
            m_sync_upload_fence->~Fence();
            m_sync_upload_fence = nullptr;
        }
        if (m_batch_timeline)
        {
            m_batch_timeline->~Semaphore();
            m_batch_timeline = nullptr;
        }

        // Free all live buffer slots
        if (m_pool.VertexBuffer)
        {
            m_device->GpuMem.FreeBuffer(m_pool.VertexBuffer);
        }
        if (m_pool.IndexBuffer)
        {
            m_device->GpuMem.FreeBuffer(m_pool.IndexBuffer);
        }
        if (m_builtin_vertex_buf)
        {
            m_device->GpuMem.FreeBuffer(m_builtin_vertex_buf);
        }
        if (m_builtin_index_buf)
        {
            m_device->GpuMem.FreeBuffer(m_builtin_index_buf);
        }

        for (uint32_t i = 0; i < m_gbuf_slot_count; ++i)
        {
            if (m_gbuf_slots[i].Generation != 0 && m_gbuf_slots[i].Data)
            {
                m_device->GpuMem.FreeBuffer(m_gbuf_slots[i].Data);
            }
        }

        m_streaming_mgr.Deinitialize();
        m_device                       = nullptr;
        m_scene_descriptor_preparation = nullptr;
        m_registry                     = nullptr;
    }

    void RenderResourceManager::BeginFrame(uint32_t frame_index)
    {
        PollFrameResources();
        // Initialization may leave a builtin-geometry batch open for slot 0.
        // A resize or skipped acquire can make the first rendered slot different.
        // Submit its copies before changing slots or retiring their staging data.
        if (m_batch_mode && m_batch_frame_index != frame_index)
        {
            EndBatchUpload();
        }
        m_active_frame_index = static_cast<uint8_t>(frame_index);
        RetireBatchStagings();
        DispatchQueuedTextureDecodes();
        FlushPendingMeshReleases();
        m_streaming_mgr.Tick(frame_index);
        if (m_streaming_mgr.IsCompactionRequested())
        {
            RunCompaction();
        }
        FlushPendingUploads(frame_index);
        FlushPendingSwaps(frame_index);
        FlushPendingTextureReloads();
        FlushPendingTextureReleases();
        PollSceneResourcePreparations();
    }

    void RenderResourceManager::EndFrame()
    {
        if (m_batch_mode)
        {
            EndBatchUpload();
        }
        PollSceneResourcePreparations();
    }

    void RenderResourceManager::FlushPendingSwaps(uint32_t frame_index)
    {
        uint32_t    count              = 0;
        PendingSwap local[MAX_PENDING] = {};
        {
            std::lock_guard lock(m_pending_swap_mutex);
            count = m_pending_swap_count;
            secure_memcpy(local, sizeof(local), m_pending_swaps, count * sizeof(PendingSwap));
            m_pending_swap_count = 0;
        }
        if (count == 0)
        {
            return;
        }

        // Idempotent — no-op if FlushPendingUploads already opened the batch this frame.
        EnsureBatchOpen(static_cast<uint8_t>(frame_index));

        for (uint32_t i = 0; i < count; ++i)
        {
            const PendingSwap& s = local[i];
            if (s.OldBuffer.Generation & GBUF_GEN_TAG)
            {
                // No AssetHandle-driven re-upload path exists for generic buffers today —
                // every live ScheduleSwap(BufferHandle,...) call carries a mesh handle.
                ZENGINE_LOG_RENDER_WARN("[RRM] Hot-reload swap for generic buffers is not supported — skipping")
            }
            else
            {
                if (s.OldBuffer.Index >= m_mesh_slot_count || m_mesh_slots[s.OldBuffer.Index].Generation != s.OldBuffer.Generation)
                {
                    continue; // stale handle — asset was released before the swap could apply
                }
                MeshSlot new_data = AppendMeshData(s.NewAsset, frame_index);
                if (new_data.VtxCount == 0)
                {
                    ZENGINE_LOG_RENDER_ERR("[RRM] Hot-reload swap failed to re-upload mesh data — old data left in place")
                    continue;
                }
                // A public handle may now address new content while an older immutable
                // version stays allocated for its outstanding renderer-owned pins.
                auto& slot = m_mesh_slots[s.OldBuffer.Index];
                ReplaceMeshGeometry(slot.Data, new_data);
            }
        }
    }

    // Contexts + C-style record callbacks for RecordAndSubmit (no std::function — matches the
    // rest of the engine's zero-heap-alloc convention for hot-path callbacks, see ThreadPool's
    // TaskFn).
    struct GlobalBufferCopyCtx
    {
        VkBuffer     SrcBuffer;
        VkBuffer     DstBuffer;
        VkDeviceSize DstOffset;
        size_t       ByteSize;
    };

    static void RecordGlobalBufferCopy(VkCommandBuffer cmd, void* ctx_ptr)
    {
        auto*        ctx = static_cast<GlobalBufferCopyCtx*>(ctx_ptr);
        VkBufferCopy region{.srcOffset = 0, .dstOffset = ctx->DstOffset, .size = ctx->ByteSize};
        vkCmdCopyBuffer(cmd, ctx->SrcBuffer, ctx->DstBuffer, 1, &region);

        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = ctx->DstBuffer;
        barrier.offset              = ctx->DstOffset;
        barrier.size                = ctx->ByteSize;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    struct RingCopyCtx
    {
        VkBuffer     SrcBuffer;
        VkBuffer     DstBuffer;
        uint32_t     RingOffset;
        VkDeviceSize DstOffset;
        size_t       ByteSize;
    };

    static void RecordRingCopy(VkCommandBuffer cmd, void* ctx_ptr)
    {
        auto*        ctx = static_cast<RingCopyCtx*>(ctx_ptr);
        VkBufferCopy region{.srcOffset = ctx->RingOffset, .dstOffset = ctx->DstOffset, .size = ctx->ByteSize};
        vkCmdCopyBuffer(cmd, ctx->SrcBuffer, ctx->DstBuffer, 1, &region);

        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = ctx->DstBuffer;
        barrier.offset              = ctx->DstOffset;
        barrier.size                = ctx->ByteSize;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    struct StagingCopyCtx
    {
        VkBuffer     SrcBuffer;
        VkBuffer     DstBuffer;
        VkDeviceSize DstOffset;
        size_t       ByteSize;
    };

    static void RecordStagingCopy(VkCommandBuffer cmd, void* ctx_ptr)
    {
        auto*        ctx = static_cast<StagingCopyCtx*>(ctx_ptr);
        VkBufferCopy region{.srcOffset = 0, .dstOffset = ctx->DstOffset, .size = ctx->ByteSize};
        vkCmdCopyBuffer(cmd, ctx->SrcBuffer, ctx->DstBuffer, 1, &region);

        // Same fallback destinations as RecordRingCopy (generic SSBOs, ZUI vertex/index
        // buffers), so the same access/stage transition. Previously relied on the caller
        // fully blocking on a fence before returning — harmless then, but once this runs
        // as part of a deferred batch, the cross-submission consumer needs an explicit
        // barrier, not just ordering.
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = ctx->DstBuffer;
        barrier.offset              = ctx->DstOffset;
        barrier.size                = ctx->ByteSize;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    static void RecordAndSubmit(CommandBuffer* cmd, Rendering::Primitives::Fence* fence, VkQueue queue, void (*record_fn)(VkCommandBuffer, void*), void* record_ctx)
    {
        cmd->ResetState();
        vkResetCommandBuffer(cmd->GetHandle(), 0);
        cmd->Begin();
        record_fn(cmd->GetHandle(), record_ctx);
        cmd->End();

        VkCommandBuffer raw = cmd->GetHandle();
        VkSubmitInfo    submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &raw;

        // Wait before reset — fence may be in-flight under MoltenVK async completion.
        fence->Wait(UINT64_MAX);
        fence->Reset();
        vkQueueSubmit(queue, 1, &submit, fence->GetHandle());
        fence->Wait(UINT64_MAX);
        cmd->ResetState();
    }

    // Derive a per-axis geometry streaming budget from the device's device-local VRAM.
    // Uses 15% of the largest device-local heap, clamped to [GLOBAL_VTX_MIN, GLOBAL_VTX_CAPACITY].
    // The same value is used for both vtx and idx axes (split evenly from the total budget).
    static VkDeviceSize DeriveGeometryBudget(const VkPhysicalDeviceMemoryProperties& props)
    {
        VkDeviceSize largest_device_local = 0;
        for (uint32_t i = 0; i < props.memoryHeapCount; ++i)
        {
            if ((props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && props.memoryHeaps[i].size > largest_device_local)
            {
                largest_device_local = props.memoryHeaps[i].size;
            }
        }

        if (largest_device_local == 0)
        {
            return RenderResourceManager::GLOBAL_VTX_CAPACITY;
        }

        VkDeviceSize half_budget = largest_device_local * 15 / 100 / 2;
        half_budget              = std::max(half_budget, RenderResourceManager::GLOBAL_VTX_MIN);
        half_budget              = std::min(half_budget, RenderResourceManager::GLOBAL_VTX_CAPACITY);
        return half_budget;
    }

    void RenderResourceManager::InitGlobalBuffers()
    {
        VkDeviceSize vtx_capacity = 0;
        VkDeviceSize idx_capacity = 0;

        if (m_device->GeometryStreamingBudget != 0)
        {
            // Explicit project.json override: split evenly, clamp to [min, max].
            VkDeviceSize half = m_device->GeometryStreamingBudget / 2;
            half              = std::max(half, GLOBAL_VTX_MIN);
            half              = std::min(half, GLOBAL_VTX_CAPACITY);
            vtx_capacity      = half;
            idx_capacity      = half;
            ZENGINE_LOG_RENDER_INFO("[RRM] Geometry pool: project override {} MB vtx + {} MB idx", vtx_capacity >> 20, idx_capacity >> 20)
        }
        else
        {
            vtx_capacity = DeriveGeometryBudget(m_device->PhysicalDeviceMemoryProperties);
            idx_capacity = vtx_capacity;
            ZENGINE_LOG_RENDER_INFO("[RRM] Geometry pool: auto-detected {} MB vtx + {} MB idx (15% of largest device-local heap)", vtx_capacity >> 20, idx_capacity >> 20)
        }

        m_pool.VertexBuffer = m_device->GpuMem.AllocateBuffer(vtx_capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::GlobalVertexBuffer");
        m_pool.IndexBuffer  = m_device->GpuMem.AllocateBuffer(idx_capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::GlobalIndexBuffer");
        ZENGINE_VALIDATE_ASSERT(m_pool.VertexBuffer, "RRM: global vertex buffer allocation failed")
        ZENGINE_VALIDATE_ASSERT(m_pool.IndexBuffer, "RRM: global index buffer allocation failed")
        // Live slots plus at most MAX_BUFFERS detached pinned versions can each
        // contribute a hole on both axes. Spare nodes cover edge holes and transient
        // insertion/coalescing during replacement. Admission remains entirely bounded.
        m_pool.Initialize(m_device->Arena, vtx_capacity, idx_capacity, MAX_BUFFERS * 4 + 8);

        m_builtin_vertex_buf = m_device->GpuMem.AllocateBuffer(BUILTIN_VTX_CAPACITY, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::BuiltinVertexBuffer");
        m_builtin_index_buf  = m_device->GpuMem.AllocateBuffer(BUILTIN_IDX_CAPACITY, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::BuiltinIndexBuffer");
        m_builtin_vtx_cursor = 0;
        m_builtin_idx_cursor = 0;
        ZENGINE_VALIDATE_ASSERT(m_builtin_vertex_buf, "RRM: builtin vertex buffer allocation failed")
        ZENGINE_VALIDATE_ASSERT(m_builtin_index_buf, "RRM: builtin index buffer allocation failed")

        m_streaming_mgr.Initialize(m_device, this);
    }

    void RenderResourceManager::RegisterBuiltinGeometry(const void* vtx_data, size_t vtx_bytes, const uint32_t* idx_data, uint32_t idx_count, uint32_t& out_vtx_offset, uint32_t& out_idx_offset)
    {
        const size_t idx_bytes = idx_count * sizeof(uint32_t);
        ZENGINE_VALIDATE_ASSERT(m_builtin_vtx_cursor + vtx_bytes <= BUILTIN_VTX_CAPACITY, "RRM::RegisterBuiltinGeometry: builtin vertex buffer out of space")
        ZENGINE_VALIDATE_ASSERT(m_builtin_idx_cursor + idx_bytes <= BUILTIN_IDX_CAPACITY, "RRM::RegisterBuiltinGeometry: builtin index buffer out of space")

        // Called pre-render-thread (single-threaded init) — the batch this opens stays
        // open until the first real frame joins it or submits it before switching
        // frame slots. Builtin data goes into the pinned builtin buffers, not
        // the streaming global buffers, so a ResetGeometryBuffers never corrupts it.
        EnsureBatchOpen(static_cast<uint8_t>(m_active_frame_index));
        AppendToGlobalBuffer(m_builtin_vertex_buf, vtx_data, vtx_bytes, m_builtin_vtx_cursor, m_active_frame_index);
        AppendToGlobalBuffer(m_builtin_index_buf, idx_data, idx_bytes, m_builtin_idx_cursor, m_active_frame_index);

        out_vtx_offset        = static_cast<uint32_t>(m_builtin_vtx_cursor / (8 * sizeof(float)));
        out_idx_offset        = static_cast<uint32_t>(m_builtin_idx_cursor / sizeof(uint32_t));
        m_builtin_vtx_cursor += vtx_bytes;
        m_builtin_idx_cursor += idx_bytes;
    }

    void RenderResourceManager::ResetGeometryBuffers()
    {
        m_pending_reset.store(true, std::memory_order_release);
    }

    void RenderResourceManager::ResetGeometryBuffersInternal()
    {
        // A reset invalidates registrations, not promises made by older pins. Retained
        // regions remain charged to the same pool; there is no second buffer allocation.
        const bool retain_regions = m_pinned_mesh_geometry_count != 0;
        if (!retain_regions)
        {
            m_pool.Reset();
        }
        for (uint32_t i = 0; i < m_mesh_slot_count; ++i)
        {
            if (retain_regions && m_mesh_slots[i].Generation != 0)
            {
                RetireMeshGeometry(m_mesh_slots[i].Data);
            }
            else
            {
                // Pool reset reclaims GPU regions in bulk, but draw metadata
                // remains individually owned by each version's cache-slab block.
                ReleaseMeshDrawMetadata(m_mesh_slots[i].Data.DrawMetadata);
            }
            m_mesh_slots[i] = {};
        }
        m_mesh_slot_count = 0;

        std::lock_guard lock(m_uuid_map_mutex);
        for (uint32_t i = 0; i < m_uuid_to_buffer_count; ++i)
        {
            m_uuid_to_buffer[i] = {};
        }
        m_uuid_to_buffer_count = 0;
    }

    void RenderResourceManager::RunCompaction()
    {
        // Repacking in place would invalidate immutable pinned offsets. Defer rather
        // than wait, relocate pinned bytes, or silently rewrite an existing promise.
        if (m_pinned_mesh_geometry_count != 0 || !m_registry)
        {
            return;
        }
        // Hold CPU source ownership through preflight and repacking. An idle GPU
        // version may outlive its CPU cache entry; defer rather than reset the
        // pool and then discover a missing source. Active scenes pin their CPU
        // closure, so their streaming reloads do not depend on idle retention.
        auto* assets = AssetManager::Instance();
        if (!assets)
        {
            return;
        }
        std::unique_lock source_lock(assets->IngestMutex, std::try_to_lock);
        if (!source_lock.owns_lock())
        {
            return;
        }
        struct CompactEntry
        {
            uint32_t              SlotIdx = 0;
            Managers::AssetHandle Asset   = 0;
        };
        CompactEntry entries[MAX_UUID_MAP]  = {};
        bool         accounted[MAX_BUFFERS] = {};
        uint32_t     entry_count            = 0;

        {
            std::lock_guard lock(m_uuid_map_mutex);
            for (uint32_t i = 0; i < m_uuid_to_buffer_count; ++i)
            {
                const UUIDBufferPair& pair = m_uuid_to_buffer[i];
                if (!pair.Handle.IsValid())
                {
                    continue;
                }
                uint32_t slot_idx = pair.Handle.Index;
                if (slot_idx >= m_mesh_slot_count || accounted[slot_idx])
                {
                    continue;
                }
                const auto& slot = m_mesh_slots[slot_idx];
                if (slot.Generation != pair.Handle.Generation)
                {
                    continue;
                }
                if (slot.Data.State != StreamingState::Resident)
                {
                    continue;
                }
                const AssetRecord* rec = m_registry->FindByUUID(pair.UUID);
                if (!rec)
                {
                    continue;
                }
                entries[entry_count++] = {slot_idx, rec->SlotHandle};
                accounted[slot_idx]    = true;
            }
        }

        // Refuse an incomplete repack: every live region must have a source entry.
        // Otherwise resetting the pool would leave an unlisted slot pointing into
        // another mesh's new geometry.
        uint32_t resident_count = 0;
        for (uint32_t i = 0; i < m_mesh_slot_count; ++i)
        {
            if (m_mesh_slots[i].Generation != 0 && m_mesh_slots[i].Data.State == StreamingState::Resident)
            {
                ++resident_count;
            }
        }
        if (entry_count != resident_count)
        {
            return;
        }
        for (uint32_t i = 0; i < entry_count; ++i)
        {
            const auto* mesh = AssetManager::GetAsset<AssetMesh>(entries[i].Asset);
            if (!mesh || mesh->Vertices.empty() || mesh->Indices.empty() || mesh->SubMeshes.empty())
            {
                return;
            }
        }

        // Reset the pool: zero cursors, clear free lists. VkBuffer content is irrelevant —
        // every Resident byte will be re-uploaded into the new packed layout below.
        m_pool.Reset();

        // Clear stale regions so no slot holds a dangling offset after the reset.
        for (uint32_t i = 0; i < entry_count; ++i)
        {
            ReleaseMeshDrawMetadata(m_mesh_slots[entries[i].SlotIdx].Data.DrawMetadata);
            m_mesh_slots[entries[i].SlotIdx].Data.DrawMetadata = nullptr;
            m_mesh_slots[entries[i].SlotIdx].Data.Region       = {};
            m_mesh_slots[entries[i].SlotIdx].Data.Version      = 0;
            m_mesh_slots[entries[i].SlotIdx].Data.VtxCount     = 0;
            m_mesh_slots[entries[i].SlotIdx].Data.IdxCount     = 0;
            m_mesh_slots[entries[i].SlotIdx].Data.State        = StreamingState::Unloaded;
        }

        // Re-upload each mesh into a fresh packed region.  EnsureBatchOpen opens the
        // batch if nothing else has yet — it will be closed by EndFrame as normal.
        EnsureBatchOpen(m_active_frame_index);
        for (uint32_t i = 0; i < entry_count; ++i)
        {
            MeshSlot new_data = AppendMeshData(entries[i].Asset, m_active_frame_index);
            if (new_data.VtxCount > 0)
            {
                ReplaceMeshGeometry(m_mesh_slots[entries[i].SlotIdx].Data, new_data);
            }
        }

        m_streaming_mgr.ClearCompactionRequest();
        ZENGINE_LOG_RENDER_INFO("[RRM] Geometry compaction complete — {} meshes re-packed, fragmentation now {:.1f}%", entry_count, m_pool.FragmentationRatio() * 100.f)
    }

    void RenderResourceManager::RetireBatchStagings()
    {
        uint64_t completed = 0;
        vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_batch_timeline->GetHandle(), &completed);

        for (uint32_t i = 0; i < m_batch_frames.size(); ++i)
        {
            // LastSignal covers the previous submission, not newly recorded copies.
            if (m_batch_mode && i == m_batch_frame_index)
            {
                continue;
            }
            BatchFrameState& frame = m_batch_frames[i];
            if (frame.StagingCount == 0)
            {
                continue;
            }
            uint64_t gate = frame.LastSignal;
            if (gate == 0 || gate > completed)
            {
                continue;
            }
            for (uint32_t j = 0; j < frame.StagingCount; ++j)
            {
                m_device->GpuMem.FreeBuffer(frame.StagingBuffers[j]);
            }
            frame.StagingCount = 0;
        }
    }

    void RenderResourceManager::EnsureBatchOpen(uint8_t frame_index)
    {
        if (m_batch_mode && m_batch_frame_index != frame_index)
        {
            EndBatchUpload();
        }
        if (!m_batch_mode)
        {
            BeginBatchUpload(frame_index);
        }
    }

    void RenderResourceManager::BeginBatchUpload(uint8_t frame_index)
    {
        m_batch_frame_index    = frame_index;
        // Instant buffer, not the regular pool's slot 0 — that slot is shared by the two
        // remaining synchronous callers (UpdateBuffer's ring path, UploadFontAtlas), which
        // submit and block before returning; this buffer's submission is deferred instead.
        m_batch_cmd            = m_upload_cmd_mgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, 0, 0, false);

        BatchFrameState& frame = m_batch_frames[frame_index];

        // Wait for this frame index's buffer to be free of its last use before recording
        // into it again. Almost always a no-op — BufferedFrameCount frames have already
        // passed by the time this frame index comes back around.
        if (frame.LastSignal != 0)
        {
            m_batch_timeline->Wait(frame.LastSignal, UINT64_MAX);
        }

        // RetireBatchStagings polls the batch timeline each frame. Any staging buffers
        // still owned by this frame index are released only after its prior batch signal.
        m_batch_cmd->ResetState();
        vkResetCommandBuffer(m_batch_cmd->GetHandle(), 0);
        m_batch_cmd->Begin();
        m_batch_mode = true;
    }

    void RenderResourceManager::EndBatchUpload()
    {
        m_batch_cmd->End();

        // Submit without blocking so a mesh drop never stalls the render thread. Present()
        // consumes the resulting timeline operation in the graphics submission below.
        // Signals m_batch_timeline — a dedicated semaphore with exactly one writer (this
        // function) — rather than DeviceSwapchain::RenderTimeline, which Present() also
        // drives independently.
        uint64_t signal_value                          = ++m_batch_next_value;
        m_batch_frames[m_batch_frame_index].LastSignal = signal_value;

        // The global geometry buffers are bound as whole-buffer storage descriptors.
        // A fresh copy therefore needs to wait for every prior graphics read before
        // writing, even when its byte range is newly allocated: validation cannot infer
        // the shader's per-draw subrange, and separate graphics queues may overlap.
        //
        // RenderTimeline covers the previous frame's final Present() submission. Earlier
        // graphics batches are submitted directly by RenderGraph, however. Relaying those
        // batches through RenderTimeline preserves execution order but not their shader
        // access scope, so the copy must wait on their direct timeline as well.
        auto* const           swapchain                = m_device->SwapchainPtr;
        constexpr auto        copy_wait_stage          = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        // A batch can contain generic UpdateBuffer copies as well as mesh data. Its
        // consumer queue is therefore not known here: it can be graphics or a
        // dedicated compute queue. ALL_COMMANDS is valid for either queue and keeps
        // every possible consumer behind the copy.
        constexpr auto        consumer_wait_stage      = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSemaphoreSubmitInfo copy_waits[2]            = {};
        uint32_t              copy_wait_count          = 0;
        if (swapchain->RenderTimelineNextValue != 0)
        {
            copy_waits[copy_wait_count++] = {
                .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = swapchain->RenderTimeline->GetHandle(),
                .value     = swapchain->RenderTimelineNextValue,
                .stageMask = copy_wait_stage,
            };
        }

        if (swapchain->DirectGraphicsTimeline && swapchain->DirectGraphicsTimelineValue != 0)
        {
            copy_waits[copy_wait_count++] = {
                .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = swapchain->DirectGraphicsTimeline->GetHandle(),
                .value     = swapchain->DirectGraphicsTimelineValue,
                .stageMask = copy_wait_stage,
            };
        }

        if (m_device->QueueSubmit(m_batch_cmd, m_batch_timeline, signal_value, copy_waits, copy_wait_count))
        {
            m_batch_accepted_value = signal_value;
            m_device->EnqueueAsyncGPUOperation({consumer_wait_stage, signal_value, m_batch_timeline});
        }
        else
        {
            // A rejected submit cannot supply a completion gate. Device loss is
            // terminal; never let a later counter value turn it into readiness.
            m_batch_submission_failed                      = true;
            m_batch_frames[m_batch_frame_index].LastSignal = 0;
        }

        // Left in m_batch_frames[m_batch_frame_index] for RetireBatchStagings (or the next
        // BeginBatchUpload for this same frame index) to free once m_batch_timeline proves
        // this copy has completed.
        m_batch_mode = false;
        m_batch_cmd  = nullptr;
    }

    void RenderResourceManager::AppendToGlobalBuffer(BufferView& dst_buf, const void* data, size_t byte_size, VkDeviceSize byte_offset, uint32_t frame_index)
    {
        BufferView staging = m_device->GpuMem.AllocateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging, "RRM::Staging");
        ZENGINE_VALIDATE_ASSERT(staging, "RRM::AppendToGlobalBuffer: staging alloc failed")
        ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, staging.Allocation, 0, byte_size) == VK_SUCCESS, "RRM::AppendToGlobalBuffer: staging copy failed")

        GlobalBufferCopyCtx ctx{staging.Handle, dst_buf.Handle, byte_offset, byte_size};

        // Mesh staging and direct buffer writers open a batch before reaching
        // this point; there is no synchronous fallback path.
        ZENGINE_VALIDATE_ASSERT(m_batch_mode, "RRM::AppendToGlobalBuffer: called without an open batch — call EnsureBatchOpen first")
        ZENGINE_VALIDATE_ASSERT(static_cast<uint8_t>(frame_index) == m_batch_frame_index, "RRM::AppendToGlobalBuffer: frame_index does not match the currently open batch")
        RecordGlobalBufferCopy(m_batch_cmd->GetHandle(), &ctx);
        BatchFrameState& frame = m_batch_frames[m_batch_frame_index];
        ZENGINE_VALIDATE_ASSERT(frame.StagingCount < MAX_PENDING * 2, "RRM::AppendToGlobalBuffer: batch staging overflow")
        frame.StagingBuffers[frame.StagingCount++] = staging;
    }

    const RenderResourceManager::MeshDrawMetadata* RenderResourceManager::CaptureMeshDrawMetadata(TLSFSlab& slab, const AssetMesh& mesh)
    {
        constexpr uint32_t floats_per_vertex = DRAW_VERTEX_BYTES / sizeof(float);
        if (mesh.Vertices.size() % floats_per_vertex != 0 || mesh.SubMeshes.size() > (SIZE_MAX - sizeof(MeshDrawMetadata)) / sizeof(AssetSubMesh))
        {
            return nullptr;
        }
        const size_t vertex_count = mesh.Vertices.size() / floats_per_vertex;
        const size_t index_count  = mesh.Indices.size();
        if (vertex_count > UINT32_MAX || index_count > UINT32_MAX)
        {
            return nullptr;
        }
        for (const auto& submesh : mesh.SubMeshes)
        {
            if (submesh.VertexOffset > vertex_count || submesh.VertexCount > vertex_count - submesh.VertexOffset || submesh.IndexOffset > index_count || submesh.IndexCount > index_count - submesh.IndexOffset)
            {
                return nullptr;
            }
        }
        // Metadata shares the existing bounded cache slab, not the transient
        // decode arena. Refusal preserves both the previous mesh and its pins.
        const size_t bytes = sizeof(MeshDrawMetadata) + mesh.SubMeshes.size() * sizeof(AssetSubMesh);
        auto*        block = slab.TryAllocate(bytes, alignof(MeshDrawMetadata));
        if (!block)
        {
            return nullptr;
        }
        auto* metadata = new (block) MeshDrawMetadata{};
        static_assert(alignof(MeshDrawMetadata) >= alignof(AssetSubMesh));
        auto* submeshes = reinterpret_cast<AssetSubMesh*>(metadata + 1);
        for (size_t i = 0; i < mesh.SubMeshes.size(); ++i)
        {
            new (submeshes + i) AssetSubMesh(mesh.SubMeshes[i]);
        }
        metadata->SubMeshes    = {submeshes, mesh.SubMeshes.size()};
        metadata->BoundsCenter = mesh.BoundsCenter;
        metadata->BoundsRadius = mesh.BoundsRadius;
        metadata->BoundsMin    = mesh.BoundsMin;
        metadata->BoundsMax    = mesh.BoundsMax;
        metadata->BoundsValid  = mesh.BoundsValid;
        metadata->Slab         = &slab;
        return metadata;
    }

    void RenderResourceManager::ReleaseMeshDrawMetadata(const MeshDrawMetadata* metadata)
    {
        if (metadata)
        {
            static_assert(std::is_trivially_destructible_v<AssetSubMesh>);
            auto* slab       = metadata->Slab;
            auto* allocation = const_cast<MeshDrawMetadata*>(metadata);
            allocation->~MeshDrawMetadata();
            slab->Free(allocation);
        }
    }

    RenderResourceManager::MeshSlot RenderResourceManager::AppendMeshData(AssetHandle asset, uint32_t frame_index)
    {
        auto* assets = AssetManager::Instance();
        if (!assets)
        {
            return {};
        }
        // Keep descriptor growth/reload and payload lifetime out of GPU staging.
        std::lock_guard asset_lock(assets->IngestMutex);
        AssetMesh*      mesh = AssetManager::GetAsset<AssetMesh>(asset);
        if (!mesh || mesh->Vertices.empty())
        {
            return {};
        }

        static constexpr uint32_t FLOATS_PER_DRAW_VERTEX = 8;                                      // x,y,z, nx,ny,nz, u,v
        static constexpr uint32_t DRAW_VERTEX_BYTES      = FLOATS_PER_DRAW_VERTEX * sizeof(float); // 32

        size_t                    vert_bytes             = mesh->Vertices.size() * sizeof(float);
        size_t                    idx_bytes              = mesh->Indices.size() * sizeof(uint32_t);
        const uint64_t            version                = NextMeshGeometryVersion();
        if (version == 0)
        {
            ZENGINE_LOG_RENDER_ERR("[RRM] Geometry version identities exhausted")
            return {};
        }
        // Capture validates whole vertices and submesh ranges before allocating
        // geometry or recording any copies, so malformed payloads fail closed.
        const auto* metadata = CaptureMeshDrawMetadata(assets->ContainerSlab, *mesh);
        if (!metadata)
        {
            ZENGINE_LOG_RENDER_ERR("[RRM] Mesh draw metadata invalid or cache slab exhausted")
            return {};
        }
        GeometryRegion region;
        if (!m_pool.Allocate(static_cast<VkDeviceSize>(vert_bytes), static_cast<VkDeviceSize>(idx_bytes), region))
        {
            ReleaseMeshDrawMetadata(metadata);
            ZENGINE_LOG_RENDER_ERR("[RRM] AppendMeshData: geometry pool full")
            return {};
        }

        // Residency reloads can be the first upload work in this frame. Establish
        // their batch here rather than relying on unrelated pending imports.
        EnsureBatchOpen(static_cast<uint8_t>(frame_index));
        AppendToGlobalBuffer(m_pool.VertexBuffer, mesh->Vertices.data(), vert_bytes, region.VtxByteOffset, frame_index);
        AppendToGlobalBuffer(m_pool.IndexBuffer, mesh->Indices.data(), idx_bytes, region.IdxByteOffset, frame_index);

        uint32_t vtx_elem_offset = static_cast<uint32_t>(region.VtxByteOffset / DRAW_VERTEX_BYTES);
        uint32_t idx_elem_offset = static_cast<uint32_t>(region.IdxByteOffset / sizeof(uint32_t));
        uint32_t vtx_elem_count  = static_cast<uint32_t>(mesh->Vertices.size() / FLOATS_PER_DRAW_VERTEX);
        uint32_t idx_elem_count  = static_cast<uint32_t>(mesh->Indices.size());

        ZENGINE_LOG_RENDER_INFO("[RRM] Uploaded mesh: {} verts ({} bytes), {} indices ({} bytes) — vtx@{} idx@{}", vtx_elem_count, vert_bytes, idx_elem_count, idx_bytes, vtx_elem_offset, idx_elem_offset)

        MeshSlot result     = {region, vtx_elem_count, idx_elem_count};
        result.Version      = version;
        result.DrawMetadata = metadata;
        return result;
    }

    BufferHandle RenderResourceManager::DoUploadMesh(AssetHandle asset, uint32_t frame_index)
    {
        const uint32_t slot = AllocMeshSlot();
        if (slot == UINT32_MAX)
        {
            return {};
        }
        MeshSlot data = AppendMeshData(asset, frame_index);
        if (data.VtxCount == 0)
        {
            m_mesh_slots[slot] = {};
            return {};
        }

        m_mesh_slots[slot].Data            = data;
        m_mesh_slots[slot].Data.State      = StreamingState::Resident;
        m_mesh_slots[slot].Data.Referenced = false;
        m_mesh_slots[slot].Data.Pinned     = false;
        return {slot, m_mesh_slots[slot].Generation};
    }

    void RenderResourceManager::FlushPendingUploads(uint32_t frame_index)
    {
        // Compact geometry buffers if a scene reload was requested
        if (m_pending_reset.exchange(false, std::memory_order_acq_rel))
        {
            ResetGeometryBuffersInternal();
        }

        uint32_t      count = 0;
        PendingUpload local[MAX_PENDING];
        {
            std::lock_guard lock(m_pending_mutex);
            count = m_pending_count;
            secure_memcpy(local, sizeof(local), m_pending, count * sizeof(PendingUpload));
            m_pending_count = 0;
        }
        if (count == 0)
        {
            return;
        }

        // Joins this frame's deferred batch (opened here if nothing else has yet) — closed
        // once, by RRM::EndFrame, after everything else that might also join it this frame.
        EnsureBatchOpen(static_cast<uint8_t>(frame_index));
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto existing = FindMeshBuffer(local[i].UUID);
            if (existing.IsValid())
            {
                // Scene preparation may have restored an unloaded registration
                // after reset while this ready notification was still queued.
                if (!IsMeshResident(existing))
                {
                    RequestMeshLoad(existing, local[i].UUID);
                }
                continue;
            }
            if (m_uuid_to_buffer_count == MAX_UUID_MAP)
            {
                ZENGINE_CORE_ERROR("[RRM] Mesh registration capacity exhausted")
                continue;
            }
            BufferHandle h = DoUploadMesh(local[i].Asset, frame_index);
            if (h.IsValid())
            {
                std::lock_guard lock(m_uuid_map_mutex);
                if (m_uuid_to_buffer_count < MAX_UUID_MAP)
                {
                    m_uuid_to_buffer[m_uuid_to_buffer_count++] = {local[i].UUID, h};
                }
            }
            else
            {
                ZENGINE_CORE_ERROR("[RRM] Mesh upload failed for asset handle {}", local[i].Asset)
            }
        }
    }

    void RenderResourceManager::UpdateBuffer(BufferView& dst, const void* data, size_t byte_size, uint32_t dst_offset)
    {
        if (!data || byte_size == 0 || !dst)
        {
            return;
        }

        VkMemoryPropertyFlags mem_flags = 0;
        vmaGetAllocationMemoryProperties(m_device->GpuMem.Allocator, dst.Allocation, &mem_flags);

        if (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            // BAR / HOST_VISIBLE — direct memcpy, no command buffer.
            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, dst.Allocation, dst_offset, byte_size) == VK_SUCCESS, "RRM::UpdateBuffer: host-visible memcpy failed")
            if (!(mem_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            {
                vmaFlushAllocation(m_device->GpuMem.Allocator, dst.Allocation, dst_offset, byte_size);
            }
            return;
        }

        // DEVICE_LOCAL. Two sub-cases with different lifecycles:
        uint32_t ring_offset = 0;
        void*    ring_ptr    = m_device->GpuMem.Ring.Allocate(static_cast<uint32_t>(byte_size), 4, &ring_offset);

        if (ring_ptr)
        {
            // Ring path stays synchronous, deliberately not deferred: GpuAllocator::Ring's
            // retirement (Ring::Drain, driven by VulkanDevice::TickMemory) tracks every
            // chunk in one FIFO compared against RenderTimeline's completed value alone.
            // Stamping a chunk with m_batch_timeline's (future, not-yet-reached) signal
            // value instead would compare it against the wrong counter — the chunk could
            // be reclaimed before the deferred copy on m_batch_timeline ever executes.
            // Fixing that needs Ring to track more than one semaphore, which is out of
            // scope here — so this sub-case keeps the fence-blocking model, just repointed
            // at m_sync_upload_fence.
            secure_memmove(ring_ptr, byte_size, data, byte_size);

            VkQueue        gfx_queue  = m_device->GetQueue(QueueType::GRAPHIC_QUEUE).Handle;
            CommandBuffer* upload_cmd = m_upload_cmd_mgr->GetCommandBuffer(QueueType::GRAPHIC_QUEUE, m_active_frame_index, 0, 0, false);
            RingCopyCtx    ctx{m_device->GpuMem.Ring.Buffer, dst.Handle, ring_offset, dst_offset, byte_size};
            RecordAndSubmit(upload_cmd, m_sync_upload_fence, gfx_queue, RecordRingCopy, &ctx);

            m_device->GpuMem.Ring.Submit(ring_offset, static_cast<uint32_t>(byte_size), m_device->SwapchainPtr->RenderTimelineNextValue);
        }
        else
        {
            // Staging path joins this frame's deferred batch — no GpuAllocator::Ring
            // involvement, so no cross-semaphore retirement hazard.
            EnsureBatchOpen(m_active_frame_index);

            BufferView staging = m_device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging);
            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, staging.Allocation, 0, byte_size) == VK_SUCCESS, "RRM::UpdateBuffer: staging copy failed")

            StagingCopyCtx ctx{staging.Handle, dst.Handle, dst_offset, byte_size};
            RecordStagingCopy(m_batch_cmd->GetHandle(), &ctx);

            BatchFrameState& frame = m_batch_frames[m_batch_frame_index];
            ZENGINE_VALIDATE_ASSERT(frame.StagingCount < MAX_PENDING * 2, "RRM::UpdateBuffer: batch staging overflow")
            frame.StagingBuffers[frame.StagingCount++] = staging;
        }
    }

    void RenderResourceManager::ScheduleSwap(BufferHandle old_handle, AssetHandle new_asset)
    {
        if (!old_handle.IsValid())
        {
            return;
        }
        std::lock_guard lock(m_pending_swap_mutex);
        if (m_pending_swap_count >= MAX_PENDING)
        {
            ZENGINE_LOG_RENDER_WARN("[RRM] Pending swap queue full — dropping hot-reload swap")
            return;
        }
        PendingSwap& s = m_pending_swaps[m_pending_swap_count++];
        s.OldBuffer    = old_handle;
        s.NewAsset     = new_asset;
    }

    bool RenderResourceManager::GetMeshOffsets(BufferHandle handle, uint32_t& vtx_offset, uint32_t& idx_offset) const
    {
        if (!handle.IsValid() || handle.Index >= m_mesh_slot_count)
        {
            return false;
        }
        const auto& slot = m_mesh_slots[handle.Index];
        if (slot.Generation != handle.Generation)
        {
            return false;
        }
        vtx_offset = static_cast<uint32_t>(slot.Data.Region.VtxByteOffset / DRAW_VERTEX_BYTES);
        idx_offset = static_cast<uint32_t>(slot.Data.Region.IdxByteOffset / sizeof(uint32_t));
        return true;
    }

    RenderResourceManager::MeshGeometryPin::MeshGeometryPin(RenderResourceManager* owner, uint32_t record, uint64_t version) : m_owner(owner), m_record(record), m_version(version) {}

    RenderResourceManager::MeshGeometryPin::~MeshGeometryPin()
    {
        Reset();
    }

    RenderResourceManager::MeshGeometryPin::MeshGeometryPin(MeshGeometryPin&& other) noexcept : m_owner(other.m_owner), m_record(other.m_record), m_version(other.m_version)
    {
        other.m_owner   = nullptr;
        other.m_record  = UINT32_MAX;
        other.m_version = 0;
    }

    RenderResourceManager::MeshGeometryPin& RenderResourceManager::MeshGeometryPin::operator=(MeshGeometryPin&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner         = other.m_owner;
            m_record        = other.m_record;
            m_version       = other.m_version;
            other.m_owner   = nullptr;
            other.m_record  = UINT32_MAX;
            other.m_version = 0;
        }
        return *this;
    }

    const RenderResourceManager::MeshGeometrySnapshot* RenderResourceManager::MeshGeometryPin::Get() const
    {
        if (!m_owner || m_record >= MAX_BUFFERS)
        {
            return nullptr;
        }
        const PinnedMeshGeometry& record = m_owner->m_pinned_mesh_geometry[m_record];
        return record.PinCount != 0 && record.Snapshot.Version == m_version ? &record.Snapshot : nullptr;
    }

    void RenderResourceManager::MeshGeometryPin::Reset()
    {
        if (m_owner)
        {
            m_owner->ReleaseMeshGeometryPin(m_record, m_version);
        }
        m_owner   = nullptr;
        m_record  = UINT32_MAX;
        m_version = 0;
    }

    RenderResourceManager::TextureVersionPin::~TextureVersionPin()
    {
        Reset();
    }

    RenderResourceManager::TextureVersionPin::TextureVersionPin(TextureVersionPin&& other) noexcept
    {
        *this = std::move(other);
    }

    RenderResourceManager::TextureVersionPin& RenderResourceManager::TextureVersionPin::operator=(TextureVersionPin&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner         = other.m_owner;
            m_slot          = other.m_slot;
            m_version       = other.m_version;
            other.m_owner   = nullptr;
            other.m_slot    = UINT32_MAX;
            other.m_version = 0;
        }
        return *this;
    }

    const RenderResourceManager::TextureVersionSnapshot* RenderResourceManager::TextureVersionPin::Get() const
    {
        if (!m_owner || m_slot >= TextureVersionCapacity)
        {
            return nullptr;
        }
        const auto& record = m_owner->m_texture_versions[m_slot];
        return record.References && record.Snapshot.Version == m_version ? &record.Snapshot : nullptr;
    }

    void RenderResourceManager::TextureVersionPin::Reset()
    {
        if (m_owner)
        {
            m_owner->ReleaseTextureVersionPin(m_slot, m_version);
        }
        m_owner   = nullptr;
        m_slot    = UINT32_MAX;
        m_version = 0;
    }

    RenderResourceManager::TextureVersionPin RenderResourceManager::PinTextureVersion(Textures::TextureHandle handle)
    {
        return PinTextureVersionInternal(handle, false);
    }

    RenderResourceManager::TextureVersionPin RenderResourceManager::PinGraphTextureVersion(Textures::TextureHandle handle)
    {
        return PinTextureVersionInternal(handle, true);
    }

    RenderResourceManager::TextureVersionPin RenderResourceManager::PinTextureVersionInternal(Textures::TextureHandle handle, bool graph_owned)
    {
        if (!m_device || !handle.Valid() || handle.Index >= TextureVersionCapacity || m_scene_resources_sealed.load(std::memory_order_acquire))
        {
            return {};
        }
        // Serialize with file-upload admission and pending destruction. First
        // admission waits for initial copies/descriptors. A prior admission can
        // survive unrelated work; every managed write invalidates that proof.
        std::scoped_lock lock(m_pending_mutex, m_pending_texture_reload_mutex, m_pending_texture_release_mutex, m_texture_version_mutex);
        const auto&      previous = m_texture_versions[handle.Index];
        const auto&      producer = m_texture_producer_completions[handle.Index];
        if (producer.Texture.Index == handle.Index && producer.Texture.Generation == handle.Generation && producer.PendingSubmission)
        {
            return {};
        }
        const bool previously_admitted = previous.PreviouslyAdmitted && previous.Snapshot.Handle.Index == handle.Index && previous.Snapshot.Handle.Generation == handle.Generation;
        if (!graph_owned && !previously_admitted && (m_pending_texture_reload_count || m_texture_replacement_count.load(std::memory_order_acquire) || HasUnsubmittedTextureWork()))
        {
            return {};
        }
        for (uint32_t i = 0; i < m_pending_texture_release_count; ++i)
        {
            const auto pending = m_pending_texture_releases[i];
            if (pending.Index == handle.Index && pending.Generation == handle.Generation)
            {
                return {};
            }
        }
        const auto  decode  = GetTextureDecodeState(handle);
        const auto* texture = GetTexture(handle);
        if (!texture || decode == TextureDecodeState::Pending || decode == TextureDecodeState::Failed)
        {
            return {};
        }
        const auto* image = m_device->ImageBufferManager.Access(texture->BufferHandle);
        if (!image)
        {
            return {};
        }
        if (graph_owned ? (!texture->Specification.IsAliasable || !image->GetBuffer().Allocation || image->GetBuffer().AllocationRecord == UINT32_MAX || FindStreamingUploadTicket(handle)) : (!image->GetBuffer().OwnsAllocation || texture->Specification.IsAliasable))
        {
            // Asset leases cannot silently admit transient images. Graph leases
            // require allocator-tracked shared memory and no streaming producer.
            return {};
        }
        auto&      record = m_texture_versions[handle.Index];
        const bool same   = record.Snapshot.Handle.Index == handle.Index && record.Snapshot.Handle.Generation == handle.Generation;
        if ((same && record.DestroyRequested) || record.References == UINT32_MAX)
        {
            return {};
        }
        if (record.References == 0)
        {
            if (m_texture_version_sequence == UINT64_MAX)
            {
                return {};
            }
            record                    = {};
            record.Snapshot           = {handle, texture->BufferHandle, ++m_texture_version_sequence, texture->Width, texture->Height, texture->Specification.Format};
            record.PreviouslyAdmitted = true;
        }
        else if (!same)
        {
            return {};
        }
        ++record.References;
        ++m_texture_version_pin_count;
        TextureVersionPin pin = {};
        pin.m_owner           = this;
        pin.m_slot            = static_cast<uint32_t>(handle.Index);
        pin.m_version         = record.Snapshot.Version;
        return pin;
    }

    bool RenderResourceManager::IsTextureVersionPinned(Textures::TextureHandle handle) const
    {
        if (!handle.Valid() || handle.Index >= TextureVersionCapacity)
        {
            return false;
        }
        std::lock_guard lock(m_texture_version_mutex);
        const auto&     record = m_texture_versions[handle.Index];
        return record.References && record.Snapshot.Handle.Generation == handle.Generation;
    }

    bool RenderResourceManager::CanWriteTextureVersion(Textures::TextureHandle handle) const
    {
        if (!handle.Valid())
        {
            return false;
        }
        if (handle.Index >= TextureVersionCapacity)
        {
            return true; // These slots cannot acquire a version pin.
        }
        std::lock_guard lock(m_texture_version_mutex);
        const auto&     record   = m_texture_versions[handle.Index];
        const bool      same     = record.Snapshot.Handle.Index == handle.Index && record.Snapshot.Handle.Generation == handle.Generation;
        const auto&     producer = m_texture_producer_completions[handle.Index];
        const bool      pending  = producer.Texture.Index == handle.Index && producer.Texture.Generation == handle.Generation && (producer.PendingSubmission || producer.Timeline || producer.AcquirePending);
        return !pending && (!same || (!record.References && !record.DestroyRequested));
    }

    bool RenderResourceManager::BeginTextureVersionWrite(Textures::TextureHandle handle)
    {
        if (!handle.Valid())
        {
            return false;
        }
        if (handle.Index >= TextureVersionCapacity)
        {
            return true;
        }
        // Ordinary writes/reconstruction must not replace an image still used
        // by an accepted upload. Poll outside the admission mutex, then recheck
        // the exact proof under that mutex before authorizing mutation.
        if (!TextureProducerComplete(handle))
        {
            return false;
        }
        std::lock_guard lock(m_texture_version_mutex);
        auto&           record   = m_texture_versions[handle.Index];
        const bool      same     = record.Snapshot.Handle.Index == handle.Index && record.Snapshot.Handle.Generation == handle.Generation;
        const auto&     producer = m_texture_producer_completions[handle.Index];
        const bool      pending  = producer.Texture.Index == handle.Index && producer.Texture.Generation == handle.Generation && (producer.PendingSubmission || producer.Timeline || producer.AcquirePending);
        if (pending || (same && (record.References || record.DestroyRequested)))
        {
            return false;
        }
        record.PreviouslyAdmitted = false;
        return true;
    }

    QueueType RenderResourceManager::TextureUploadQueue(Textures::TextureHandle handle, Specifications::ImageLayout layout) const
    {
        // Only fresh, never-graphics-used images start on transfer. Reuploads
        // stay on graphics instead of requiring an unimplemented ownership
        // round-trip back to transfer. Layout alone is insufficient: rejecting
        // an unsubmitted rewrite resets it without undoing earlier GPU ownership.
        if (!m_device->HasSeparateTransferQueue || layout != Specifications::ImageLayout::UNDEFINED || !handle.Valid() || handle.Index >= TextureVersionCapacity)
        {
            return QueueType::GRAPHIC_QUEUE;
        }
        std::lock_guard lock(m_texture_version_mutex);
        const auto&     producer      = m_texture_producer_completions[handle.Index];
        const bool      graphics_only = producer.Texture.Index == handle.Index && producer.Texture.Generation == handle.Generation && producer.GraphicsUploadOnly;
        return graphics_only ? QueueType::GRAPHIC_QUEUE : QueueType::TRANSFER_QUEUE;
    }

    bool RenderResourceManager::BeginTextureUploadRecording(Textures::TextureHandle handle, bool graphics_upload)
    {
        // Async uploads require a bounded lifetime record even when no scene has
        // pinned the image yet. Do not silently bypass ownership beyond the cap.
        if (!handle.Valid() || handle.Index >= TextureVersionCapacity)
        {
            return false;
        }
        std::lock_guard lock(m_texture_version_mutex);
        auto&           record        = m_texture_versions[handle.Index];
        auto&           producer      = m_texture_producer_completions[handle.Index];
        const bool      same          = record.Snapshot.Handle.Index == handle.Index && record.Snapshot.Handle.Generation == handle.Generation;
        const bool      same_producer = producer.Texture.Index == handle.Index && producer.Texture.Generation == handle.Generation;
        if ((same && (record.References || record.DestroyRequested)) || (same_producer && (producer.PendingSubmission || producer.Timeline || producer.AcquirePending)) || !TrackTextureDecode(handle))
        {
            return false;
        }
        record.PreviouslyAdmitted = false;
        if (!same_producer)
        {
            producer         = {};
            producer.Texture = handle;
        }
        // Preserve an earlier accepted producer's completion proof. Rejection
        // of this recording must not erase GPU work that is already in flight.
        producer.PendingSubmission   = true;
        producer.GraphicsUploadOnly |= graphics_upload;
        return true;
    }

    bool RenderResourceManager::RetainTextureForRetirement(Textures::TextureHandle handle, bool force_defer)
    {
        if (!handle.Valid() || handle.Index >= TextureVersionCapacity)
        {
            return false;
        }
        if (m_device && !m_device->GlobalTextures.IsLive(handle))
        {
            return false;
        }
        std::lock_guard lock(m_texture_version_mutex);
        auto&           record = m_texture_versions[handle.Index];
        const bool      same   = record.Snapshot.Handle.Index == handle.Index && record.Snapshot.Handle.Generation == handle.Generation;
        if (!same)
        {
            if (record.References)
            {
                return false; // Stale handle cannot mark another live version.
            }
            record                 = {};
            record.Snapshot.Handle = handle;
        }
        // Seal admission even with zero readers: destruction is deferred and its
        // old generation must not acquire a new lease before the queue drains.
        record.DestroyRequested      = true;
        const auto& producer         = m_texture_producer_completions[handle.Index];
        const bool  producer_pending = producer.Texture.Index == handle.Index && producer.Texture.Generation == handle.Generation && (producer.PendingSubmission || producer.Timeline);
        if (!record.References)
        {
            const uint64_t bit = uint64_t(1) << (handle.Index % 64);
            if (producer_pending || force_defer)
            {
                // Direct device destruction must retain independently submitted
                // producers too, even after the graph acknowledged their ticket.
                m_texture_retirement_ready[handle.Index / 64] |= bit;
            }
            else
            {
                m_texture_retirement_ready[handle.Index / 64] &= ~bit;
            }
        }
        return record.References != 0 || producer_pending || force_defer;
    }

    void RenderResourceManager::ReleaseTextureVersionPin(uint32_t slot, uint64_t version)
    {
        std::lock_guard lock(m_texture_version_mutex);
        auto&           record = m_texture_versions[slot];
        ZENGINE_VALIDATE_ASSERT(record.References && record.Snapshot.Version == version, "Invalid texture-version pin release")
        --record.References;
        --m_texture_version_pin_count;
        if (record.References == 0 && record.DestroyRequested)
        {
            // Fixed, lossless overflow storage. Never make final-pin release
            // depend on obtaining a slot in the smaller producer release queue.
            m_texture_retirement_ready[slot / 64] |= uint64_t(1) << (slot % 64);
        }
    }

    void RenderResourceManager::AdmitTextureVersionRetirements()
    {
        // Caller owns the pending-release mutex. Scan at most 128 bitmap words
        // and admit at most 16 textures per pump. Full queues leave bits intact.
        std::lock_guard lock(m_texture_version_mutex);
        uint32_t        admitted = 0;
        for (uint32_t word = 0; word < TextureVersionCapacity / 64; ++word)
        {
            auto& ready = m_texture_retirement_ready[word];
            while (ready && admitted < TextureRetirementSlice && m_pending_texture_release_count < MAX_PENDING)
            {
                const uint32_t bit    = static_cast<uint32_t>(std::countr_zero(ready));
                const auto     handle = m_texture_versions[word * 64 + bit].Snapshot.Handle;
                bool           queued = false;
                for (uint32_t i = 0; i < m_pending_texture_release_count; ++i)
                {
                    const auto pending = m_pending_texture_releases[i];
                    queued             = queued || (pending.Index == handle.Index && pending.Generation == handle.Generation);
                }
                if (!queued)
                {
                    m_pending_texture_releases[m_pending_texture_release_count++] = handle;
                }
                ready &= ~(uint64_t(1) << bit);
                ++admitted;
            }
            if (admitted == TextureRetirementSlice || m_pending_texture_release_count == MAX_PENDING)
            {
                break;
            }
        }
    }

    RenderResourceManager::MaterialVersionPin::MaterialVersionPin(MaterialVersionPin&& other) noexcept
    {
        *this = std::move(other);
    }

    RenderResourceManager::MaterialVersionPin& RenderResourceManager::MaterialVersionPin::operator=(MaterialVersionPin&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_snapshot       = other.m_snapshot;
            other.m_snapshot = {};
            for (uint32_t map = 0; map < 5; ++map)
            {
                m_textures[map]            = std::move(other.m_textures[map]);
                m_descriptor_versions[map] = std::exchange(other.m_descriptor_versions[map], 0);
            }
            m_descriptors_required = std::exchange(other.m_descriptors_required, false);
        }
        return *this;
    }

    void RenderResourceManager::MaterialVersionPin::Reset()
    {
        m_snapshot             = {};
        m_descriptors_required = false;
        for (auto& version : m_descriptor_versions)
        {
            version = 0;
        }
        for (auto& pin : m_textures)
        {
            pin.Reset();
        }
    }

    RenderResourceManager::MaterialVersionPin RenderResourceManager::RetainMaterialVersion(const Meshes::MeshMaterial& material)
    {
        MaterialVersionPin pin = {};
        if (m_material_version_sequence == UINT64_MAX || m_scene_resources_sealed.load(std::memory_order_acquire))
        {
            return {};
        }
        const uint32_t maps[] = {material.AlbedoMap, material.EmissiveMap, material.NormalMap, material.OpacityMap, material.SpecularMap};
        for (uint32_t map = 0; map < 5; ++map)
        {
            if (maps[map] == INVALID_MAP_HANDLE)
            {
                continue;
            }
            if (!m_device || maps[map] >= m_device->MaxGlobalTexture)
            {
                return {};
            }
            const auto handle   = m_device->GlobalTextures.ToHandle(maps[map]);
            pin.m_textures[map] = PinTextureVersion(handle);
            if (!pin.m_textures[map])
            {
                return {};
            }
        }
        pin.m_snapshot = {material, ++m_material_version_sequence};
        return pin;
    }

    RenderResourceManager::MaterialVersionPin RenderResourceManager::PinMaterialVersion(const uuids::uuid& uuid, MaterialVersionPolicy policy, MaterialVersionError* error)
    {
        const auto fail = [error](MaterialVersionError reason) -> MaterialVersionPin {
            if (error)
            {
                *error = reason;
            }
            return {};
        };
        if (error)
        {
            *error = MaterialVersionError::None;
        }
        if (policy != MaterialVersionPolicy::ExistingBindings && policy != MaterialVersionPolicy::RequiredSceneBindings)
        {
            return fail(MaterialVersionError::ResourcesUnavailable);
        }
        auto* assets = AssetManager::Instance();
        if (!assets || uuid.is_nil())
        {
            return fail(MaterialVersionError::MissingMaterial);
        }
        std::lock_guard lock(assets->IngestMutex);
        const auto*     slot = assets->UUIDToMaterialSlot.find(uuid);
        if (!slot || *slot >= assets->Materials.size() || *slot >= assets->GPUMeshMaterials.size() || assets->Materials[*slot].MaterialUUID != uuid)
        {
            return fail(MaterialVersionError::MissingMaterial);
        }
        const auto&             material      = assets->GPUMeshMaterials[*slot];
        Textures::TextureHandle identities[5] = {};
        if (policy == MaterialVersionPolicy::RequiredSceneBindings)
        {
            const auto&                     source  = assets->Materials[*slot];
            const uuids::uuid               ids[]   = {source.AlbedoTexUUID, source.EmissiveTexUUID, source.NormalTexUUID, source.OpacityTexUUID, source.SpecularTexUUID};
            const Core::Containers::String* paths[] = {&source.AlbedoTexPath, &source.EmissiveTexPath, &source.NormalTexPath, &source.OpacityTexPath, &source.SpecularTexPath};
            const uint32_t                  maps[]  = {material.AlbedoMap, material.EmissiveMap, material.NormalMap, material.OpacityMap, material.SpecularMap};
            for (uint32_t map = 0; map < 5; ++map)
            {
                if (ids[map].is_nil() && paths[map]->empty() && maps[map] == INVALID_MAP_HANDLE)
                {
                    continue; // Schema-defined absent optional map.
                }
                if (ids[map].is_nil() || maps[map] == INVALID_MAP_HANDLE)
                {
                    return fail(MaterialVersionError::MissingRequiredTexture);
                }
                const auto* identity = assets->UUIDToTextureHandle.find(ids[map]);
                if (!identity || !identity->Valid())
                {
                    return fail(MaterialVersionError::MissingRequiredTexture);
                }
                if (identity->Index == assets->FallbackTextureHandle.Index && identity->Generation == assets->FallbackTextureHandle.Generation)
                {
                    return fail(MaterialVersionError::DebugFallback);
                }
                if (identity->Index != maps[map])
                {
                    return fail(MaterialVersionError::TextureIdentityMismatch);
                }
                if (m_device && !GetTexture(*identity))
                {
                    return fail(MaterialVersionError::TextureIdentityMismatch);
                }
                if (GetTextureDecodeState(*identity) == TextureDecodeState::Failed)
                {
                    return fail(MaterialVersionError::TextureLoadFailed);
                }
                identities[map] = *identity;
            }
        }
        auto pin = RetainMaterialVersion(material);
        if (!pin)
        {
            return fail(MaterialVersionError::ResourcesUnavailable);
        }
        if (policy == MaterialVersionPolicy::RequiredSceneBindings)
        {
            for (uint32_t map = 0; map < 5; ++map)
            {
                const auto* texture = pin.GetTexture(map);
                if (texture && (texture->Handle.Index != identities[map].Index || texture->Handle.Generation != identities[map].Generation))
                {
                    return fail(MaterialVersionError::TextureIdentityMismatch);
                }
            }
        }
        return pin;
    }

    bool RenderResourceManager::GetFrameMaterialVersion(const uuids::uuid& uuid, uint32_t* index, MaterialVersionSnapshot* snapshot, bool use_fallback)
    {
        auto* assets = AssetManager::Instance();
        if (!assets)
        {
            return false;
        }
        std::lock_guard lock(assets->IngestMutex);
        const auto*     slot  = assets->UUIDToMaterialSlot.find(uuid);
        const bool      valid = slot && *slot < assets->Materials.size() && *slot < assets->GPUMeshMaterials.size() && assets->Materials[*slot].MaterialUUID == uuid;
        if (!valid && (!use_fallback || assets->GPUMeshMaterials.empty()))
        {
            return false;
        }
        return GetFrameMaterialVersion(assets->GPUMeshMaterials[valid ? *slot : 0], index, snapshot);
    }

    RenderResourceManager::TextureVersionPin RenderResourceManager::CloneTextureVersion(const TextureVersionPin& retained)
    {
        if (retained.m_owner != this || retained.m_slot >= TextureVersionCapacity)
        {
            return {};
        }
        std::lock_guard lock(m_texture_version_mutex);
        auto&           record = m_texture_versions[retained.m_slot];
        if (!record.References || record.Snapshot.Version != retained.m_version || record.References == UINT32_MAX)
        {
            return {};
        }
        // This is another consumer of a live lease, not new admission. Pending
        // deletion seals UUID/handle acquisition, but must not invalidate owners
        // already responsible for retaining this exact image through their use.
        ++record.References;
        ++m_texture_version_pin_count;
        TextureVersionPin pin = {};
        pin.m_owner           = this;
        pin.m_slot            = retained.m_slot;
        pin.m_version         = retained.m_version;
        return pin;
    }

    RenderResourceManager::MaterialVersionPin RenderResourceManager::CloneMaterialVersion(const MaterialVersionPin& retained)
    {
        if (!retained)
        {
            return {};
        }
        MaterialVersionPin pin = {};
        for (uint32_t map = 0; map < 5; ++map)
        {
            if (retained.m_textures[map])
            {
                pin.m_textures[map] = CloneTextureVersion(retained.m_textures[map]);
                if (!pin.m_textures[map])
                {
                    return {}; // All previously cloned references roll back.
                }
            }
        }
        pin.m_snapshot             = retained.m_snapshot;
        pin.m_descriptors_required = retained.m_descriptors_required;
        for (uint32_t map = 0; map < 5; ++map)
        {
            pin.m_descriptor_versions[map] = retained.m_descriptor_versions[map];
        }
        return pin;
    }

    bool RenderResourceManager::GetFrameMaterialVersion(const MaterialVersionPin& retained, uint32_t* index, MaterialVersionSnapshot* snapshot)
    {
        for (const auto& texture : retained.m_textures)
        {
            if (texture.m_owner && texture.m_owner != this)
            {
                return false;
            }
        }
        const auto* source = retained.Get();
        return source && GetFrameMaterialVersionInternal(source->Material, &retained, index, snapshot);
    }

    bool RenderResourceManager::GetFrameMaterialVersion(const Meshes::MeshMaterial& material, uint32_t* index, MaterialVersionSnapshot* snapshot)
    {
        return GetFrameMaterialVersionInternal(material, nullptr, index, snapshot);
    }

    bool RenderResourceManager::GetFrameMaterialVersionInternal(const Meshes::MeshMaterial& material, const MaterialVersionPin* retained, uint32_t* index, MaterialVersionSnapshot* snapshot, bool recording)
    {
        if (!index || !snapshot || m_scene_resources_sealed.load(std::memory_order_acquire) || (retained && !MaterialDescriptorVersionsCurrent(*retained)))
        {
            return false;
        }
        const auto key  = MaterialKey(material);
        uint64_t   hash = 14695981039346656037ull;
        for (const auto word : key)
        {
            hash = (hash ^ word) * 1099511628211ull;
        }
        const auto bucket = static_cast<uint32_t>(hash % FrameMaterialBucketCount);
        for (uint32_t link = m_frame_material_buckets[bucket]; link; link = m_frame_materials[link - 1].Next)
        {
            auto& record = m_frame_materials[link - 1];
            if (record.Hash == hash && MaterialKey(record.Pin.Get()->Material) == key)
            {
                if (retained && retained->m_descriptors_required)
                {
                    // The same immutable map slots may already have a legacy
                    // frame entry. Carry the verified publication proof forward
                    // without changing its shader snapshot or retained images.
                    record.Pin.m_descriptors_required = true;
                    for (uint32_t map = 0; map < 5; ++map)
                    {
                        record.Pin.m_descriptor_versions[map] = retained->m_descriptor_versions[map];
                    }
                }
                // Existing immutable leases are safe to reuse while unrelated
                // uploads/reloads run; no new image is frozen by this admission.
                if (recording && !record.Pending)
                {
                    record.Pending = true;
                    ++m_pending_frame_material_count;
                }
                if (!recording)
                {
                    ++record.Reservations; // bounded by two preparation slots
                }
                *index    = link - 1;
                *snapshot = *record.Pin.Get();
                return true;
            }
        }
        if (m_frame_material_count == FrameMaterialCapacity)
        {
            return false;
        }
        auto pin = retained ? CloneMaterialVersion(*retained) : RetainMaterialVersion(material);
        if (!pin)
        {
            return false;
        }
        uint32_t available = m_frame_material_free_hint;
        while (m_frame_materials[available].Pin)
        {
            available = (available + 1) % FrameMaterialCapacity;
        }
        auto& record                     = m_frame_materials[available];
        record.Pin                       = std::move(pin);
        record.Hash                      = hash;
        record.Next                      = m_frame_material_buckets[bucket];
        record.Pending                   = recording;
        record.Reservations              = recording ? 0 : 1;
        m_frame_material_buckets[bucket] = available + 1;
        m_frame_material_free_hint       = (available + 1) % FrameMaterialCapacity;
        ++m_frame_material_count;
        m_pending_frame_material_count += uint32_t(recording);
        *index                          = available;
        *snapshot                       = *record.Pin.Get();
        return true;
    }

    RenderResourceManager::TextureVersionPin RenderResourceManager::RetainTextureVersion(const TextureVersionPin& retained)
    {
        return CloneTextureVersion(retained);
    }

    bool RenderResourceManager::GetFrameTextureVersion(const TextureVersionPin& retained, TextureVersionSnapshot* snapshot)
    {
        const auto* source = retained.Get();
        if (!snapshot || retained.m_owner != this || !source || source->Handle.Index >= TextureVersionCapacity)
        {
            return false;
        }
        const uint32_t slot   = static_cast<uint32_t>(source->Handle.Index);
        auto&          record = m_frame_textures[slot];
        if (!record.Pin)
        {
            if (m_frame_texture_count == TextureVersionCapacity)
            {
                return false;
            }
            auto pin = CloneTextureVersion(retained);
            if (!pin)
            {
                return false;
            }
            record.Pin                                     = std::move(pin);
            m_frame_texture_slots[m_frame_texture_count++] = slot;
        }
        else if (record.Pin.Get()->Version != source->Version)
        {
            return false; // A retained generation cannot share a slot with another.
        }
        if (!record.Pending)
        {
            record.Pending = true;
            ++m_pending_frame_texture_count;
        }
        *snapshot = *record.Pin.Get();
        return true;
    }

    void RenderResourceManager::UploadFrameMaterials(BufferView& destination)
    {
        if (!m_pending_frame_material_count)
        {
            return;
        }
        ZENGINE_VALIDATE_ASSERT(destination && destination.Size >= FrameMaterialCapacity * sizeof(Meshes::MeshMaterial), "Frame material buffer cannot hold the bounded snapshot table")
        uint32_t                             index = 0;
        std::array<Meshes::MeshMaterial, 16> batch = {};
        while (index < FrameMaterialCapacity)
        {
            if (!m_frame_materials[index].Pending)
            {
                ++index;
                continue;
            }
            const uint32_t start = index;
            uint32_t       count = 0;
            while (index < FrameMaterialCapacity && count < batch.size() && m_frame_materials[index].Pending)
            {
                batch[count++] = m_frame_materials[index++].Pin.Get()->Material;
            }
            UpdateBuffer(destination, batch.data(), count * sizeof(Meshes::MeshMaterial), start * sizeof(Meshes::MeshMaterial));
        }
    }

    void RenderResourceManager::ReleaseFrameMaterialReservation(uint32_t index)
    {
        auto& record = m_frame_materials[index];
        ZENGINE_VALIDATE_ASSERT(record.Reservations != 0, "Scene material reservation underflow")
        if (--record.Reservations || record.Pending)
        {
            return;
        }
        for (uint32_t timeline = 0; timeline < ResourceTimelineCount; ++timeline)
        {
            if (record.RetireValues[timeline] > m_resource_completed[timeline])
            {
                return;
            }
        }
        ReleaseFrameMaterial(index);
    }

    void RenderResourceManager::ReleaseFrameMaterial(uint32_t index)
    {
        auto&      record = m_frame_materials[index];
        const auto bucket = static_cast<uint32_t>(record.Hash % FrameMaterialBucketCount);
        auto*      link   = &m_frame_material_buckets[bucket];
        while (*link != index + 1)
        {
            ZENGINE_VALIDATE_ASSERT(*link != 0, "Frame material missing from its version bucket")
            link = &m_frame_materials[*link - 1].Next;
        }
        *link  = record.Next;
        record = {};
        --m_frame_material_count;
        m_frame_material_free_hint = std::min(m_frame_material_free_hint, index);
    }

    uint64_t RenderResourceManager::NextMeshGeometryVersion()
    {
        if (m_mesh_geometry_version == std::numeric_limits<uint64_t>::max())
        {
            return 0;
        }
        return ++m_mesh_geometry_version;
    }

    RenderResourceManager::MeshGeometryPin RenderResourceManager::PinMeshGeometry(BufferHandle handle)
    {
        if (!handle.IsValid() || (handle.Generation & GBUF_GEN_TAG) || handle.Index >= m_mesh_slot_count)
        {
            return {};
        }
        auto& slot = m_mesh_slots[handle.Index];
        if (slot.Generation != handle.Generation || slot.Data.State != StreamingState::Resident || slot.Data.Version == 0)
        {
            return {};
        }
        if (slot.Data.PinRecord != UINT32_MAX)
        {
            auto& record = m_pinned_mesh_geometry[slot.Data.PinRecord];
            ZENGINE_VALIDATE_ASSERT(record.PinCount != 0 && !record.Detached && record.Snapshot.Version == slot.Data.Version, "Mesh pin record lost its current owner")
            if (record.PinCount == std::numeric_limits<uint32_t>::max())
            {
                return {};
            }
            ++record.PinCount;
            return {this, slot.Data.PinRecord, slot.Data.Version};
        }
        if (m_pinned_mesh_geometry_count == MAX_BUFFERS)
        {
            return {};
        }
        for (uint32_t visited = 0; visited < MAX_BUFFERS; ++visited)
        {
            const uint32_t index   = m_next_mesh_pin_record;
            m_next_mesh_pin_record = (m_next_mesh_pin_record + 1) % MAX_BUFFERS;
            auto& record           = m_pinned_mesh_geometry[index];
            if (record.PinCount != 0)
            {
                continue;
            }
            record = {
                {slot.Data.Region, slot.Data.Version, slot.Data.VtxCount, slot.Data.IdxCount, slot.Data.DrawMetadata},
                handle, 1, false
            };
            slot.Data.PinRecord = index;
            ++m_pinned_mesh_geometry_count;
            return {this, index, slot.Data.Version};
        }
        ZENGINE_VALIDATE_ASSERT(false, "Mesh pin record count disagrees with capacity")
        return {};
    }

    void RenderResourceManager::ReleaseMeshGeometryPin(uint32_t index, uint64_t version)
    {
        ZENGINE_VALIDATE_ASSERT(index < MAX_BUFFERS, "Invalid mesh pin index")
        auto& record = m_pinned_mesh_geometry[index];
        ZENGINE_VALIDATE_ASSERT(record.PinCount != 0 && record.Snapshot.Version == version, "Invalid mesh pin version")
        if (--record.PinCount != 0)
        {
            return;
        }
        if (record.Detached)
        {
            m_pool.Free(record.Snapshot.Region);
            ReleaseMeshDrawMetadata(record.Snapshot.DrawMetadata);
        }
        else
        {
            auto& slot = m_mesh_slots[record.Owner.Index];
            ZENGINE_VALIDATE_ASSERT(slot.Generation == record.Owner.Generation && slot.Data.Version == version && slot.Data.PinRecord == index, "Mesh pin owner changed without retirement")
            slot.Data.PinRecord = UINT32_MAX;
        }
        record = {};
        --m_pinned_mesh_geometry_count;
    }

    bool RenderResourceManager::GetFrameMeshGeometry(BufferHandle handle, MeshGeometrySnapshot* snapshot)
    {
        if (!snapshot || m_scene_resources_sealed.load(std::memory_order_acquire))
        {
            return false;
        }
        auto pin = PinMeshGeometry(handle);
        if (!pin)
        {
            return false;
        }
        return RetainFrameMeshGeometry(std::move(pin), snapshot);
    }

    bool RenderResourceManager::RetainFrameMeshGeometry(MeshGeometryPin&& pin, MeshGeometrySnapshot* snapshot)
    {
        const uint32_t index  = pin.m_record;
        auto&          record = m_pinned_mesh_geometry[index];
        *snapshot             = record.Snapshot;
        if (!record.FrameHeld)
        {
            // Transfer this reference to renderer-owned retirement. Additional
            // draws borrow that same implicit pin, not one pin per instance/frame.
            record.FrameHeld = true;
            ++m_frame_geometry_count;
            pin.m_owner   = nullptr;
            pin.m_record  = UINT32_MAX;
            pin.m_version = 0;
        }
        if (!record.FramePending)
        {
            record.FramePending = true;
            ++m_pending_frame_geometry_count;
        }
        if (!record.Detached)
        {
            MarkMeshReferenced(record.Owner);
        }
        return true;
    }

    void RenderResourceManager::NotifyResourceSubmission(uint32_t timeline_index, Primitives::Semaphore* timeline, uint64_t value)
    {
        ZENGINE_VALIDATE_ASSERT(timeline_index < ResourceTimelineCount && timeline && value != 0, "Invalid resource submission completion point")
        if (m_pending_frame_geometry_count == 0 && m_pending_frame_material_count == 0 && m_pending_frame_texture_count == 0 && !HasPreparedSceneBuffers(true))
        {
            return;
        }
        auto*& bound = m_resource_timelines[timeline_index];
        ZENGINE_VALIDATE_ASSERT(!bound || bound == timeline, "Resource timeline replaced before its previous uses retired")
        bound = timeline;
        StampResourceSubmission(timeline_index, value);
    }

    void RenderResourceManager::StampResourceSubmission(uint32_t timeline_index, uint64_t value)
    {
        ZENGINE_VALIDATE_ASSERT(timeline_index < ResourceTimelineCount && value != 0, "Invalid resource submission stamp")
        if (m_pending_frame_geometry_count == 0 && m_pending_frame_material_count == 0 && m_pending_frame_texture_count == 0 && !HasPreparedSceneBuffers(true))
        {
            return;
        }
        for (uint32_t index = 0; index < m_frame_texture_count; ++index)
        {
            auto& record = m_frame_textures[m_frame_texture_slots[index]];
            if (record.Pending)
            {
                record.RetireValues[timeline_index] = std::max(record.RetireValues[timeline_index], value);
            }
        }
        for (auto& record : m_pinned_mesh_geometry)
        {
            if (record.FramePending)
            {
                record.RetireValues[timeline_index] = std::max(record.RetireValues[timeline_index], value);
            }
        }
        for (auto& record : m_frame_materials)
        {
            if (record.Pending)
            {
                record.RetireValues[timeline_index] = std::max(record.RetireValues[timeline_index], value);
            }
        }
        for (auto& record : m_scene_resources)
        {
            if (record.InitialBuffersPending)
            {
                record.InitialBufferRetireValues[timeline_index] = std::max(record.InitialBufferRetireValues[timeline_index], value);
            }
        }
    }

    void RenderResourceManager::FinishResourceFrame()
    {
        for (auto& record : m_scene_resources)
        {
            record.InitialBuffersPending = false;
        }
        for (uint32_t index = 0; index < m_frame_texture_count; ++index)
        {
            m_frame_textures[m_frame_texture_slots[index]].Pending = false;
        }
        m_pending_frame_texture_count = 0;
        if (m_pending_frame_material_count)
        {
            for (auto& record : m_frame_materials)
            {
                record.Pending = false;
            }
            m_pending_frame_material_count = 0;
        }
        if (m_pending_frame_geometry_count != 0)
        {
            for (auto& record : m_pinned_mesh_geometry)
            {
                record.FramePending = false;
            }
            m_pending_frame_geometry_count = 0;
        }
        // Unsubmitted recording can release immediately. A partially submitted
        // frame still has graph gates; failed presentation never erases those.
        CollectFrameResources(m_resource_completed);
    }

    void RenderResourceManager::CollectFrameResources(const uint64_t* completed)
    {
        if (m_frame_geometry_count == 0 && m_frame_material_count == 0 && m_frame_texture_count == 0)
        {
            return;
        }
        uint32_t texture_index = 0;
        while (texture_index < m_frame_texture_count)
        {
            auto& record = m_frame_textures[m_frame_texture_slots[texture_index]];
            bool  ready  = !record.Pending;
            for (uint32_t timeline = 0; ready && timeline < ResourceTimelineCount; ++timeline)
            {
                ready = record.RetireValues[timeline] <= completed[timeline];
            }
            if (!ready)
            {
                ++texture_index;
                continue;
            }
            record                               = {};
            m_frame_texture_slots[texture_index] = m_frame_texture_slots[--m_frame_texture_count];
        }
        for (uint32_t index = 0; index < MAX_BUFFERS; ++index)
        {
            auto& record = m_pinned_mesh_geometry[index];
            if (!record.FrameHeld || record.FramePending)
            {
                continue;
            }
            bool ready = true;
            for (uint32_t timeline_index = 0; timeline_index < ResourceTimelineCount; ++timeline_index)
            {
                if (record.RetireValues[timeline_index] > completed[timeline_index])
                {
                    ready = false;
                    break;
                }
            }
            if (ready)
            {
                const uint64_t version = record.Snapshot.Version;
                record.FrameHeld       = false;
                for (auto& value : record.RetireValues)
                {
                    value = 0;
                }
                --m_frame_geometry_count;
                ReleaseMeshGeometryPin(index, version);
            }
        }
        for (uint32_t index = 0; index < FrameMaterialCapacity; ++index)
        {
            const auto& record = m_frame_materials[index];
            if (!record.Pin || record.Pending || record.Reservations)
            {
                continue;
            }
            bool ready = true;
            for (uint32_t timeline_index = 0; timeline_index < ResourceTimelineCount; ++timeline_index)
            {
                if (record.RetireValues[timeline_index] > completed[timeline_index])
                {
                    ready = false;
                    break;
                }
            }
            if (ready)
            {
                ReleaseFrameMaterial(index);
            }
        }
    }

    void RenderResourceManager::PollFrameResources()
    {
        if (!m_device || (m_frame_geometry_count == 0 && m_frame_material_count == 0 && m_frame_texture_count == 0 && !HasPreparedSceneBuffers()))
        {
            return;
        }
        for (uint32_t i = 0; i < ResourceTimelineCount; ++i)
        {
            if (m_resource_timelines[i])
            {
                uint64_t completed = 0;
                if (vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_resource_timelines[i]->GetHandle(), &completed) == VK_SUCCESS)
                {
                    m_resource_completed[i] = completed;
                }
            }
        }
        CollectFrameResources(m_resource_completed);
    }

    bool RenderResourceManager::ForgetResourceTimeline(Primitives::Semaphore* timeline)
    {
        if (!timeline)
        {
            return true;
        }
        PollFrameResources();
        for (uint32_t i = 0; i < ResourceTimelineCount; ++i)
        {
            if (m_resource_timelines[i] != timeline)
            {
                continue;
            }
            for (uint32_t index = 0; index < m_frame_texture_count; ++index)
            {
                const auto& record = m_frame_textures[m_frame_texture_slots[index]];
                if (record.RetireValues[i] > m_resource_completed[i])
                {
                    return false;
                }
            }
            for (const auto& record : m_pinned_mesh_geometry)
            {
                if (record.FrameHeld && record.RetireValues[i] > m_resource_completed[i])
                {
                    return false;
                }
            }
            for (const auto& record : m_frame_materials)
            {
                if (record.Pin && record.RetireValues[i] > m_resource_completed[i])
                {
                    return false;
                }
            }
            for (const auto& record : m_scene_resources)
            {
                if (record.InitialBufferRetireValues[i] > m_resource_completed[i])
                {
                    return false;
                }
            }
        }
        for (uint32_t i = 0; i < ResourceTimelineCount; ++i)
        {
            if (m_resource_timelines[i] == timeline)
            {
                for (uint32_t index = 0; index < m_frame_texture_count; ++index)
                {
                    m_frame_textures[m_frame_texture_slots[index]].RetireValues[i] = 0;
                }
                for (auto& record : m_pinned_mesh_geometry)
                {
                    record.RetireValues[i] = 0;
                }
                for (auto& record : m_frame_materials)
                {
                    record.RetireValues[i] = 0;
                }
                for (auto& record : m_scene_resources)
                {
                    record.InitialBufferRetireValues[i] = 0;
                }
                m_resource_timelines[i] = nullptr;
                m_resource_completed[i] = 0;
            }
        }
        return true;
    }

    void RenderResourceManager::ReleaseFrameResourcesAfterIdle()
    {
        for (uint32_t index = 0; index < m_frame_texture_count; ++index)
        {
            m_frame_textures[m_frame_texture_slots[index]] = {};
        }
        m_frame_texture_count         = 0;
        m_pending_frame_texture_count = 0;
        for (uint32_t index = 0; index < FrameMaterialCapacity; ++index)
        {
            if (m_frame_materials[index].Pin)
            {
                ReleaseFrameMaterial(index);
            }
        }
        m_pending_frame_material_count = 0;
        for (uint32_t index = 0; index < MAX_BUFFERS; ++index)
        {
            auto& record = m_pinned_mesh_geometry[index];
            if (record.FrameHeld)
            {
                const uint64_t version = record.Snapshot.Version;
                record.FrameHeld       = false;
                record.FramePending    = false;
                for (auto& value : record.RetireValues)
                {
                    value = 0;
                }
                ReleaseMeshGeometryPin(index, version);
            }
        }
        m_frame_geometry_count         = 0;
        m_pending_frame_geometry_count = 0;
        for (uint32_t i = 0; i < ResourceTimelineCount; ++i)
        {
            m_resource_timelines[i] = nullptr;
            m_resource_completed[i] = 0;
        }
    }

    RenderResourceManager::SceneResourceLease::~SceneResourceLease()
    {
        Reset();
    }

    RenderResourceManager::SceneResourceLease::SceneResourceLease(SceneResourceLease&& other) noexcept : m_owner(std::exchange(other.m_owner, nullptr)), m_id(other.m_id) {}

    RenderResourceManager::SceneResourceLease& RenderResourceManager::SceneResourceLease::operator=(SceneResourceLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner = std::exchange(other.m_owner, nullptr);
            m_id    = other.m_id;
        }
        return *this;
    }

    void RenderResourceManager::SceneResourceLease::Reset() noexcept
    {
        if (m_owner)
        {
            auto& record = m_owner->m_scene_resources[m_id.Slot];
            ZENGINE_VALIDATE_ASSERT(record.Id == m_id, "Scene resource lease outlived its request")
            const auto previous = record.Readers.fetch_sub(1, std::memory_order_release);
            ZENGINE_VALIDATE_ASSERT(previous != 0, "Scene resource reader underflow")
            m_owner = nullptr;
        }
    }

    RenderResourceManager::SceneResourceLease RenderResourceManager::AcquireSceneResourceLease(SceneResourcePreparationId id, uint64_t scene_generation)
    {
        if (!id || id.Slot >= SceneResourcePreparationCapacity || m_scene_resources_sealed.load(std::memory_order_acquire))
        {
            return {};
        }
        auto& record = m_scene_resources[id.Slot];
        if (record.Phase.load(std::memory_order_acquire) != SceneResourcePhase::Ready || record.Id != id || record.ExpectedGeneration + 1 != scene_generation)
        {
            return {};
        }
        const auto environment = record.Environment.load(std::memory_order_acquire);
        if (environment != SceneEnvironmentPreparationState::NotRequested && environment != SceneEnvironmentPreparationState::Ready && environment != SceneEnvironmentPreparationState::Activated)
        {
            return {};
        }
        return AcquireSceneResourceReader(id);
    }

    RenderResourceManager::SceneResourceLease RenderResourceManager::AcquireSceneResourceReader(SceneResourcePreparationId id)
    {
        // Callers validate phase/identity before reaching this point. A main
        // producer cannot race its own cancellation; the render owner cannot
        // race its own reclamation. Both may acquire, so retain with a CAS.
        auto&    record  = m_scene_resources[id.Slot];
        uint32_t readers = record.Readers.load(std::memory_order_relaxed);
        do
        {
            if (readers == UINT32_MAX)
            {
                return {};
            }
        } while (!record.Readers.compare_exchange_weak(readers, readers + 1, std::memory_order_relaxed));
        SceneResourceLease lease = {};
        lease.m_owner            = this;
        lease.m_id               = id;
        return lease;
    }

    RenderResourceManager::SceneResourceLease RenderResourceManager::ClaimSceneEnvironmentPreparation(SceneEnvironmentPreparation* out)
    {
        if (!out || m_scene_resources_sealed.load(std::memory_order_acquire))
        {
            return {};
        }
        uint32_t selected = SceneResourcePreparationCapacity;
        uint64_t serial   = UINT64_MAX;
        for (uint32_t slot = 0; slot < SceneResourcePreparationCapacity; ++slot)
        {
            const auto& record = m_scene_resources[slot];
            if (record.Phase.load(std::memory_order_acquire) == SceneResourcePhase::Ready && record.Environment.load(std::memory_order_acquire) == SceneEnvironmentPreparationState::Queued && (selected == SceneResourcePreparationCapacity || record.Id.Serial < serial))
            {
                selected = slot;
                serial   = record.Id.Serial;
            }
        }
        if (selected == SceneResourcePreparationCapacity)
        {
            return {};
        }
        auto& record = m_scene_resources[selected];
        auto  lease  = AcquireSceneResourceReader(record.Id);
        if (!lease)
        {
            return {};
        }
        record.Environment.store(SceneEnvironmentPreparationState::Preparing, std::memory_order_release);
        if (!GetSceneEnvironmentPreparation(record.Id, out))
        {
            return {};
        }
        return lease;
    }

    bool RenderResourceManager::GetSceneEnvironmentPreparation(SceneResourcePreparationId id, SceneEnvironmentPreparation* out) const
    {
        if (!id || id.Slot >= SceneResourcePreparationCapacity || !out)
        {
            return false;
        }
        const auto& record = m_scene_resources[id.Slot];
        const auto  phase  = record.Phase.load(std::memory_order_acquire);
        if (phase == SceneResourcePhase::Free || phase == SceneResourcePhase::Writing || record.Id != id)
        {
            return false;
        }
        const auto state = record.Environment.load(std::memory_order_acquire);
        *out             = {id, record.EnvironmentInput, record.ExpectedGeneration, state, phase == SceneResourcePhase::Failed || (phase == SceneResourcePhase::Releasing && state != SceneEnvironmentPreparationState::Activated && record.Readers.load(std::memory_order_acquire) <= 1)};
        return true;
    }

    bool RenderResourceManager::CompleteSceneEnvironmentPreparation(SceneResourcePreparationId id, bool success, SceneResourcePreparationError error)
    {
        SceneEnvironmentPreparation preparation = {};
        if (!GetSceneEnvironmentPreparation(id, &preparation) || preparation.Cancelled || preparation.State != SceneEnvironmentPreparationState::Preparing)
        {
            return false;
        }
        auto expected = SceneEnvironmentPreparationState::Preparing;
        m_scene_resources[id.Slot].EnvironmentError.store(success ? SceneResourcePreparationError::None : error == SceneResourcePreparationError::None ? SceneResourcePreparationError::RequiredEnvironment : error, std::memory_order_relaxed);
        return m_scene_resources[id.Slot].Environment.compare_exchange_strong(expected, success ? SceneEnvironmentPreparationState::Ready : SceneEnvironmentPreparationState::Failed, std::memory_order_release);
    }

    bool RenderResourceManager::ActivateSceneEnvironmentPreparation(SceneResourcePreparationId id)
    {
        SceneEnvironmentPreparation preparation = {};
        if (!GetSceneEnvironmentPreparation(id, &preparation) || preparation.Cancelled || m_scene_resources[id.Slot].Readers.load(std::memory_order_acquire) < 2)
        {
            return false;
        }
        auto expected = SceneEnvironmentPreparationState::Ready;
        return m_scene_resources[id.Slot].Environment.compare_exchange_strong(expected, SceneEnvironmentPreparationState::Activated, std::memory_order_release);
    }

    void RenderResourceManager::NotifyPreparedSceneFrameRecorded(SceneResourcePreparationId id)
    {
        if (id && id.Slot < SceneResourcePreparationCapacity)
        {
            auto&      record      = m_scene_resources[id.Slot];
            const auto environment = record.Environment.load(std::memory_order_acquire);
            if (record.Id == id && record.Readers.load(std::memory_order_acquire) && (environment == SceneEnvironmentPreparationState::NotRequested || environment == SceneEnvironmentPreparationState::Activated))
            {
                record.FrameRecorded.store(true, std::memory_order_release);
            }
        }
    }

    bool RenderResourceManager::PrepareSceneResources(ECS::SceneRequestId request, uint64_t expected_generation, std::span<const uuids::uuid> meshes, SceneResourcePreparationId* out, ScenePreparationContent content, const SceneEnvironmentInput* environment, const SceneInitialFrameInput* initial_frame, bool defer_instances)
    {
        if (!request || !expected_generation || expected_generation == UINT64_MAX || !out || *out || meshes.size() > SceneGeometryMeshCapacity || m_scene_resources_sequence == UINT64_MAX || m_scene_resources_sealed.load(std::memory_order_acquire) || (content != ScenePreparationContent::GeometryOnly && content != ScenePreparationContent::MeshMaterials) || (environment && environment->SkyRevision == 0) || (initial_frame && (content != ScenePreparationContent::MeshMaterials || initial_frame->Instances.size() > SceneDrawCapacity)))
        {
            return false;
        }
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            if (meshes[i].is_nil() || (i && !(meshes[i - 1] < meshes[i])))
            {
                return false;
            }
        }
        if (initial_frame)
        {
            if (initial_frame->View.Captured && (!initial_frame->View.Width || !initial_frame->View.Height))
            {
                return false;
            }
            if (initial_frame->Lights.DirectionalCount > std::size(initial_frame->Lights.DirectionalLights) || initial_frame->Lights.PointCount > std::size(initial_frame->Lights.PointLights))
            {
                return false;
            }
            for (const auto& instance : defer_instances ? std::span<const Scenes::MeshInstance>{} : initial_frame->Instances)
            {
                if (instance.MeshUUID.is_nil() || !std::binary_search(meshes.begin(), meshes.end(), instance.MeshUUID))
                {
                    return false;
                }
                for (uint32_t row = 0; row < 4; ++row)
                {
                    for (uint32_t column = 0; column < 4; ++column)
                    {
                        if (!std::isfinite(instance.Transform(row, column)))
                        {
                            return false;
                        }
                    }
                }
            }
        }
        for (const auto& record : m_scene_resources)
        {
            if (record.Phase.load(std::memory_order_acquire) != SceneResourcePhase::Free && record.Id.Request == request)
            {
                return false;
            }
        }
        for (uint32_t i = 0; i < SceneResourcePreparationCapacity; ++i)
        {
            auto&              record   = m_scene_resources[i];
            SceneResourcePhase expected = SceneResourcePhase::Free;
            if (!record.Phase.compare_exchange_strong(expected, SceneResourcePhase::Writing, std::memory_order_acq_rel))
            {
                continue;
            }
            record.Id                 = {request, ++m_scene_resources_sequence, i};
            record.ExpectedGeneration = expected_generation;
            record.MeshCount          = static_cast<uint32_t>(meshes.size());
            std::copy(meshes.begin(), meshes.end(), record.Meshes);
            record.Cursor           = 0;
            record.ResidencyWait    = {};
            record.Content          = content;
            record.EnvironmentInput = environment ? *environment : SceneEnvironmentInput{};
            record.Environment.store(environment ? SceneEnvironmentPreparationState::Queued : SceneEnvironmentPreparationState::NotRequested, std::memory_order_relaxed);
            record.EnvironmentError.store(SceneResourcePreparationError::None, std::memory_order_relaxed);
            record.MaterialCount          = 0;
            record.MaterialMeshCursor     = 0;
            record.SubMeshCursor          = 0;
            record.HasInitialFrame        = initial_frame != nullptr;
            record.InitialInstanceCount   = initial_frame ? static_cast<uint32_t>(initial_frame->Instances.size()) : 0;
            record.InitialCopyCount       = defer_instances ? 0 : record.InitialInstanceCount;
            record.InitialLights          = initial_frame ? initial_frame->Lights : Scenes::LightArrayUBO{};
            record.InitialGrid            = initial_frame ? initial_frame->Grid : Scenes::SceneGridRenderSnapshot{};
            record.InitialView            = initial_frame ? initial_frame->View : Scenes::SceneInitialView{};
            record.InitialDrawCount       = 0;
            record.InitialInstanceCursor  = 0;
            record.InitialSubMeshCursor   = 0;
            record.InitialOpaqueDrawCount = 0;
            for (uint32_t index = 0; index < record.InitialCopyCount; ++index)
            {
                const auto& instance            = initial_frame->Instances[index];
                record.InitialTransforms[index] = instance.Transform;
                record.InitialInstances[index]  = {instance.Entity, static_cast<uint32_t>(std::lower_bound(meshes.begin(), meshes.end(), instance.MeshUUID) - meshes.begin())};
            }
            record.FrameRecorded.store(false, std::memory_order_relaxed);
            std::fill(std::begin(record.MaterialBuckets), std::end(record.MaterialBuckets), 0);
            record.UploadValue   = 0;
            record.Error         = SceneResourcePreparationError::None;
            record.MaterialError = MaterialVersionError::None;
            *out                 = record.Id;
            record.Phase.store(record.InitialCopyCount < record.InitialInstanceCount ? SceneResourcePhase::Collecting : SceneResourcePhase::Queued, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool RenderResourceManager::AppendSceneResourceInstances(SceneResourcePreparationId id, std::span<const Scenes::MeshInstance> instances)
    {
        if (!id || id.Slot >= SceneResourcePreparationCapacity)
        {
            return false;
        }
        auto& record = m_scene_resources[id.Slot];
        if (record.Phase.load(std::memory_order_acquire) != SceneResourcePhase::Collecting || record.Id != id)
        {
            return false;
        }
        bool valid = !instances.empty() && instances.size() <= 64 && instances.size() <= record.InitialInstanceCount - record.InitialCopyCount;
        if (!valid)
        {
            record.Error = SceneResourcePreparationError::InvalidInitialFrame;
            record.Phase.store(SceneResourcePhase::Failed, std::memory_order_release);
            return false;
        }
        for (const auto& instance : instances)
        {
            valid &= !instance.MeshUUID.is_nil() && std::binary_search(record.Meshes, record.Meshes + record.MeshCount, instance.MeshUUID);
            for (uint32_t row = 0; row < 4; ++row)
            {
                for (uint32_t column = 0; column < 4; ++column)
                {
                    valid &= std::isfinite(instance.Transform(row, column));
                }
            }
        }
        if (!valid)
        {
            record.Error = SceneResourcePreparationError::InvalidInitialFrame;
            record.Phase.store(SceneResourcePhase::Failed, std::memory_order_release);
            return false;
        }
        for (const auto& instance : instances)
        {
            const auto index                = record.InitialCopyCount++;
            record.InitialTransforms[index] = instance.Transform;
            record.InitialInstances[index]  = {instance.Entity, static_cast<uint32_t>(std::lower_bound(record.Meshes, record.Meshes + record.MeshCount, instance.MeshUUID) - record.Meshes)};
        }
        if (record.InitialCopyCount == record.InitialInstanceCount)
        {
            record.Phase.store(SceneResourcePhase::Queued, std::memory_order_release);
        }
        return true;
    }

    bool RenderResourceManager::PollSceneResourcePreparation(SceneResourcePreparationId id, SceneResourcePreparationResult* out) const
    {
        if (!id || id.Slot >= SceneResourcePreparationCapacity || !out)
        {
            return false;
        }
        const auto& record = m_scene_resources[id.Slot];
        const auto  phase  = record.Phase.load(std::memory_order_acquire);
        if (phase == SceneResourcePhase::Free || phase == SceneResourcePhase::Writing || record.Id != id)
        {
            return false;
        }
        SceneResourcePreparationResult result = {id, record.ExpectedGeneration, record.MeshCount};
        result.Content                        = record.Content;
        result.FrameRecorded                  = record.FrameRecorded.load(std::memory_order_acquire);
        result.Environment                    = record.Environment.load(std::memory_order_acquire);
        if (phase == SceneResourcePhase::Ready)
        {
            result.State                  = record.Content == ScenePreparationContent::MeshMaterials ? SceneResourcePreparationState::ResourcesRetained : SceneResourcePreparationState::GeometryReady;
            result.MaterialCount          = record.MaterialCount;
            result.InitialDrawsPrepared   = record.HasInitialFrame;
            result.InitialBuffersPrepared = record.HasInitialFrame && record.InitialBufferStage == SceneInitialBufferStage::Complete;
            result.InitialInstanceCount   = record.InitialInstanceCount;
            result.InitialDrawCount       = record.InitialDrawCount;
            if (result.Environment == SceneEnvironmentPreparationState::Failed)
            {
                result.State = SceneResourcePreparationState::Failed;
                result.Error = record.EnvironmentError.load(std::memory_order_relaxed);
            }
            else if (result.Environment == SceneEnvironmentPreparationState::Queued || result.Environment == SceneEnvironmentPreparationState::Preparing)
            {
                result.State = SceneResourcePreparationState::Preparing;
            }
        }
        else if (phase == SceneResourcePhase::Failed)
        {
            result.State         = SceneResourcePreparationState::Failed;
            result.Error         = record.Error;
            result.MaterialError = record.MaterialError;
            result.MaterialCount = record.MaterialCount;
        }
        else if (phase == SceneResourcePhase::Releasing)
        {
            result.State = SceneResourcePreparationState::Releasing;
        }
        *out = result;
        return true;
    }

    bool RenderResourceManager::ReleaseSceneResourcePreparation(SceneResourcePreparationId id)
    {
        if (!id || id.Slot >= SceneResourcePreparationCapacity)
        {
            return false;
        }
        auto& record = m_scene_resources[id.Slot];
        auto  phase  = record.Phase.load(std::memory_order_acquire);
        if (phase == SceneResourcePhase::Free || phase == SceneResourcePhase::Writing || record.Id != id)
        {
            return false;
        }
        while (phase != SceneResourcePhase::Releasing)
        {
            if (record.Phase.compare_exchange_weak(phase, SceneResourcePhase::Releasing, std::memory_order_acq_rel))
            {
                break;
            }
        }
        return true;
    }

    const RenderResourceManager::MeshGeometryPin* RenderResourceManager::FindPreparedSceneGeometry(SceneResourcePreparationId id, const uuids::uuid& mesh) const
    {
        if (!id || id.Slot >= SceneResourcePreparationCapacity)
        {
            return nullptr;
        }
        const auto& record = m_scene_resources[id.Slot];
        const auto  phase  = record.Phase.load(std::memory_order_acquire);
        if ((phase != SceneResourcePhase::Ready && !(phase == SceneResourcePhase::Releasing && record.Readers.load(std::memory_order_acquire))) || record.Id != id)
        {
            return nullptr;
        }
        const auto* found = std::lower_bound(record.Meshes, record.Meshes + record.MeshCount, mesh);
        if (found == record.Meshes + record.MeshCount || *found != mesh)
        {
            return nullptr;
        }
        return &record.Pins[found - record.Meshes];
    }

    bool RenderResourceManager::GetPreparedSceneGeometry(SceneResourcePreparationId id, const uuids::uuid& mesh, MeshGeometrySnapshot* out) const
    {
        const auto* pin = out ? FindPreparedSceneGeometry(id, mesh) : nullptr;
        if (!pin)
        {
            return false;
        }
        const auto* snapshot = pin->Get();
        ZENGINE_VALIDATE_ASSERT(snapshot, "Prepared geometry lost its retained version")
        *out = *snapshot;
        return true;
    }

    uint32_t RenderResourceManager::PreparedMaterialBucket(const SceneResourcePreparation& record, const uuids::uuid& material)
    {
        constexpr uint32_t mask = SceneMaterialCapacity * 2 - 1;
        static_assert((SceneMaterialCapacity * 2 & mask) == 0);
        uint32_t bucket = static_cast<uint32_t>(std::hash<uuids::uuid>{}(material)) & mask;
        // At most half full: the fixed table always has an empty bucket.
        while (record.MaterialBuckets[bucket] && record.Materials[record.MaterialBuckets[bucket] - 1] != material)
        {
            bucket = (bucket + 1) & mask;
        }
        return bucket;
    }

    const RenderResourceManager::MaterialVersionPin* RenderResourceManager::FindPreparedSceneMaterial(SceneResourcePreparationId id, const uuids::uuid& material) const
    {
        if (!id || id.Slot >= SceneResourcePreparationCapacity)
        {
            return nullptr;
        }
        const auto& record = m_scene_resources[id.Slot];
        const auto  phase  = record.Phase.load(std::memory_order_acquire);
        if ((phase != SceneResourcePhase::Ready && !(phase == SceneResourcePhase::Releasing && record.Readers.load(std::memory_order_acquire))) || record.Id != id || record.Content != ScenePreparationContent::MeshMaterials)
        {
            return nullptr;
        }
        const uint32_t entry = record.MaterialBuckets[PreparedMaterialBucket(record, material)];
        if (!entry)
        {
            return nullptr;
        }
        return &record.MaterialPins[entry - 1];
    }

    bool RenderResourceManager::GetPreparedSceneMaterial(SceneResourcePreparationId id, const uuids::uuid& material, MaterialVersionSnapshot* out) const
    {
        const auto* pin = out ? FindPreparedSceneMaterial(id, material) : nullptr;
        if (!pin)
        {
            return false;
        }
        const auto* snapshot = pin->Get();
        ZENGINE_VALIDATE_ASSERT(snapshot, "Prepared material lost its retained version")
        *out = *snapshot;
        return true;
    }

    bool RenderResourceManager::GetFramePreparedSceneGeometry(SceneResourcePreparationId id, const uuids::uuid& mesh, MeshGeometrySnapshot* out)
    {
        const auto* source = out ? FindPreparedSceneGeometry(id, mesh) : nullptr;
        if (!source || m_scene_resources_sealed.load(std::memory_order_acquire))
        {
            return false;
        }
        auto& record = m_pinned_mesh_geometry[source->m_record];
        if (record.PinCount == UINT32_MAX)
        {
            return false;
        }
        ++record.PinCount;
        MeshGeometryPin pin{this, source->m_record, source->m_version};
        return RetainFrameMeshGeometry(std::move(pin), out);
    }

    bool RenderResourceManager::GetFramePreparedSceneMaterial(SceneResourcePreparationId id, const uuids::uuid& material, uint32_t* index, MaterialVersionSnapshot* out)
    {
        const auto* source = index && out ? FindPreparedSceneMaterial(id, material) : nullptr;
        return source && GetFrameMaterialVersion(*source, index, out);
    }

    bool RenderResourceManager::GetPreparedSceneFrame(SceneResourcePreparationId id, Scenes::PreparedSceneFrame* out) const
    {
        if (!id || id.Slot >= SceneResourcePreparationCapacity || !out)
        {
            return false;
        }
        const auto& record = m_scene_resources[id.Slot];
        const auto  phase  = record.Phase.load(std::memory_order_acquire);
        if ((phase != SceneResourcePhase::Ready && !(phase == SceneResourcePhase::Releasing && record.Readers.load(std::memory_order_acquire))) || record.Id != id || !record.HasInitialFrame || record.FrameRecorded.load(std::memory_order_acquire))
        {
            return false;
        }
        *out = {
            {record.InitialTransforms, record.InitialInstanceCount},
            {record.InitialDraws, record.InitialDrawCount},
            record.InitialLights,
            record.InitialGrid,
            record.InitialTransformBuffer ? &record.InitialTransformBuffer : nullptr,
            record.InitialLightBuffer ? &record.InitialLightBuffer : nullptr,
            record.InitialMaterialBuffer ? &record.InitialMaterialBuffer : nullptr,
            record.InitialDrawBuffer ? &record.InitialDrawBuffer : nullptr,
            record.InitialBufferStage == SceneInitialBufferStage::Complete ? std::span<const BufferView>(record.InitialCullingInputs) : std::span<const BufferView>{},
            record.InitialBufferStage == SceneInitialBufferStage::Complete ? std::span<const BufferView>(record.InitialCulledIndirect) : std::span<const BufferView>{},
            record.InitialView,
            record.InitialBufferStage == SceneInitialBufferStage::Complete ? std::span<const BufferView>(record.InitialSelectionTokens) : std::span<const BufferView>{},
            record.InitialOpaqueDrawCount,
            record.InitialView.Captured && record.InitialBufferStage == SceneInitialBufferStage::Complete,
            record.InitialCameraBuffer ? &record.InitialCameraBuffer : nullptr
        };
        return true;
    }

    bool RenderResourceManager::GetFramePreparedSceneFrame(SceneResourcePreparationId id, Scenes::PreparedSceneFrame* out)
    {
        Scenes::PreparedSceneFrame frame = {};
        if (!out || !GetPreparedSceneFrame(id, &frame))
        {
            return false;
        }
        const auto& record = m_scene_resources[id.Slot];
        for (uint32_t mesh = 0; mesh < record.MeshCount; ++mesh)
        {
            MeshGeometrySnapshot geometry = {};
            if (!GetFramePreparedSceneGeometry(id, record.Meshes[mesh], &geometry))
            {
                return false;
            }
        }
        for (uint32_t material = 0; material < record.MaterialCount; ++material)
        {
            MaterialVersionSnapshot snapshot = {};
            uint32_t                index    = 0;
            if (!GetFramePreparedSceneMaterial(id, record.Materials[material], &index, &snapshot) || index != record.FrameMaterialIndices[material])
            {
                return false;
            }
        }
        m_scene_resources[id.Slot].InitialBuffersPending = HasSceneFrameBuffers(record);
        *out                                             = frame;
        return true;
    }

    void RenderResourceManager::PollSceneResourcePreparations()
    {
        bool work = false;
        for (const auto& record : m_scene_resources)
        {
            const auto phase  = record.Phase.load(std::memory_order_acquire);
            work             |= phase == SceneResourcePhase::Collecting || phase == SceneResourcePhase::Queued || phase == SceneResourcePhase::Preparing || phase == SceneResourcePhase::Releasing;
        }
        if (!m_device)
        {
            return;
        }
        uint64_t   completed = 0;
        const bool failed    = m_batch_submission_failed || m_device->IsDeviceLost.load(std::memory_order_acquire);
        if (!work && !failed)
        {
            return;
        }
        if (!failed && m_batch_timeline)
        {
            const VkResult result = vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_batch_timeline->GetHandle(), &completed);
            if (result != VK_SUCCESS)
            {
                m_device->CheckDeviceLost(result, "Scene geometry preparation: upload completion query");
                if (!m_device->IsDeviceLost.load(std::memory_order_acquire))
                {
                    return; // Unknown completion is never readiness.
                }
            }
        }
        TickSceneResourcePreparations(completed, failed || m_device->IsDeviceLost.load(std::memory_order_acquire));
    }

    void RenderResourceManager::TickSceneResourcePreparations(uint64_t completed, bool upload_failed)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.ResourcePreparation");
        uint32_t       budget = SceneResourceSliceOperations;
        const uint32_t first  = m_scene_resources_turn++ % SceneResourcePreparationCapacity;
        for (uint32_t offset = 0; offset < SceneResourcePreparationCapacity; ++offset)
        {
            auto& record = m_scene_resources[(first + offset) % SceneResourcePreparationCapacity];
            auto  phase  = record.Phase.load(std::memory_order_acquire);
            if (upload_failed && phase == SceneResourcePhase::Ready)
            {
                record.Error = SceneResourcePreparationError::UploadFailed;
                record.Phase.compare_exchange_strong(phase, SceneResourcePhase::Failed, std::memory_order_release);
                continue;
            }
            if (phase == SceneResourcePhase::Queued)
            {
                if (!record.Phase.compare_exchange_strong(phase, SceneResourcePhase::Preparing, std::memory_order_acq_rel))
                {
                    continue;
                }
                BeginSceneFrameBufferPreparation(record);
                phase = SceneResourcePhase::Preparing;
            }
            const auto upload_complete = [&] { return record.UploadValue <= m_batch_accepted_value && record.UploadValue <= completed; };
            if (phase == SceneResourcePhase::Releasing)
            {
                if (record.Readers.load(std::memory_order_acquire))
                {
                    continue;
                }
                if (!upload_failed && !upload_complete())
                {
                    continue;
                }
                bool buffers_ready = !record.InitialBuffersPending;
                for (uint32_t timeline = 0; timeline < ResourceTimelineCount; ++timeline)
                {
                    buffers_ready &= record.InitialBufferRetireValues[timeline] <= m_resource_completed[timeline];
                }
                if (!buffers_ready)
                {
                    continue; // Device loss is not successful consumer completion.
                }
                if (HasSceneFrameBuffers(record) && budget)
                {
                    ReleaseSceneFrameBuffers(record, &budget);
                }
                if (HasSceneFrameBuffers(record))
                {
                    continue;
                }
                while (record.MaterialCount && budget)
                {
                    const auto index = --record.MaterialCount;
                    ReleaseFrameMaterialReservation(record.FrameMaterialIndices[index]);
                    record.MaterialPins[index].Reset();
                    --budget;
                }
                while (!record.MaterialCount && record.Cursor && budget)
                {
                    record.Pins[--record.Cursor].Reset();
                    --budget;
                }
                if (!record.Cursor && !record.MaterialCount)
                {
                    // Identity/input stay untouched until the producer claims Free.
                    record.Phase.store(SceneResourcePhase::Free, std::memory_order_release);
                }
                continue;
            }
            if (phase != SceneResourcePhase::Preparing)
            {
                continue;
            }
            if (upload_failed)
            {
                record.Error = SceneResourcePreparationError::UploadFailed;
            }
            while (!upload_failed && record.Cursor < record.MeshCount && budget)
            {
                if (record.Phase.load(std::memory_order_acquire) != SceneResourcePhase::Preparing)
                {
                    break;
                }
                --budget;
                auto handle = FindMeshBuffer(record.Meshes[record.Cursor]);
                if (!handle.IsValid() && record.Content == ScenePreparationContent::MeshMaterials)
                {
                    AssetHandle source = 0;
                    if (m_registry && m_registry->TryReadSlot(record.Meshes[record.Cursor], AssetType::MESH, &source) && AssetManager::ReadAssetHandleType(source) == AssetType::MESH)
                    {
                        // A CPU cache hit does not emit another OnReady callback.
                        // Restore bounded render-owned identity after reset without
                        // requiring a viewport draw or decoding on this thread.
                        std::lock_guard lock(m_uuid_map_mutex);
                        const uint32_t  slot = m_uuid_to_buffer_count < MAX_UUID_MAP ? AllocMeshSlot() : UINT32_MAX;
                        if (slot == UINT32_MAX)
                        {
                            record.Error = SceneResourcePreparationError::GeometryCapacity;
                            break;
                        }
                        m_mesh_slots[slot].Data                    = {};
                        handle                                     = {slot, m_mesh_slots[slot].Generation};
                        m_uuid_to_buffer[m_uuid_to_buffer_count++] = {record.Meshes[record.Cursor], handle};
                    }
                }
                if (!handle.IsValid())
                {
                    record.Error = SceneResourcePreparationError::MissingGeometry;
                    break;
                }
                if (!IsMeshResident(handle))
                {
                    if (record.Content == ScenePreparationContent::MeshMaterials && handle.Index < m_mesh_slot_count && m_mesh_slots[handle.Index].Generation == handle.Generation)
                    {
                        const auto& slot = m_mesh_slots[handle.Index].Data;
                        if (slot.LoadFailed && record.ResidencyWait == handle)
                        {
                            record.Error = SceneResourcePreparationError::UploadFailed;
                            break;
                        }
                        if (slot.State == StreamingState::Pending)
                        {
                            record.ResidencyWait = handle;
                            break;
                        }
                        AssetHandle source = 0;
                        if (slot.State == StreamingState::Unloaded && m_registry && m_registry->TryReadSlot(record.Meshes[record.Cursor], AssetType::MESH, &source) && AssetManager::ReadAssetHandleType(source) == AssetType::MESH)
                        {
                            // Queue saturation is backpressure, not readiness or
                            // missing content. The next slice retries admission.
                            if (RequestMeshLoad(handle, record.Meshes[record.Cursor]))
                            {
                                record.ResidencyWait = handle;
                            }
                            break;
                        }
                    }
                    record.Error = SceneResourcePreparationError::NotResident;
                    break;
                }
                auto pin = PinMeshGeometry(handle);
                if (!pin)
                {
                    record.Error = SceneResourcePreparationError::PinCapacity;
                    break;
                }
                // Resident permits drawing behind queued upload waits; it does
                // not prove completion. Conservatively cover every batch recorded
                // before acquiring this version, including an open batch.
                if (m_batch_mode && m_batch_next_value == UINT64_MAX)
                {
                    record.Error = SceneResourcePreparationError::UploadFailed;
                    break;
                }
                record.UploadValue           = std::max(record.UploadValue, m_batch_next_value + uint64_t(m_batch_mode));
                record.Pins[record.Cursor++] = std::move(pin);
                record.ResidencyWait         = {};
            }
            // Walk only the immutable metadata owned by the retained versions.
            // One submesh (including duplicates) or mesh boundary costs one op,
            // so empty meshes and highly shared materials cannot bypass slicing.
            while (!upload_failed && record.Error == SceneResourcePreparationError::None && record.Cursor == record.MeshCount && record.Content == ScenePreparationContent::MeshMaterials && record.MaterialMeshCursor < record.MeshCount && budget)
            {
                if (record.Phase.load(std::memory_order_acquire) != SceneResourcePhase::Preparing)
                {
                    break;
                }
                --budget;
                const auto* metadata = record.Pins[record.MaterialMeshCursor].Get()->DrawMetadata;
                if (!metadata)
                {
                    record.Error = SceneResourcePreparationError::MissingDrawMetadata;
                    break;
                }
                if (record.SubMeshCursor == metadata->SubMeshes.size())
                {
                    ++record.MaterialMeshCursor;
                    record.SubMeshCursor = 0;
                    continue;
                }
                const auto&    material = metadata->SubMeshes[record.SubMeshCursor].MaterialUUID;
                const uint32_t bucket   = PreparedMaterialBucket(record, material);
                if (!record.MaterialBuckets[bucket])
                {
                    if (record.MaterialCount == SceneMaterialCapacity)
                    {
                        record.Error = SceneResourcePreparationError::MaterialCapacity;
                        break;
                    }
                    MaterialVersionError material_error = MaterialVersionError::None;
                    auto                 pin            = PinMaterialVersion(material, MaterialVersionPolicy::RequiredSceneBindings, &material_error);
                    if (!pin)
                    {
                        if (material_error == MaterialVersionError::ResourcesUnavailable && HasPendingTextureUploads())
                        {
                            break; // Existing decode/descriptor work is backpressure, not a missing dependency.
                        }
                        record.Error         = material_error == MaterialVersionError::MissingRequiredTexture || material_error == MaterialVersionError::DebugFallback || material_error == MaterialVersionError::TextureIdentityMismatch || material_error == MaterialVersionError::TextureLoadFailed ? SceneResourcePreparationError::RequiredMaterialBinding : SceneResourcePreparationError::MaterialNotRetainable;
                        record.MaterialError = material_error;
                        break;
                    }
                    bool texture_upload_failed = false;
                    if (!SceneTextureProducersReady(pin, &texture_upload_failed))
                    {
                        if (texture_upload_failed)
                        {
                            record.Error = SceneResourcePreparationError::UploadFailed;
                        }
                        break;
                    }
                    const auto descriptors = PrepareMaterialDescriptorVersions(pin);
                    if (descriptors != SceneTextureDescriptorState::Ready)
                    {
                        if (descriptors == SceneTextureDescriptorState::Failed)
                        {
                            record.Error = SceneResourcePreparationError::RequiredDescriptor;
                        }
                        break;
                    }
                    if (m_batch_mode && m_batch_next_value == UINT64_MAX)
                    {
                        record.Error = SceneResourcePreparationError::UploadFailed;
                        break;
                    }
                    record.UploadValue                  = std::max(record.UploadValue, m_batch_next_value + uint64_t(m_batch_mode));
                    uint32_t                frame_index = 0;
                    MaterialVersionSnapshot snapshot    = {};
                    if (!GetFrameMaterialVersionInternal(pin.Get()->Material, &pin, &frame_index, &snapshot, false))
                    {
                        record.Error = SceneResourcePreparationError::MaterialCapacity;
                        break;
                    }
                    record.FrameMaterialIndices[record.MaterialCount] = frame_index;
                    record.InitialMaterialEntries[frame_index]        = record.MaterialCount + 1;
                    record.InitialMaterialTableSize                   = std::max(record.InitialMaterialTableSize, frame_index + 1);
                    record.Materials[record.MaterialCount]            = material;
                    record.MaterialPins[record.MaterialCount]         = std::move(pin);
                    record.MaterialBuckets[bucket]                    = ++record.MaterialCount;
                }
                ++record.SubMeshCursor;
            }
            // Expand exact retained metadata before publication, one submesh or
            // instance boundary per operation. A changed cache cannot alter this
            // draw layout or defer discovering an over-budget scene until draw.
            while (!upload_failed && record.Error == SceneResourcePreparationError::None && record.Content == ScenePreparationContent::MeshMaterials && record.MaterialMeshCursor == record.MeshCount && record.InitialInstanceCursor < record.InitialInstanceCount && budget)
            {
                if (record.Phase.load(std::memory_order_acquire) != SceneResourcePhase::Preparing)
                {
                    break;
                }
                --budget;
                const auto& instance = record.InitialInstances[record.InitialInstanceCursor];
                const auto* geometry = record.Pins[instance.MeshIndex].Get();
                const auto* metadata = geometry ? geometry->DrawMetadata : nullptr;
                if (!metadata || metadata->SubMeshes.empty())
                {
                    record.Error = SceneResourcePreparationError::InvalidInitialFrame;
                    break;
                }
                if (record.InitialSubMeshCursor == metadata->SubMeshes.size())
                {
                    ++record.InitialInstanceCursor;
                    record.InitialSubMeshCursor = 0;
                    continue;
                }
                if (record.InitialDrawCount == SceneDrawCapacity)
                {
                    record.Error = SceneResourcePreparationError::DrawCapacity;
                    break;
                }
                const auto&    submesh = metadata->SubMeshes[record.InitialSubMeshCursor];
                const uint32_t entry   = record.MaterialBuckets[PreparedMaterialBucket(record, submesh.MaterialUUID)];
                if (!entry || !submesh.VertexCount || !submesh.IndexCount || uint64_t(submesh.VertexOffset) + submesh.VertexCount > geometry->VertexCount || uint64_t(submesh.IndexOffset) + submesh.IndexCount > geometry->IndexCount || uint64_t(geometry->VertexOffset()) + submesh.VertexOffset > UINT32_MAX || uint64_t(geometry->IndexOffset()) + submesh.IndexOffset > UINT32_MAX)
                {
                    record.Error = SceneResourcePreparationError::InvalidInitialFrame;
                    break;
                }
                auto& draw                     = record.InitialDraws[record.InitialDrawCount];
                draw                           = {};
                draw.Allocation.VertexOffset   = geometry->VertexOffset() + submesh.VertexOffset;
                draw.Allocation.VertexCount    = submesh.VertexCount;
                draw.Allocation.IndexOffset    = geometry->IndexOffset() + submesh.IndexOffset;
                draw.Allocation.IndexCount     = submesh.IndexCount;
                draw.Allocation.InstanceCount  = 1;
                draw.Allocation.TransformId    = record.InitialInstanceCursor;
                draw.Allocation.MaterialId     = record.FrameMaterialIndices[entry - 1];
                draw.WorldBounds               = Scenes::MakeDrawWorldBounds(record.InitialTransforms[record.InitialInstanceCursor], metadata->BoundsCenter, metadata->BoundsRadius);
                draw.Entity                    = instance.Entity;
                draw.Allocation.PickToken      = instance.Entity.IsValid() ? record.InitialDrawCount + 1 : 0;
                draw.Transparent               = record.MaterialPins[entry - 1].Get()->Material.AlphaMode == Materials::AlphaMode::Blend;
                record.InitialOpaqueDrawCount += !draw.Transparent;
                if (!std::isfinite(draw.WorldBounds.x) || !std::isfinite(draw.WorldBounds.y) || !std::isfinite(draw.WorldBounds.z) || !std::isfinite(draw.WorldBounds.w))
                {
                    record.Error = SceneResourcePreparationError::InvalidInitialFrame;
                    break;
                }
                ++record.InitialDrawCount;
                ++record.InitialSubMeshCursor;
            }
            if (!upload_failed && record.Error == SceneResourcePreparationError::None && record.HasInitialFrame && record.InitialInstanceCursor == record.InitialInstanceCount && record.MaterialMeshCursor == record.MeshCount)
            {
                PrepareSceneFrameBuffers(record, budget);
            }
            auto expected = SceneResourcePhase::Preparing;
            if (record.Error != SceneResourcePreparationError::None)
            {
                record.Phase.compare_exchange_strong(expected, SceneResourcePhase::Failed, std::memory_order_release);
            }
            else if (record.Cursor == record.MeshCount && (record.Content == ScenePreparationContent::GeometryOnly || record.MaterialMeshCursor == record.MeshCount) && record.InitialInstanceCursor == record.InitialInstanceCount && (!record.HasInitialFrame || !m_device || !m_device->LogicalDevice || record.InitialBufferStage == SceneInitialBufferStage::Complete) && upload_complete())
            {
                // Never overwrite a concurrent cancellation with a ready result.
                record.Phase.compare_exchange_strong(expected, SceneResourcePhase::Ready, std::memory_order_release);
            }
        }
    }

    SceneTextureDescriptorState RenderResourceManager::PrepareMaterialDescriptorVersions(MaterialVersionPin& material)
    {
        bool pending = false;
        for (uint32_t map = 0; map < 5; ++map)
        {
            const auto* texture = material.GetTexture(map);
            if (!texture)
            {
                continue; // An absent optional map has no descriptor dependency.
            }
            if (!m_scene_descriptor_preparation)
            {
                return SceneTextureDescriptorState::Failed;
            }
            const auto state = m_scene_descriptor_preparation->PrepareSceneTextureDescriptor(texture->Handle, texture->Image, &material.m_descriptor_versions[map]);
            if (state == SceneTextureDescriptorState::Failed)
            {
                return state;
            }
            pending |= state == SceneTextureDescriptorState::Pending;
        }
        material.m_descriptors_required = !pending;
        return pending ? SceneTextureDescriptorState::Pending : MaterialDescriptorVersionsCurrent(material) ? SceneTextureDescriptorState::Ready : SceneTextureDescriptorState::Failed;
    }

    bool RenderResourceManager::MaterialDescriptorVersionsCurrent(const MaterialVersionPin& material) const
    {
        if (!material.m_descriptors_required)
        {
            return true; // Legacy callers have not requested the scene binding contract.
        }
        for (uint32_t map = 0; map < 5; ++map)
        {
            const auto* texture = material.GetTexture(map);
            if (texture && (!m_scene_descriptor_preparation || !m_scene_descriptor_preparation->IsSceneTextureDescriptorCurrent(texture->Handle, texture->Image, material.m_descriptor_versions[map])))
            {
                return false;
            }
        }
        return true;
    }

    bool RenderResourceManager::SceneTextureProducersReady(const MaterialVersionPin& material, bool* failed)
    {
        for (uint32_t map = 0; map < 5; ++map)
        {
            const auto* texture = material.GetTexture(map);
            if (texture && !TextureProducerComplete(texture->Handle, failed))
            {
                return false;
            }
        }
        return true;
    }

    bool RenderResourceManager::TextureProducerComplete(Textures::TextureHandle handle, bool* failed)
    {
        TextureProducerCompletion producer   = {};
        bool                      has_record = false;
        if (handle.Valid() && handle.Index < TextureVersionCapacity)
        {
            std::lock_guard lock(m_texture_version_mutex);
            const auto&     record = m_texture_producer_completions[handle.Index];
            has_record             = record.Texture.Index == handle.Index && record.Texture.Generation == handle.Generation;
            if (has_record)
            {
                producer = record;
            }
        }
        // Legacy handles outside the versioned slot range retain their ticket
        // completion gate. Admitted scene images use the persistent slot record.
        if (handle.Index >= TextureVersionCapacity)
        {
            if (const auto* ticket = FindStreamingUploadTicket(handle))
            {
                producer = {ticket->Texture, ticket->CompletionTimeline, ticket->CompletionValue};
            }
        }
        if (producer.PendingSubmission)
        {
            return false;
        }
        if (!producer.Timeline)
        {
            return true;
        }
        uint64_t       completed = 0;
        const VkResult result    = vkGetSemaphoreCounterValue(m_device->LogicalDevice, producer.Timeline->GetHandle(), &completed);
        if (result != VK_SUCCESS)
        {
            m_device->CheckDeviceLost(result, "Texture producer completion query");
            if (failed)
            {
                *failed = m_device->IsDeviceLost.load(std::memory_order_acquire);
            }
            return false;
        }
        if (completed < producer.Value)
        {
            return false;
        }
        if (has_record)
        {
            std::lock_guard lock(m_texture_version_mutex);
            auto&           record = m_texture_producer_completions[handle.Index];
            if (record.Texture.Index != handle.Index || record.Texture.Generation != handle.Generation || record.Timeline != producer.Timeline || record.Value != producer.Value || record.PendingSubmission)
            {
                return false; // A different producer cannot inherit this observation.
            }
            record.Timeline = nullptr; // Observed completion, not just accepted consumption.
            record.Value    = 0;
        }
        return true;
    }

    bool RenderResourceManager::HasPreparedSceneBuffers(bool pending_only) const
    {
        for (const auto& record : m_scene_resources)
        {
            if (HasSceneFrameBuffers(record) && (!pending_only || record.InitialBuffersPending))
            {
                return true;
            }
        }
        return false;
    }

    bool RenderResourceManager::HasSceneFrameBuffers(const SceneResourcePreparation& record)
    {
        if (record.InitialTransformBuffer || record.InitialLightBuffer || record.InitialMaterialBuffer || record.InitialDrawBuffer || record.InitialCameraBuffer)
        {
            return true;
        }
        for (uint32_t frame = 0; frame < Scenes::SceneData::MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            if (record.InitialCullingInputs[frame] || record.InitialCulledIndirect[frame] || record.InitialSelectionTokens[frame])
            {
                return true;
            }
        }
        return false;
    }

    void RenderResourceManager::BeginSceneFrameBufferPreparation(SceneResourcePreparation& record)
    {
        record.InitialTransformUploadCursor    = 0;
        record.InitialMaterialTableSize        = 1;
        record.InitialMaterialUploadCursor     = 0;
        record.InitialDrawUploadCursor         = 0;
        record.InitialCullingUploadCursor      = 0;
        record.InitialCullingOpaqueCursor      = 0;
        record.InitialCullingTransparentCursor = 0;
        record.InitialBufferFrame              = 0;
        record.InitialBufferStage              = record.InitialView.Captured ? SceneInitialBufferStage::BuildCullingOrder : SceneInitialBufferStage::AllocateTransforms;
        record.InitialOrderCursor              = 0;
        record.InitialOrderOpaqueCursor        = 0;
        record.InitialOrderTransparentCursor   = 0;
        record.InitialBuffersPending           = false;
        std::fill(std::begin(record.InitialMaterialEntries), std::end(record.InitialMaterialEntries), 0);
        std::fill(std::begin(record.InitialBufferRetireValues), std::end(record.InitialBufferRetireValues), 0);
    }

    void RenderResourceManager::PrepareSceneFrameBuffers(SceneResourcePreparation& record, uint32_t& budget)
    {
        if (!m_device || !m_device->LogicalDevice)
        {
            return; // CPU-only hosts do not claim GPU buffer preparation.
        }
        auto& allocator = m_device->GpuMem;
        while (budget && record.InitialBufferStage != SceneInitialBufferStage::Complete && record.Error == SceneResourcePreparationError::None)
        {
            if (record.Phase.load(std::memory_order_acquire) != SceneResourcePhase::Preparing)
            {
                return;
            }
            --budget;
            VkResult status = VK_SUCCESS;
            switch (record.InitialBufferStage)
            {
                case SceneInitialBufferStage::BuildCullingOrder:
                {
                    const auto end = std::min(record.InitialDrawCount, record.InitialOrderCursor + 64);
                    for (; record.InitialOrderCursor < end; ++record.InitialOrderCursor)
                    {
                        const auto  index = record.InitialOrderCursor;
                        const auto& draw  = record.InitialDraws[index];
                        if (draw.Transparent)
                        {
                            const auto center                                                                                  = record.InitialView.Camera.View * Core::Maths::Vec4f(draw.WorldBounds.x, draw.WorldBounds.y, draw.WorldBounds.z, 1.f);
                            record.InitialViewDepths[index]                                                                    = -center.z;
                            record.InitialCullingOrder[record.InitialOpaqueDrawCount + record.InitialOrderTransparentCursor++] = index;
                        }
                        else
                        {
                            record.InitialCullingOrder[record.InitialOrderOpaqueCursor++] = index;
                        }
                    }
                    if (record.InitialOrderCursor == record.InitialDrawCount)
                    {
                        record.InitialOrderCursor = 0;
                        record.InitialBufferStage = SceneInitialBufferStage::HeapifyTransparentOrder;
                    }
                    break;
                }
                case SceneInitialBufferStage::HeapifyTransparentOrder:
                case SceneInitialBufferStage::SortTransparentOrder:
                {
                    auto*      begin  = record.InitialCullingOrder + record.InitialOpaqueDrawCount;
                    const auto before = [&](uint32_t left, uint32_t right) { return Materials::TransparentDepthBefore(record.InitialViewDepths[left], left, record.InitialViewDepths[right], right); };
                    // Each operation processes at most 64 heap steps, each
                    // bounded by log2(SceneDrawCapacity), without extra storage.
                    if (record.InitialBufferStage == SceneInitialBufferStage::HeapifyTransparentOrder)
                    {
                        const auto end = std::min(record.InitialOrderTransparentCursor, record.InitialOrderCursor + 64);
                        while (record.InitialOrderCursor < end)
                        {
                            std::push_heap(begin, begin + ++record.InitialOrderCursor, before);
                        }
                        if (record.InitialOrderCursor == record.InitialOrderTransparentCursor)
                        {
                            record.InitialBufferStage = SceneInitialBufferStage::SortTransparentOrder;
                        }
                    }
                    else
                    {
                        for (uint32_t step = 0; step < 64 && record.InitialOrderCursor > 1; ++step)
                        {
                            std::pop_heap(begin, begin + record.InitialOrderCursor--, before);
                        }
                        if (record.InitialOrderCursor <= 1)
                        {
                            record.InitialBufferStage = SceneInitialBufferStage::AllocateTransforms;
                        }
                    }
                    break;
                }
                case SceneInitialBufferStage::AllocateTransforms:
                    record.InitialTransformBuffer = allocator.TryAllocateBuffer(std::max(1u, record.InitialInstanceCount) * sizeof(Core::Maths::Mat4f), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "PreparedSceneTransforms", &status);
                    if (!record.InitialTransformBuffer)
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferAllocation;
                        break;
                    }
                    record.InitialBufferStage = SceneInitialBufferStage::WriteTransforms;
                    break;
                case SceneInitialBufferStage::WriteTransforms:
                {
                    constexpr uint32_t batch_size = 64;
                    const auto         identity   = Core::Maths::Identity<Core::Maths::Mat4f>();
                    const auto         count      = record.InitialInstanceCount ? std::min(batch_size, record.InitialInstanceCount - record.InitialTransformUploadCursor) : 1u;
                    const auto*        source     = record.InitialInstanceCount ? record.InitialTransforms + record.InitialTransformUploadCursor : &identity;
                    if (!allocator.WriteHostBuffer(record.InitialTransformBuffer, source, count * sizeof(Core::Maths::Mat4f), record.InitialTransformUploadCursor * sizeof(Core::Maths::Mat4f), &status))
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferWrite;
                        break;
                    }
                    record.InitialTransformUploadCursor += count;
                    if (record.InitialTransformUploadCursor >= record.InitialInstanceCount)
                    {
                        record.InitialBufferStage = SceneInitialBufferStage::AllocateLights;
                    }
                    break;
                }
                case SceneInitialBufferStage::AllocateLights:
                    record.InitialLightBuffer = allocator.TryAllocateBuffer(sizeof(Scenes::LightArrayUBO), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "PreparedSceneLights", &status);
                    if (!record.InitialLightBuffer)
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferAllocation;
                        break;
                    }
                    record.InitialBufferStage = SceneInitialBufferStage::WriteLights;
                    break;
                case SceneInitialBufferStage::WriteLights:
                    if (!allocator.WriteHostBuffer(record.InitialLightBuffer, &record.InitialLights, sizeof(record.InitialLights), 0, &status))
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferWrite;
                        break;
                    }
                    record.InitialBufferStage = SceneInitialBufferStage::AllocateMaterials;
                    break;
                case SceneInitialBufferStage::AllocateMaterials:
                    record.InitialMaterialBuffer = allocator.TryAllocateBuffer(record.InitialMaterialTableSize * sizeof(Meshes::MeshMaterial), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "PreparedSceneMaterials", &status);
                    if (!record.InitialMaterialBuffer)
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferAllocation;
                        break;
                    }
                    record.InitialBufferStage = SceneInitialBufferStage::WriteMaterials;
                    break;
                case SceneInitialBufferStage::WriteMaterials:
                {
                    std::array<Meshes::MeshMaterial, 16> batch = {};
                    const auto                           count = std::min(static_cast<uint32_t>(batch.size()), record.InitialMaterialTableSize - record.InitialMaterialUploadCursor);
                    for (uint32_t index = 0; index < count; ++index)
                    {
                        const auto entry = record.InitialMaterialEntries[record.InitialMaterialUploadCursor + index];
                        if (entry)
                        {
                            batch[index] = record.MaterialPins[entry - 1].Get()->Material;
                        }
                    }
                    if (!allocator.WriteHostBuffer(record.InitialMaterialBuffer, batch.data(), count * sizeof(Meshes::MeshMaterial), record.InitialMaterialUploadCursor * sizeof(Meshes::MeshMaterial), &status))
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferWrite;
                        break;
                    }
                    record.InitialMaterialUploadCursor += count;
                    if (record.InitialMaterialUploadCursor == record.InitialMaterialTableSize)
                    {
                        record.InitialBufferStage = SceneInitialBufferStage::AllocateDraws;
                    }
                    break;
                }
                case SceneInitialBufferStage::AllocateDraws:
                    record.InitialDrawBuffer = allocator.TryAllocateBuffer(std::max(1u, record.InitialDrawCount) * sizeof(Meshes::SubMeshAllocation), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "PreparedSceneDraws", &status);
                    if (!record.InitialDrawBuffer)
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferAllocation;
                        break;
                    }
                    record.InitialBufferStage = SceneInitialBufferStage::WriteDraws;
                    break;
                case SceneInitialBufferStage::WriteDraws:
                {
                    std::array<Meshes::SubMeshAllocation, 64> batch = {};
                    const auto                                count = std::min(static_cast<uint32_t>(batch.size()), std::max(1u, record.InitialDrawCount) - record.InitialDrawUploadCursor);
                    for (uint32_t index = 0; index < count && record.InitialDrawCount; ++index)
                    {
                        batch[index] = record.InitialDraws[record.InitialDrawUploadCursor + index].Allocation;
                    }
                    if (!allocator.WriteHostBuffer(record.InitialDrawBuffer, batch.data(), count * sizeof(Meshes::SubMeshAllocation), record.InitialDrawUploadCursor * sizeof(Meshes::SubMeshAllocation), &status))
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferWrite;
                        break;
                    }
                    record.InitialDrawUploadCursor += count;
                    if (record.InitialDrawUploadCursor >= record.InitialDrawCount)
                    {
                        record.InitialBufferStage = SceneInitialBufferStage::AllocateCullingInput;
                    }
                    break;
                }
                case SceneInitialBufferStage::AllocateCullingInput:
                    record.InitialCullingInputs[record.InitialBufferFrame] = allocator.TryAllocateBuffer(std::max(1u, record.InitialDrawCount) * sizeof(Scenes::FrustumCullingInput), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "PreparedSceneCullingInput", &status);
                    if (!record.InitialCullingInputs[record.InitialBufferFrame])
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferAllocation;
                        break;
                    }
                    record.InitialBufferStage              = SceneInitialBufferStage::WriteCullingInput;
                    record.InitialCullingUploadCursor      = 0;
                    record.InitialCullingOpaqueCursor      = 0;
                    record.InitialCullingTransparentCursor = record.InitialOpaqueDrawCount;
                    break;
                case SceneInitialBufferStage::WriteCullingInput:
                {
                    std::array<Scenes::FrustumCullingInput, 64> opaque            = {};
                    std::array<Scenes::FrustumCullingInput, 64> transparent       = {};
                    uint32_t                                    opaque_count      = 0;
                    uint32_t                                    transparent_count = 0;
                    const auto                                  count             = std::min(static_cast<uint32_t>(opaque.size()), std::max(1u, record.InitialDrawCount) - record.InitialCullingUploadCursor);
                    for (uint32_t index = 0; index < count && record.InitialDrawCount; ++index)
                    {
                        const auto                  input_index = record.InitialCullingUploadCursor + index;
                        const auto                  draw_index  = record.InitialView.Captured ? record.InitialCullingOrder[input_index] : input_index;
                        const auto&                 draw        = record.InitialDraws[draw_index];
                        Scenes::FrustumCullingInput input       = {
                            .WorldBounds = draw.WorldBounds, .Command = {.vertexCount = draw.Allocation.IndexCount, .instanceCount = 1, .firstVertex = 0, .firstInstance = draw_index}
                        };
                        if (draw.Transparent)
                        {
                            transparent[transparent_count++] = input;
                        }
                        else
                        {
                            opaque[opaque_count++] = input;
                        }
                    }
                    if (!record.InitialDrawCount)
                    {
                        opaque_count = 1; // Defined empty storage; never submitted as a draw.
                    }
                    const auto& buffer = record.InitialCullingInputs[record.InitialBufferFrame];
                    if ((opaque_count && !allocator.WriteHostBuffer(buffer, opaque.data(), opaque_count * sizeof(Scenes::FrustumCullingInput), record.InitialCullingOpaqueCursor * sizeof(Scenes::FrustumCullingInput), &status)) || (transparent_count && !allocator.WriteHostBuffer(buffer, transparent.data(), transparent_count * sizeof(Scenes::FrustumCullingInput), record.InitialCullingTransparentCursor * sizeof(Scenes::FrustumCullingInput), &status)))
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferWrite;
                        break;
                    }
                    record.InitialCullingUploadCursor      += count;
                    record.InitialCullingOpaqueCursor      += opaque_count;
                    record.InitialCullingTransparentCursor += transparent_count;
                    if (record.InitialCullingUploadCursor >= record.InitialDrawCount)
                    {
                        if (++record.InitialBufferFrame == Scenes::SceneData::MAX_FRAMES_IN_FLIGHT)
                        {
                            record.InitialBufferFrame = 0;
                            record.InitialBufferStage = SceneInitialBufferStage::AllocateCulledIndirect;
                        }
                        else
                        {
                            record.InitialBufferStage = SceneInitialBufferStage::AllocateCullingInput;
                        }
                    }
                    break;
                }
                case SceneInitialBufferStage::AllocateCulledIndirect:
                    // Compute fully writes the output before graphics reads it;
                    // unlike mapped inputs, it needs allocation, not a host upload.
                    record.InitialCulledIndirect[record.InitialBufferFrame] = allocator.TryAllocateBuffer(std::max(1u, record.InitialDrawCount) * sizeof(VkDrawIndirectCommand), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, GpuMemoryDomain::DeviceGeometry, "PreparedSceneCulledIndirect", &status);
                    if (!record.InitialCulledIndirect[record.InitialBufferFrame])
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferAllocation;
                        break;
                    }
                    if (++record.InitialBufferFrame == Scenes::SceneData::MAX_FRAMES_IN_FLIGHT)
                    {
                        record.InitialBufferFrame = 0;
                        record.InitialBufferStage = SceneInitialBufferStage::AllocateSelectionTokens;
                    }
                    break;
                case SceneInitialBufferStage::AllocateSelectionTokens:
                    // Token zero is reserved. Candidate-local storage prevents
                    // outgoing selection feedback from leaking into the first frame.
                    record.InitialSelectionTokens[record.InitialBufferFrame] = allocator.TryAllocateBuffer((record.InitialDrawCount + 1) * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "PreparedSceneSelectionTokens", &status);
                    if (!record.InitialSelectionTokens[record.InitialBufferFrame])
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferAllocation;
                        break;
                    }
                    record.InitialSelectionUploadCursor = 0;
                    record.InitialBufferStage           = SceneInitialBufferStage::WriteSelectionTokens;
                    break;
                case SceneInitialBufferStage::WriteSelectionTokens:
                {
                    const std::array<uint32_t, 256> zeros = {};
                    const auto                      count = std::min(static_cast<uint32_t>(zeros.size()), record.InitialDrawCount + 1 - record.InitialSelectionUploadCursor);
                    if (!allocator.WriteHostBuffer(record.InitialSelectionTokens[record.InitialBufferFrame], zeros.data(), count * sizeof(uint32_t), record.InitialSelectionUploadCursor * sizeof(uint32_t), &status))
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferWrite;
                        break;
                    }
                    record.InitialSelectionUploadCursor += count;
                    if (record.InitialSelectionUploadCursor == record.InitialDrawCount + 1)
                    {
                        record.InitialBufferStage = ++record.InitialBufferFrame == Scenes::SceneData::MAX_FRAMES_IN_FLIGHT ? (record.InitialView.Captured ? SceneInitialBufferStage::AllocateCamera : SceneInitialBufferStage::Complete) : SceneInitialBufferStage::AllocateSelectionTokens;
                    }
                    break;
                }
                case SceneInitialBufferStage::AllocateCamera:
                    record.InitialCameraBuffer = allocator.TryAllocateBuffer(sizeof(Renderers::UBOCameraLayout), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, GpuMemoryDomain::HostUniform, "PreparedSceneCamera", &status);
                    if (!record.InitialCameraBuffer)
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferAllocation;
                        break;
                    }
                    record.InitialBufferStage = SceneInitialBufferStage::WriteCamera;
                    break;
                case SceneInitialBufferStage::WriteCamera:
                {
                    const auto&                camera  = record.InitialView.Camera;
                    Renderers::UBOCameraLayout uniform = {.View = camera.View, .Projection = camera.Projection, .Position = Core::Maths::Vec4f(camera.Position, 1.f)};
                    if (!std::isfinite(camera.Position.x) || !std::isfinite(camera.Position.y) || !std::isfinite(camera.Position.z) || !Core::Maths::Geometry::TryInverse(camera.Projection * camera.View, &uniform.InvViewProj))
                    {
                        record.Error = SceneResourcePreparationError::InvalidInitialFrame;
                        break;
                    }
                    if (!allocator.WriteHostBuffer(record.InitialCameraBuffer, &uniform, sizeof(uniform), 0, &status))
                    {
                        record.Error = SceneResourcePreparationError::InitialBufferWrite;
                        break;
                    }
                    record.InitialBufferStage = SceneInitialBufferStage::Complete;
                    break;
                }
                case SceneInitialBufferStage::Complete:
                    break;
            }
            m_device->CheckDeviceLost(status, "Scene immutable buffer preparation");
        }
    }

    void RenderResourceManager::ReleaseSceneFrameBuffers(SceneResourcePreparation& record, uint32_t* budget)
    {
        const auto release = [&](BufferView& buffer) {
            if (buffer && (!budget || *budget))
            {
                m_device->GpuMem.FreeBuffer(buffer);
                if (budget)
                {
                    --*budget;
                }
            }
        };
        release(record.InitialTransformBuffer);
        release(record.InitialLightBuffer);
        release(record.InitialMaterialBuffer);
        release(record.InitialDrawBuffer);
        release(record.InitialCameraBuffer);
        for (uint32_t frame = 0; frame < Scenes::SceneData::MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            release(record.InitialCullingInputs[frame]);
            release(record.InitialCulledIndirect[frame]);
            release(record.InitialSelectionTokens[frame]);
        }
    }

    void RenderResourceManager::ReleaseSceneAndFrameResourcesAfterIdle()
    {
        // Scene reservations refer into the frame-material table. Release them
        // before forcibly clearing idle frame holds, including partially prepared
        // candidates that were cancelled during engine shutdown.
        ReleaseSceneResourcePreparationsAfterIdle();
        ReleaseFrameResourcesAfterIdle();
    }

    void RenderResourceManager::ReleaseSceneResourcePreparationsAfterIdle()
    {
        for (auto& record : m_scene_resources)
        {
            ZENGINE_VALIDATE_ASSERT(record.Readers.load(std::memory_order_acquire) == 0, "Scene resource mailbox leases survived joined-reader shutdown")
            ReleaseSceneFrameBuffers(record);
            record.InitialBuffersPending = false;
            std::fill(std::begin(record.InitialBufferRetireValues), std::end(record.InitialBufferRetireValues), 0);
            while (record.MaterialCount)
            {
                const auto index = --record.MaterialCount;
                ReleaseFrameMaterialReservation(record.FrameMaterialIndices[index]);
                record.MaterialPins[index].Reset();
            }
            while (record.Cursor)
            {
                record.Pins[--record.Cursor].Reset();
            }
            record.Phase.store(SceneResourcePhase::Free, std::memory_order_release);
        }
    }

    void RenderResourceManager::RetireMeshGeometry(MeshSlot& geometry)
    {
        if (geometry.PinRecord != UINT32_MAX)
        {
            auto& record = m_pinned_mesh_geometry[geometry.PinRecord];
            ZENGINE_VALIDATE_ASSERT(record.PinCount != 0 && !record.Detached && record.Snapshot.Version == geometry.Version, "Cannot retire a mismatched mesh geometry version")
            record.Detached = true;
        }
        else if (geometry.Region.VtxByteSize != 0 || geometry.Region.IdxByteSize != 0)
        {
            m_pool.Free(geometry.Region);
        }
        if (geometry.PinRecord == UINT32_MAX)
        {
            ReleaseMeshDrawMetadata(geometry.DrawMetadata);
        }
        geometry.DrawMetadata = nullptr;
        geometry.Region       = {};
        geometry.Version      = 0;
        geometry.PinRecord    = UINT32_MAX;
        geometry.VtxCount     = 0;
        geometry.IdxCount     = 0;
    }

    void RenderResourceManager::ReplaceMeshGeometry(MeshSlot& geometry, const MeshSlot& replacement)
    {
        const bool pinned = geometry.Pinned;
        RetireMeshGeometry(geometry);
        geometry            = replacement;
        geometry.State      = StreamingState::Resident;
        geometry.Referenced = false;
        geometry.Pinned     = pinned;
    }

    bool RenderResourceManager::RequestMeshLoad(BufferHandle handle, const uuids::uuid& uuid)
    {
        AssetHandle source = 0;
        if (!m_registry || !m_registry->TryReadSlot(uuid, AssetType::MESH, &source) || AssetManager::ReadAssetHandleType(source) != AssetType::MESH)
        {
            return false;
        }
        return m_streaming_mgr.RequestLoad({uuid, source, handle, 0});
    }

    bool RenderResourceManager::IsMeshResident(BufferHandle handle) const
    {
        if (!handle.IsValid() || handle.Index >= m_mesh_slot_count)
        {
            return false;
        }
        const auto& slot = m_mesh_slots[handle.Index];
        if (slot.Generation != handle.Generation)
        {
            return false;
        }
        return slot.Data.State == StreamingState::Resident;
    }

    void RenderResourceManager::MarkMeshReferenced(BufferHandle handle)
    {
        if (!handle.IsValid() || handle.Index >= m_mesh_slot_count)
        {
            return;
        }
        auto& slot = m_mesh_slots[handle.Index];
        if (slot.Generation == handle.Generation)
        {
            slot.Data.Referenced = true;
        }
    }

    BufferHandle RenderResourceManager::FindMeshBuffer(const uuids::uuid& uuid) const
    {
        // Render-thread only — m_uuid_to_buffer is written in FlushPendingUploads
        // which also runs on the render thread, so no mutex needed.
        for (uint32_t i = 0; i < m_uuid_to_buffer_count; ++i)
        {
            if (m_uuid_to_buffer[i].UUID == uuid)
            {
                return m_uuid_to_buffer[i].Handle;
            }
        }
        return {};
    }

    void RenderResourceManager::ReleaseMeshGeometry(const uuids::uuid& uuid)
    {
        // Capture registration identity, not mutable render-thread slot data. A reset
        // or re-upload between enqueue and drain must not release a newer generation.
        std::lock_guard map_lock(m_uuid_map_mutex);
        for (uint32_t i = 0; i < m_uuid_to_buffer_count; ++i)
        {
            UUIDBufferPair& pair = m_uuid_to_buffer[i];
            if (pair.UUID != uuid)
            {
                continue;
            }
            pair.ReleaseRequested = true;
            return;
        }
    }

    void RenderResourceManager::FlushPendingMeshReleases()
    {
        // No callbacks, asset access, or GPU waits while holding the map lock. All
        // release producers only mark map entries; pool/slot mutation stays here.
        std::lock_guard map_lock(m_uuid_map_mutex);
        uint32_t        i = 0;
        while (i < m_uuid_to_buffer_count)
        {
            const UUIDBufferPair& pair = m_uuid_to_buffer[i];
            if (!pair.ReleaseRequested)
            {
                ++i;
                continue;
            }
            const BufferHandle handle = pair.Handle;
            if (handle.IsValid() && !(handle.Generation & GBUF_GEN_TAG) && handle.Index < m_mesh_slot_count)
            {
                auto& slot = m_mesh_slots[handle.Index];
                if (slot.Generation == handle.Generation)
                {
                    RetireMeshGeometry(slot.Data);
                    slot = {};
                }
            }
            m_uuid_to_buffer[i]                      = m_uuid_to_buffer[--m_uuid_to_buffer_count];
            m_uuid_to_buffer[m_uuid_to_buffer_count] = {};
        }
    }

    void RenderResourceManager::Release(BufferHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        if (handle.Generation & GBUF_GEN_TAG)
        {
            // Generic device-local buffer — deferred-free the VmaAllocation.
            if (handle.Index >= m_gbuf_slot_count)
            {
                return;
            }
            auto& slot = m_gbuf_slots[handle.Index];
            if (slot.Generation != handle.Generation)
            {
                return;
            }
            DeferredFreeEntry e;
            e.EntryKind     = DeferredFreeEntry::Kind::Buffer;
            e.TimelineValue = m_device->SwapchainPtr->RenderTimelineNextValue;
            e.Data.Buffer   = slot.Data;
            m_device->DeferFree(e);
            slot.Data       = {};
            slot.Generation = 0;
        }
        else
        {
            // Render-thread only. Invalidate the registration while any pinned old
            // content keeps its pool allocation until the final consumer retires.
            if (handle.Index >= m_mesh_slot_count)
            {
                return;
            }
            auto& slot = m_mesh_slots[handle.Index];
            if (slot.Generation != handle.Generation)
            {
                return;
            }
            RetireMeshGeometry(slot.Data);
            slot = {};
            std::lock_guard lock(m_uuid_map_mutex);
            uint32_t        i = 0;
            while (i < m_uuid_to_buffer_count)
            {
                if (m_uuid_to_buffer[i].Handle.Index == handle.Index && m_uuid_to_buffer[i].Handle.Generation == handle.Generation)
                {
                    m_uuid_to_buffer[i]                      = m_uuid_to_buffer[--m_uuid_to_buffer_count];
                    m_uuid_to_buffer[m_uuid_to_buffer_count] = {};
                }
                else
                {
                    ++i;
                }
            }
        }
    }

    const Rendering::Textures::Texture* RenderResourceManager::GetTexture(const Rendering::Textures::TextureHandle& handle) const
    {
        return m_device ? m_device->GlobalTextures.Access(handle) : nullptr;
    }

    void RenderResourceManager::EnqueueDeletion(DeferredFreeEntry entry)
    {
        m_device->DeferFree(entry);
    }

    void RenderResourceManager::EnqueueDeletion(VkShaderModule module)
    {
        if (module == VK_NULL_HANDLE)
        {
            return;
        }
        DeferredFreeEntry e;
        e.EntryKind      = DeferredFreeEntry::Kind::VkHandle;
        e.TimelineValue  = m_device->SwapchainPtr->RenderTimelineNextValue;
        e.Data.Vk.Handle = reinterpret_cast<void*>(module);
        e.Data.Vk.Type   = Rendering::DeviceResourceType::SHADERMODULE;
        e.Data.Vk.Extra  = nullptr;
        m_device->DeferFree(e);
    }

    void RenderResourceManager::EnqueueDeletion(VkPipeline pipeline)
    {
        if (pipeline == VK_NULL_HANDLE)
        {
            return;
        }
        DeferredFreeEntry e;
        e.EntryKind      = DeferredFreeEntry::Kind::VkHandle;
        e.TimelineValue  = m_device->SwapchainPtr->RenderTimelineNextValue;
        e.Data.Vk.Handle = reinterpret_cast<void*>(pipeline);
        e.Data.Vk.Type   = Rendering::DeviceResourceType::PIPELINE;
        e.Data.Vk.Extra  = nullptr;
        m_device->DeferFree(e);
    }

    uint32_t RenderResourceManager::AllocMeshSlot()
    {
        for (uint32_t i = 0; i < m_mesh_slot_count; ++i)
        {
            if (m_mesh_slots[i].Generation == 0 && m_mesh_slot_gen_counter[i] < GBUF_GEN_TAG - 1)
            {
                m_mesh_slots[i].Generation = ++m_mesh_slot_gen_counter[i];
                return i;
            }
        }
        // Mesh generations must neither wrap nor enter the generic-buffer tag.
        while (m_mesh_slot_count < MAX_BUFFERS && m_mesh_slot_gen_counter[m_mesh_slot_count] >= GBUF_GEN_TAG - 1)
        {
            ++m_mesh_slot_count;
        }
        if (m_mesh_slot_count == MAX_BUFFERS)
        {
            return UINT32_MAX;
        }
        uint32_t idx                 = m_mesh_slot_count++;
        m_mesh_slots[idx].Generation = ++m_mesh_slot_gen_counter[idx];
        return idx;
    }

    // Masks the monotonic counter to 31 bits before OR-ing in GBUF_GEN_TAG (bit 31) so the
    // counter can never collide with the tag, and skips 0 on the (practically unreachable)
    // wraparound since Generation == 0 is the universal free/invalid sentinel.
    uint32_t RenderResourceManager::NextGBufGeneration(uint32_t& counter)
    {
        uint32_t gen = (++counter) & 0x7FFF'FFFFu;
        if (gen == 0)
        {
            gen = (++counter) & 0x7FFF'FFFFu;
        }
        return gen | GBUF_GEN_TAG;
    }

    uint32_t RenderResourceManager::AllocGBufSlot()
    {
        for (uint32_t i = 0; i < m_gbuf_slot_count; ++i)
        {
            if (m_gbuf_slots[i].Generation == 0)
            {
                m_gbuf_slots[i].Generation = NextGBufGeneration(m_gbuf_slot_gen_counter[i]);
                return i;
            }
        }
        ZENGINE_VALIDATE_ASSERT(m_gbuf_slot_count < MAX_GENERIC_BUFS, "RRM: MAX_GENERIC_BUFS exceeded")
        uint32_t idx                 = m_gbuf_slot_count++;
        m_gbuf_slots[idx].Generation = NextGBufGeneration(m_gbuf_slot_gen_counter[idx]);
        return idx;
    }

    BufferHandle RenderResourceManager::UploadBuffer(const void* data, size_t byte_size, VkBufferUsageFlags usage, const char* debug_name)
    {
        if (!data || byte_size == 0)
        {
            return {};
        }

        Core::Memory::BufferView buf = m_device->GpuMem.AllocateBuffer(static_cast<VkDeviceSize>(byte_size), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, Core::Memory::GpuMemoryDomain::DeviceGeometry, debug_name ? debug_name : "RRM::UploadBuffer");

        if (!buf)
        {
            return {};
        }

        EnsureBatchOpen(m_active_frame_index);

        BufferView staging = m_device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging);
        ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, staging.Allocation, 0, byte_size) == VK_SUCCESS, "RRM::UploadBuffer: staging copy failed")

        StagingCopyCtx ctx{staging.Handle, buf.Handle, 0, byte_size};
        RecordStagingCopy(m_batch_cmd->GetHandle(), &ctx);

        BatchFrameState& frame = m_batch_frames[m_batch_frame_index];
        ZENGINE_VALIDATE_ASSERT(frame.StagingCount < MAX_PENDING * 2, "RRM::UploadBuffer: batch staging overflow")
        frame.StagingBuffers[frame.StagingCount++] = staging;

        uint32_t slot_idx                          = AllocGBufSlot();
        m_gbuf_slots[slot_idx].Data                = buf;
        return {slot_idx, m_gbuf_slots[slot_idx].Generation};
    }

    const Core::Memory::BufferView* RenderResourceManager::GetBuffer(BufferHandle handle) const
    {
        if (!handle.IsValid() || !(handle.Generation & GBUF_GEN_TAG))
        {
            return nullptr;
        }
        if (handle.Index >= m_gbuf_slot_count)
        {
            return nullptr;
        }
        const auto& slot = m_gbuf_slots[handle.Index];
        return slot.Generation == handle.Generation ? &slot.Data : nullptr;
    }

    void RenderResourceManager::InitTextureTimelines()
    {
        uint32_t total_pool_count = m_device->CommandBufferMgr->TotalPoolCount;
        m_tex_total_cmd_count     = m_device->CommandBufferMgr->MaxBufferPerPool * m_device->CommandBufferMgr->MaxBufferPerPool;

        m_tex_timelines.init(m_device->Arena, total_pool_count, total_pool_count);
        m_tex_next_values.init(m_device->Arena, total_pool_count, total_pool_count);
        m_tex_retire_values.init(m_device->Arena, total_pool_count, total_pool_count);
        m_tex_retire_staging.init(m_device->Arena, total_pool_count, total_pool_count);
        m_tex_deferral_retry.init(m_device->Arena, MAX_TEXTURE_DEFERRALS);

        for (uint32_t i = 0; i < total_pool_count; ++i)
        {
            m_tex_timelines[i] = ZPushStructCtorArgs<Rendering::Primitives::Semaphore>(m_device->Arena, m_device, true);
            m_tex_retire_values[i].init(m_device->Arena, m_tex_total_cmd_count, m_tex_total_cmd_count);
            m_tex_retire_staging[i].init(m_device->Arena, m_tex_total_cmd_count, m_tex_total_cmd_count);
            m_tex_next_values[i].store(1, std::memory_order_release);
        }

        if (m_device->HasSeparateTransferQueue)
        {
            m_tex_transfer_timelines.init(m_device->Arena, total_pool_count, total_pool_count);
            m_tex_transfer_next_values.init(m_device->Arena, total_pool_count, total_pool_count);
            m_tex_transfer_retire.init(m_device->Arena, total_pool_count, total_pool_count);
            m_tex_transfer_staging.init(m_device->Arena, total_pool_count, total_pool_count);

            for (uint32_t i = 0; i < total_pool_count; ++i)
            {
                m_tex_transfer_timelines[i] = ZPushStructCtorArgs<Rendering::Primitives::Semaphore>(m_device->Arena, m_device, true);
                m_tex_transfer_retire[i].init(m_device->Arena, m_tex_total_cmd_count, m_tex_total_cmd_count);
                m_tex_transfer_staging[i].init(m_device->Arena, m_tex_total_cmd_count, m_tex_total_cmd_count);
                m_tex_transfer_next_values[i].store(1, std::memory_order_release);
            }
        }
        m_streaming_upload_tickets.init(m_device->Arena, MAX_TEXTURE_DEFERRALS);
    }

    void RenderResourceManager::InitUploadSlabs()
    {
        m_active_texture_decode_slabs.value.store(0, std::memory_order_relaxed);
        for (uint32_t i = 0; i < MAX_CONCURRENT_TEXTURE_DECODES; ++i)
        {
            m_upload_slabs[i].Init(m_upload_arena, UPLOAD_SLAB_BYTES);
        }
    }

    bool RenderResourceManager::TryAcquireTextureDecodeSlab(uint8_t* out_index)
    {
        ZENGINE_VALIDATE_ASSERT(out_index != nullptr, "RenderResourceManager::TryAcquireTextureDecodeSlab: output must not be null")
        uint32_t occupied = m_active_texture_decode_slabs.value.load(std::memory_order_acquire);
        for (;;)
        {
            for (uint32_t index = 0; index < MAX_CONCURRENT_TEXTURE_DECODES; ++index)
            {
                const uint32_t bit = 1u << index;
                if ((occupied & bit) != 0)
                {
                    continue;
                }

                const uint32_t desired = occupied | bit;
                if (m_active_texture_decode_slabs.value.compare_exchange_weak(occupied, desired, std::memory_order_acq_rel, std::memory_order_acquire))
                {
                    *out_index = static_cast<uint8_t>(index);
                    return true;
                }
                break;
            }
            if (occupied == (1u << MAX_CONCURRENT_TEXTURE_DECODES) - 1u)
            {
                return false;
            }
        }
    }

    void RenderResourceManager::ReleaseTextureDecodeSlab(uint8_t index)
    {
        ZENGINE_VALIDATE_ASSERT(index < MAX_CONCURRENT_TEXTURE_DECODES, "RenderResourceManager::ReleaseTextureDecodeSlab: invalid slab index")
        const uint32_t bit      = 1u << index;
        uint32_t       occupied = m_active_texture_decode_slabs.value.load(std::memory_order_acquire);
        for (;;)
        {
            ZENGINE_VALIDATE_ASSERT((occupied & bit) != 0, "RenderResourceManager::ReleaseTextureDecodeSlab: slab was not leased")
            if (m_active_texture_decode_slabs.value.compare_exchange_weak(occupied, occupied & ~bit, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                return;
            }
        }
    }

    void RenderResourceManager::ShutdownTextureTimelines()
    {
        uint32_t total_pool_count = m_device->CommandBufferMgr->TotalPoolCount;
        for (uint32_t p = 0; p < total_pool_count; ++p)
        {
            for (uint32_t i = 0; i < m_tex_total_cmd_count; ++i)
            {
                auto& sb = m_tex_retire_staging[p][i];
                if (sb.Handle != VK_NULL_HANDLE)
                {
                    m_device->GpuMem.FreeBuffer(sb);
                }

                if (m_device->HasSeparateTransferQueue)
                {
                    auto& tsb = m_tex_transfer_staging[p][i];
                    if (tsb.Handle != VK_NULL_HANDLE)
                    {
                        m_device->GpuMem.FreeBuffer(tsb);
                    }
                }
            }

            // Explicitly destroy arena-allocated Semaphore objects — VkSemaphore handles
            // are never freed by the arena page release.
            if (p < m_tex_timelines.size() && m_tex_timelines[p])
            {
                m_tex_timelines[p]->~Semaphore();
            }

            if (m_device->HasSeparateTransferQueue && p < m_tex_transfer_timelines.size() && m_tex_transfer_timelines[p])
            {
                m_tex_transfer_timelines[p]->~Semaphore();
            }
        }
    }

    Rendering::Textures::TextureHandle RenderResourceManager::UploadTextureBuffer(uint8_t frame_index, uint8_t thread_index, const Rendering::Textures::TextureHandle& handle, unsigned char* data, size_t data_size)
    {
        using namespace Rendering::Specifications;
        using namespace Rendering::Primitives;

        if (!handle.Valid() || !data || !TextureProducerComplete(handle) || !CanWriteTextureVersion(handle))
        {
            return {};
        }
        // An unconsumed release still requires the graph's acquire. Completion
        // alone is not ownership, even when no scene version currently pins it.
        if (FindStreamingUploadTicket(handle))
        {
            return {};
        }

        uint32_t pool_index = (frame_index * m_device->CommandBufferMgr->TotalThreadCount) + thread_index;

        auto     texture    = m_device->GlobalTextures.Access(handle);
        if (!texture)
        {
            return {};
        }
        const VkDeviceSize upload_size = data_size == 0 ? texture->BufferSize : static_cast<VkDeviceSize>(data_size);
        if (upload_size == 0 || upload_size > texture->BufferSize)
        {
            return {};
        }
        auto img_buf = m_device->ImageBufferManager.Access(texture->BufferHandle);
        if (!img_buf)
        {
            return {};
        }
        auto img_buf_aspect = (texture->Specification.Format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        auto buffer_handle  = img_buf->GetHandle();

        if (TextureUploadQueue(handle, img_buf->Layout) == QueueType::TRANSFER_QUEUE)
        {
            auto&    transfer_retire = m_tex_transfer_retire[pool_index];
            uint32_t i               = 0;
            for (; i < GEOMETRY_UPLOAD_SLOT; ++i)
            {
                if (transfer_retire[i] == 0)
                {
                    break;
                }
            }
            if (i >= GEOMETRY_UPLOAD_SLOT)
            {
                ZENGINE_CORE_WARN("[RRM] UploadTextureBuffer: no free transfer slot — upload deferred")
                return {};
            }
            if (!BeginTextureUploadRecording(handle, false))
            {
                return {};
            }

            auto                            transfer_cmd = m_device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, frame_index, thread_index, i);

            ImageMemoryBarrierSpecification to_transfer  = {};
            to_transfer.ImageHandle                      = buffer_handle;
            to_transfer.OldLayout                        = img_buf->Layout;
            to_transfer.NewLayout                        = ImageLayout::TRANSFER_DST_OPTIMAL;
            to_transfer.ImageAspectMask                  = VkImageAspectFlagBits(img_buf_aspect);
            to_transfer.SourceAccessMask                 = VK_ACCESS_NONE;
            to_transfer.DestinationAccessMask            = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_transfer.SourceStageMask                  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            to_transfer.DestinationStageMask             = VK_PIPELINE_STAGE_TRANSFER_BIT;
            to_transfer.LayerCount                       = texture->Specification.LayerCount;
            to_transfer.MipLevelCount                    = texture->Specification.MipLevelCount;
            to_transfer.SourceQueueFamily                = m_device->TransferFamilyIndex;
            to_transfer.DestinationQueueFamily           = m_device->TransferFamilyIndex;
            transfer_cmd->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
            img_buf->Layout                                  = to_transfer.NewLayout;

            // A streamed upload is submitted independently from the render timeline.
            // Keep its staging allocation off the render-timeline ring and retire it
            // with the producer timeline below.
            BufferView                      transfer_staging = m_device->WriteTextureDataAfterAdmission(transfer_cmd, handle, data, nullptr, false, upload_size);

            ImageMemoryBarrierSpecification release          = {};
            release.ImageHandle                              = buffer_handle;
            release.OldLayout                                = ImageLayout::TRANSFER_DST_OPTIMAL;
            release.NewLayout                                = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            release.ImageAspectMask                          = VkImageAspectFlagBits(img_buf_aspect);
            release.SourceAccessMask                         = VK_ACCESS_TRANSFER_WRITE_BIT;
            release.DestinationAccessMask                    = VK_ACCESS_NONE;
            release.SourceStageMask                          = VK_PIPELINE_STAGE_TRANSFER_BIT;
            // This is the producer half of a queue-family transfer, recorded
            // on a transfer-only queue. The render graph records the matching
            // graphics-side acquire with the actual consumer stage.
            release.DestinationStageMask                     = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            release.LayerCount                               = texture->Specification.LayerCount;
            release.MipLevelCount                            = texture->Specification.MipLevelCount;
            const uint32_t producer_family                   = m_device->GetQueue(QueueType::TRANSFER_QUEUE).FamilyIndex;
            const bool     transfers_ownership               = producer_family != m_device->GraphicFamilyIndex;
            release.SourceQueueFamily                        = transfers_ownership ? producer_family : VK_QUEUE_FAMILY_IGNORED;
            release.DestinationQueueFamily                   = transfers_ownership ? m_device->GraphicFamilyIndex : VK_QUEUE_FAMILY_IGNORED;
            transfer_cmd->TransitionImageLayout(ImageMemoryBarrier{release});
            img_buf->Layout = release.NewLayout;
            transfer_cmd->End();

            uint64_t transfer_val = m_tex_transfer_next_values[pool_index].fetch_add(1, std::memory_order_acq_rel);
            transfer_retire[i]    = transfer_val;
            if (transfer_staging)
            {
                m_tex_transfer_staging[pool_index][i] = transfer_staging;
            }

            m_async_uploads.Enqueue({
                .Buffer            = transfer_cmd,
                .Timeline          = m_tex_transfer_timelines[pool_index],
                .WaitTimeline      = nullptr,
                .WaitFlag          = VK_PIPELINE_STAGE_2_NONE,
                .SignalValue       = transfer_val,
                .WaitValue         = UINT64_MAX,
                .ExposeToFrameWait = false,
                .StreamingTicket   = {.Texture = handle, .CompletionTimeline = m_tex_transfer_timelines[pool_index], .CompletionValue = transfer_val, .PreReleaseLayout = Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(release.OldLayout)], .PostReleaseLayout = Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(release.NewLayout)], .ProducerQueueFamily = producer_family},
                .OnSubmitted       = &RenderResourceManager::OnStreamingUploadSubmitted,
                .OnRejected        = &RenderResourceManager::OnStreamingUploadRejected,
                .SubmissionContext = this,
            });
        }
        else
        {
            auto&    retire_values = m_tex_retire_values[pool_index];
            uint32_t i             = 0;
            for (; i < GEOMETRY_UPLOAD_SLOT; ++i)
            {
                if (retire_values[i] == 0)
                {
                    break;
                }
            }
            if (i >= GEOMETRY_UPLOAD_SLOT)
            {
                ZENGINE_CORE_WARN("[RRM] UploadTextureBuffer: no free graphics slot — upload deferred")
                return {};
            }
            if (!BeginTextureUploadRecording(handle, true))
            {
                return {};
            }

            auto                            cmd         = m_device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);

            ImageMemoryBarrierSpecification to_transfer = {};
            to_transfer.ImageHandle                     = buffer_handle;
            to_transfer.OldLayout                       = img_buf->Layout;
            to_transfer.NewLayout                       = ImageLayout::TRANSFER_DST_OPTIMAL;
            to_transfer.ImageAspectMask                 = VkImageAspectFlagBits(img_buf_aspect);
            // Reuploads stay on graphics. Order the overwrite after prior
            // graphics reads/writes, including legacy consumers without pins.
            to_transfer.SourceAccessMask                = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            to_transfer.DestinationAccessMask           = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_transfer.SourceStageMask                 = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            to_transfer.DestinationStageMask            = VK_PIPELINE_STAGE_TRANSFER_BIT;
            to_transfer.LayerCount                      = texture->Specification.LayerCount;
            to_transfer.MipLevelCount                   = texture->Specification.MipLevelCount;
            to_transfer.SourceQueueFamily               = m_device->GraphicFamilyIndex;
            to_transfer.DestinationQueueFamily          = m_device->GraphicFamilyIndex;
            cmd->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
            img_buf->Layout                          = to_transfer.NewLayout;

            // See the dedicated-transfer branch above: the upload submission is not
            // represented by RenderTimeline, so its staging buffer must not use the ring.
            BufferView                      staging  = m_device->WriteTextureDataAfterAdmission(cmd, handle, data, nullptr, false, upload_size);

            ImageMemoryBarrierSpecification to_final = {};
            to_final.ImageHandle                     = buffer_handle;
            to_final.OldLayout                       = ImageLayout::TRANSFER_DST_OPTIMAL;
            to_final.NewLayout                       = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            to_final.ImageAspectMask                 = VkImageAspectFlagBits(img_buf_aspect);
            to_final.SourceAccessMask                = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_final.DestinationAccessMask           = VK_ACCESS_SHADER_READ_BIT;
            to_final.SourceStageMask                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
            to_final.DestinationStageMask            = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            to_final.LayerCount                      = texture->Specification.LayerCount;
            to_final.MipLevelCount                   = texture->Specification.MipLevelCount;
            to_final.SourceQueueFamily               = m_device->GraphicFamilyIndex;
            to_final.DestinationQueueFamily          = m_device->GraphicFamilyIndex;
            cmd->TransitionImageLayout(ImageMemoryBarrier{to_final});
            cmd->End();

            uint64_t signal_value = m_tex_next_values[pool_index].fetch_add(1, std::memory_order_acq_rel);
            retire_values[i]      = signal_value;
            if (staging)
            {
                m_tex_retire_staging[pool_index][i] = staging;
            }
            img_buf->Layout = to_final.NewLayout;
            m_async_uploads.Enqueue({
                .Buffer            = cmd,
                .Timeline          = m_tex_timelines[pool_index],
                .WaitTimeline      = nullptr,
                .WaitFlag          = VK_PIPELINE_STAGE_2_NONE,
                .SignalValue       = signal_value,
                .WaitValue         = UINT64_MAX,
                .ExposeToFrameWait = false,
                .StreamingTicket   = {.Texture = handle, .CompletionTimeline = m_tex_timelines[pool_index], .CompletionValue = signal_value, .PreReleaseLayout = Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(to_final.OldLayout)], .PostReleaseLayout = Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(to_final.NewLayout)], .ProducerQueueFamily = m_device->GraphicFamilyIndex},
                .OnSubmitted       = &RenderResourceManager::OnStreamingUploadSubmitted,
                .OnRejected        = &RenderResourceManager::OnStreamingUploadRejected,
                .SubmissionContext = this,
            });
        }
        return handle;
    }

    Rendering::Textures::TextureHandle RenderResourceManager::UploadFontAtlas(unsigned char* pixels, uint32_t width, uint32_t height)
    {
        using namespace Rendering::Specifications;
        using namespace Rendering::Primitives;

        if (!pixels || width == 0 || height == 0)
        {
            return {};
        }

        TextureSpecification spec                     = {};
        spec.Width                                    = width;
        spec.Height                                   = height;
        spec.Format                                   = ImageFormat::R8G8B8A8_UNORM;

        auto                            handle        = m_device->CreateTexture(spec);
        auto                            texture       = m_device->GlobalTextures.Access(handle);
        auto                            img_buf       = m_device->ImageBufferManager.Access(texture->BufferHandle);
        auto                            buffer_handle = img_buf->GetHandle();

        ImageMemoryBarrierSpecification to_transfer   = {};
        to_transfer.ImageHandle                       = buffer_handle;
        to_transfer.OldLayout                         = img_buf->Layout;
        to_transfer.NewLayout                         = ImageLayout::TRANSFER_DST_OPTIMAL;
        to_transfer.ImageAspectMask                   = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.SourceAccessMask                  = VK_ACCESS_NONE;
        to_transfer.DestinationAccessMask             = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.SourceStageMask                   = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        to_transfer.DestinationStageMask              = VK_PIPELINE_STAGE_TRANSFER_BIT;
        to_transfer.LayerCount                        = 1;
        to_transfer.SourceQueueFamily                 = m_device->GraphicFamilyIndex;
        to_transfer.DestinationQueueFamily            = m_device->GraphicFamilyIndex;

        ImageMemoryBarrierSpecification to_final      = {};
        to_final.ImageHandle                          = buffer_handle;
        to_final.OldLayout                            = ImageLayout::TRANSFER_DST_OPTIMAL;
        to_final.NewLayout                            = ImageLayout::SHADER_READ_ONLY_OPTIMAL;
        to_final.ImageAspectMask                      = VK_IMAGE_ASPECT_COLOR_BIT;
        to_final.SourceAccessMask                     = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_final.DestinationAccessMask                = VK_ACCESS_SHADER_READ_BIT;
        to_final.SourceStageMask                      = VK_PIPELINE_STAGE_TRANSFER_BIT;
        to_final.DestinationStageMask                 = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        to_final.LayerCount                           = 1;
        to_final.SourceQueueFamily                    = m_device->GraphicFamilyIndex;
        to_final.DestinationQueueFamily               = m_device->GraphicFamilyIndex;

        auto*                         cmd             = m_upload_cmd_mgr->GetCommandBuffer(QueueType::GRAPHIC_QUEUE, m_active_frame_index, 0, 0, false);
        Rendering::Primitives::Fence* fence           = m_sync_upload_fence;
        fence->Wait(UINT64_MAX);
        fence->Reset();
        cmd->ResetState();
        vkResetCommandBuffer(cmd->GetHandle(), 0);
        cmd->Begin();
        cmd->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
        img_buf->Layout        = to_transfer.NewLayout;
        uint32_t   ring_offset = 0;
        BufferView staging     = m_device->WriteTextureData(cmd, handle, pixels, &ring_offset);
        cmd->TransitionImageLayout(ImageMemoryBarrier{to_final});
        img_buf->Layout = to_final.NewLayout;
        cmd->End();

        VkQueue         gfx_queue = m_device->GetQueue(QueueType::GRAPHIC_QUEUE).Handle;
        VkCommandBuffer raw       = cmd->GetHandle();
        VkSubmitInfo    submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &raw;

        vkQueueSubmit(gfx_queue, 1, &submit, fence->GetHandle());
        fence->Wait(UINT64_MAX);
        cmd->ResetState();

        // Gated on the render timeline's own progress rather than marked safe at value 0 —
        // the fence wait above is not trustworthy proof of completion once any command
        // buffer on the queue has already timed out (issue #764 follow-up investigation).
        if (ring_offset != std::numeric_limits<uint32_t>::max())
        {
            m_device->GpuMem.Ring.Submit(ring_offset, static_cast<uint32_t>(texture->BufferSize), m_device->SwapchainPtr->RenderTimelineNextValue);
        }

        if (staging)
        {
            DeferredFreeEntry e;
            e.EntryKind   = DeferredFreeEntry::Kind::Buffer;
            e.Data.Buffer = staging;
            m_device->DeferFree(e);
        }

        return handle;
    }

    bool RenderResourceManager::EnqueueTextureDeferral(const TextureDeferral& deferral)
    {
        return m_tex_deferral_queue.push(deferral);
    }

    bool RenderResourceManager::TextureReleaseRequested(Textures::TextureHandle handle)
    {
        // Workers may inspect only these synchronized identities, never native
        // texture/image tables. Their specification and pixel storage are local.
        std::scoped_lock lock(m_pending_texture_release_mutex, m_texture_version_mutex);
        if (handle.Valid() && handle.Index < TextureVersionCapacity)
        {
            const auto& record = m_texture_versions[handle.Index];
            if (record.Snapshot.Handle.Valid() && (record.Snapshot.Handle.Generation > handle.Generation || (record.Snapshot.Handle.Generation == handle.Generation && record.DestroyRequested)))
            {
                return true;
            }
        }
        for (uint32_t index = 0; index < m_pending_texture_release_count; ++index)
        {
            const auto pending = m_pending_texture_releases[index];
            if (pending.Index == handle.Index && pending.Generation == handle.Generation)
            {
                return true;
            }
        }
        return false;
    }

    void RenderResourceManager::ReleaseTextureDeferral(TextureDeferral& deferral)
    {
        if (deferral.Slab && deferral.Pixels)
        {
            deferral.Slab->Free(deferral.Pixels);
        }
        if (deferral.DecodeSlabIndex != UINT8_MAX)
        {
            ReleaseTextureDecodeSlab(deferral.DecodeSlabIndex);
        }
        deferral = {};
    }

    bool RenderResourceManager::ProcessTextureDeferral(uint8_t frame_index, TextureDeferral& deferral)
    {
        const auto  handle   = deferral.TexHandle;
        const auto* texture  = m_device ? m_device->GlobalTextures.Access(handle) : nullptr;
        const bool  terminal = !texture || !deferral.Pixels || handle.Index >= TextureVersionCapacity || !texture->BufferSize || !m_device->ImageBufferManager.Access(texture->BufferHandle) || (deferral.ByteSize && deferral.ByteSize > texture->BufferSize) || TextureReleaseRequested(handle) || GetTextureDecodeState(handle) == TextureDecodeState::Failed;
        if (terminal)
        {
            // A late decode is allowed to finish, not to revive a destroyed
            // generation or retain its pixels forever in the retry array.
            PublishTextureDecodeCompletion(handle, false);
            ReleaseTextureDeferral(deferral);
            return true;
        }
        if (!UploadTextureBuffer(frame_index, 0, handle, deferral.Pixels, deferral.ByteSize).Valid())
        {
            return false; // Pins, unconsumed acquires or command capacity may clear.
        }
        ReleaseTextureDeferral(deferral);
        return true;
    }

    void RenderResourceManager::DiscardTextureDeferrals()
    {
        TextureDeferral deferral = {};
        while (m_tex_deferral_queue.pop(deferral))
        {
            PublishTextureDecodeCompletion(deferral.TexHandle, false);
            ReleaseTextureDeferral(deferral);
        }

        for (TextureDeferral& retry : m_tex_deferral_retry)
        {
            PublishTextureDecodeCompletion(retry.TexHandle, false);
            ReleaseTextureDeferral(retry);
        }
        m_tex_deferral_retry.clear();
    }

    void RenderResourceManager::CompleteDeferrals(uint8_t frame_index)
    {
        // Retain deferrals that could not claim an upload slot. Keeping them in this
        // render-thread-owned array avoids requeueing into a concurrently produced MPSC
        // queue, and preserves the pixel allocation until a later frame can upload it.
        for (size_t i = 0; i < m_tex_deferral_retry.size();)
        {
            if (!ProcessTextureDeferral(frame_index, m_tex_deferral_retry[i]))
            {
                ++i;
                continue;
            }
            m_tex_deferral_retry.erase(i);
        }

        TextureDeferral deferral = {};
        while (m_tex_deferral_queue.pop(deferral))
        {
            if (!ProcessTextureDeferral(frame_index, deferral))
            {
                m_tex_deferral_retry.push(deferral);
            }
        }

        DispatchQueuedTextureDecodes();
    }

    void RenderResourceManager::SubmitAsyncUploads()
    {
        m_async_uploads.SubmitAll();
    }

    const Core::Containers::Array<Hardwares::StreamingUploadTicket>& RenderResourceManager::GetStreamingUploadTickets() const
    {
        return m_streaming_upload_tickets;
    }

    const Hardwares::StreamingUploadTicket* RenderResourceManager::FindStreamingUploadTicket(const Rendering::Textures::TextureHandle& handle) const
    {
        for (const auto& ticket : m_streaming_upload_tickets)
        {
            if (ticket.Texture.Index == handle.Index && ticket.Texture.Generation == handle.Generation)
            {
                return &ticket;
            }
        }
        return nullptr;
    }

    void RenderResourceManager::PublishStreamingUploadTicket(const Hardwares::StreamingUploadTicket& ticket)
    {
        if (!ticket.Texture.Valid() || !ticket.CompletionTimeline || ticket.CompletionValue == 0)
        {
            return;
        }

        if (ticket.Texture.Index < TextureVersionCapacity)
        {
            std::lock_guard lock(m_texture_version_mutex);
            auto&           producer = m_texture_producer_completions[ticket.Texture.Index];
            if (producer.Texture.Valid() && producer.Texture.Generation > ticket.Texture.Generation)
            {
                return; // A late old-generation notification cannot replace the live proof.
            }
            const bool graphics_only = (producer.Texture.Index == ticket.Texture.Index && producer.Texture.Generation == ticket.Texture.Generation && producer.GraphicsUploadOnly) || ticket.ProducerQueueFamily == m_device->GraphicFamilyIndex;
            producer                 = {ticket.Texture, ticket.CompletionTimeline, ticket.CompletionValue, false, true, graphics_only};
        }

        for (auto& replacement : m_texture_replacements)
        {
            if (replacement.Replacement.Index == ticket.Texture.Index && replacement.Replacement.Generation == ticket.Texture.Generation)
            {
                replacement.Producer = ticket;
                break;
            }
        }

        for (auto& pending : m_streaming_upload_tickets)
        {
            if (pending.Texture.Index == ticket.Texture.Index && pending.Texture.Generation == ticket.Texture.Generation)
            {
                pending = ticket;
                return;
            }
        }
        m_streaming_upload_tickets.push(ticket);
    }

    void RenderResourceManager::OnStreamingUploadSubmitted(void* context, const Hardwares::StreamingUploadTicket& ticket)
    {
        if (context)
        {
            auto* manager = static_cast<RenderResourceManager*>(context);
            manager->PublishStreamingUploadTicket(ticket);
            manager->MarkTextureUploadSubmitted(ticket.Texture);
        }
    }

    void RenderResourceManager::OnStreamingUploadRejected(void* context, const Hardwares::StreamingUploadTicket& ticket)
    {
        if (context)
        {
            static_cast<RenderResourceManager*>(context)->RejectTextureUpload(ticket);
        }
    }

    void RenderResourceManager::ResetTextureUploadSlot(Rendering::QueueType queue, uint32_t pool, uint32_t slot)
    {
        const bool transfer = queue == QueueType::TRANSFER_QUEUE;
        auto&      values   = transfer ? m_tex_transfer_retire : m_tex_retire_values;
        auto&      staging  = transfer ? m_tex_transfer_staging : m_tex_retire_staging;
        auto&      buffers  = transfer ? m_device->CommandBufferMgr->InstantTransferCommandBuffers : m_device->CommandBufferMgr->InstantGraphicsCommandBuffers;
        auto*      command  = buffers[pool * m_tex_total_cmd_count + slot];
        command->ResetState();
        if (!m_device->IsDeviceLost.load(std::memory_order_acquire))
        {
            const VkResult result = vkResetCommandBuffer(command->GetHandle(), 0);
            m_device->CheckDeviceLost(result, "Texture upload slot reset");
            ZENGINE_VALIDATE_ASSERT(result == VK_SUCCESS || m_device->IsDeviceLost.load(std::memory_order_acquire), "Failed to reset texture upload slot")
        }
        values[pool][slot] = 0;
        auto& buffer       = staging[pool][slot];
        if (buffer.Handle != VK_NULL_HANDLE)
        {
            m_device->GpuMem.FreeBuffer(buffer);
        }
    }

    void RenderResourceManager::RejectTextureUpload(const Hardwares::StreamingUploadTicket& ticket)
    {
        if (!ticket.Texture.Valid() || !ticket.CompletionTimeline || !ticket.CompletionValue)
        {
            return;
        }
        // An exact timeline/value identifies one reserved recording. Do not
        // reclaim accepted work, another pool's slot, or a duplicate rejection.
        for (const auto queue : {QueueType::GRAPHIC_QUEUE, QueueType::TRANSFER_QUEUE})
        {
            const bool transfer  = queue == QueueType::TRANSFER_QUEUE;
            auto&      timelines = transfer ? m_tex_transfer_timelines : m_tex_timelines;
            auto&      values    = transfer ? m_tex_transfer_retire : m_tex_retire_values;
            for (uint32_t pool = 0; pool < timelines.size(); ++pool)
            {
                if (timelines[pool] != ticket.CompletionTimeline)
                {
                    continue;
                }
                for (uint32_t slot = 0; slot < GEOMETRY_UPLOAD_SLOT; ++slot)
                {
                    if (values[pool][slot] != ticket.CompletionValue)
                    {
                        continue;
                    }
                    ResetTextureUploadSlot(queue, pool, slot);
                    // No submitted transition occurred. A later complete upload
                    // may discard these failed generation's contents explicitly.
                    auto* texture = m_device->GlobalTextures.Access(ticket.Texture);
                    auto* image   = texture ? m_device->ImageBufferManager.Access(texture->BufferHandle) : nullptr;
                    if (image)
                    {
                        image->Layout = Specifications::ImageLayout::UNDEFINED;
                    }
                    PublishTextureDecodeCompletion(ticket.Texture, false);
                    if (ticket.Texture.Index < TextureVersionCapacity)
                    {
                        std::lock_guard lock(m_texture_version_mutex);
                        auto&           producer = m_texture_producer_completions[ticket.Texture.Index];
                        if (producer.Texture.Index == ticket.Texture.Index && producer.Texture.Generation == ticket.Texture.Generation)
                        {
                            producer.PendingSubmission = false;
                        }
                    }
                    return;
                }
            }
        }
    }

    void RenderResourceManager::AcknowledgeStreamingUploadTicket(const Hardwares::StreamingUploadTicket& ticket)
    {
        for (uint32_t index = 0; index < m_streaming_upload_tickets.size(); ++index)
        {
            const auto& pending = m_streaming_upload_tickets[index];
            if (pending.Texture.Index != ticket.Texture.Index || pending.Texture.Generation != ticket.Texture.Generation || pending.CompletionTimeline != ticket.CompletionTimeline || pending.CompletionValue != ticket.CompletionValue)
            {
                continue;
            }
            if (ticket.Texture.Index < TextureVersionCapacity)
            {
                std::lock_guard lock(m_texture_version_mutex);
                auto&           producer = m_texture_producer_completions[ticket.Texture.Index];
                if (producer.Texture.Index == ticket.Texture.Index && producer.Texture.Generation == ticket.Texture.Generation)
                {
                    producer.AcquirePending     = false;
                    producer.GraphicsUploadOnly = true;
                }
            }
            m_streaming_upload_tickets.erase(index);
            return;
        }
    }

    void RenderResourceManager::RetireTextureSlots(uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index     = (frame_index * m_device->CommandBufferMgr->TotalThreadCount) + thread_index;

        uint64_t graphics_value = 0;
        vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_tex_timelines[pool_index]->GetHandle(), &graphics_value);

        auto& retire_values = m_tex_retire_values[pool_index];
        for (uint32_t i = 0; i < GEOMETRY_UPLOAD_SLOT; ++i)
        {
            auto retire_val = retire_values[i];
            if (retire_val != 0 && graphics_value >= retire_val)
            {
                ResetTextureUploadSlot(QueueType::GRAPHIC_QUEUE, pool_index, i);
            }
        }

        if (m_device->HasSeparateTransferQueue)
        {
            uint64_t transfer_value = 0;
            vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_tex_transfer_timelines[pool_index]->GetHandle(), &transfer_value);
            auto& transfer_retire = m_tex_transfer_retire[pool_index];
            for (uint32_t i = 0; i < GEOMETRY_UPLOAD_SLOT; ++i)
            {
                auto tv = transfer_retire[i];
                if (tv != 0 && transfer_value >= tv)
                {
                    ResetTextureUploadSlot(QueueType::TRANSFER_QUEUE, pool_index, i);
                }
            }
        }
    }

    void RenderResourceManager::ClearAsyncUploads()
    {
        CancelTextureReplacements();
        for (const auto& replacement : m_texture_replacements)
        {
            if (replacement.Replacement.Valid() && !replacement.Resolved)
            {
                ScheduleTextureReload(replacement.UUID);
            }
        }
        m_async_uploads.Clear();
        m_device->AsyncGPUOperations.clear();

        // Geometry batches are submitted immediately, unlike the cancellable
        // texture jobs above. Keep their semaphore dependency across recreation:
        // an OUT_OF_DATE acquire has no Present() submission to relay it to the
        // next frame's graph.
        if (m_batch_timeline && m_batch_accepted_value != 0)
        {
            m_device->EnqueueAsyncGPUOperation({VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_batch_accepted_value, m_batch_timeline});
        }
        // Accepted texture releases still require a matching graph acquire after
        // recreation. Cancelling unsubmitted jobs cannot cancel those releases.
    }

    void RenderResourceManager::ResetTextureTimelines()
    {
        auto total_thread_count = m_device->CommandBufferMgr->TotalThreadCount;
        auto frame_count        = m_device->SwapchainPtr->BufferredFrameCount;

        for (uint32_t f = 0; f < frame_count; ++f)
        {
            for (uint32_t t = 0; t < total_thread_count; ++t)
            {
                RetireTextureSlots(static_cast<uint8_t>(f), static_cast<uint8_t>(t));

                uint32_t pool_index = (f * total_thread_count) + t;
                uint64_t gv         = 0;
                vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_tex_timelines[pool_index]->GetHandle(), &gv);
                m_tex_next_values[pool_index].store(gv + 1, std::memory_order_release);

                if (m_device->HasSeparateTransferQueue)
                {
                    uint64_t tv = 0;
                    vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_tex_transfer_timelines[pool_index]->GetHandle(), &tv);
                    m_tex_transfer_next_values[pool_index].store(tv + 1, std::memory_order_release);
                }
            }
        }
        // Idle/completed producers do not implicitly transfer queue ownership.
        // Their semaphores remain alive, so retain unconsumed acquire tickets.
    }

    Rendering::Textures::TextureHandle RenderResourceManager::IngestTexture(const uuids::uuid& uuid, const char* absolute_path, Rendering::Textures::TextureHandle existing)
    {
        ZENGINE_LOG_RENDER_INFO("[RRM] {} texture {} from {}", existing.Valid() ? "Reloading" : "Ingesting", uuids::to_string(uuid), absolute_path)
        return SubmitTextureFile(absolute_path, existing);
    }

    bool RenderResourceManager::HasPendingTextureUploads()
    {
        std::scoped_lock lock(m_pending_mutex, m_pending_texture_reload_mutex);
        // Deferred registration deliberately publishes the fallback for one graph
        // frame before the real descriptor. Pixel readiness alone is insufficient:
        // do not bake that temporary magenta fallback into a persistent preview.
        return m_pending_texture_reload_count != 0 || m_texture_replacement_count.load(std::memory_order_acquire) != 0 || HasUnsubmittedTextureWork();
    }

    bool RenderResourceManager::HasUnsubmittedTextureWork() const
    {
        return (m_device && (!m_device->DeferredTextureDescriptorUpdates.empty() || !m_device->TextureHandleToUpdates.empty())) || !m_queued_texture_decodes.empty() || m_active_texture_decode_slabs.value.load(std::memory_order_acquire) != 0 || !m_tex_deferral_queue.empty() || !m_tex_deferral_retry.empty() || m_async_uploads.HasPendingJobs();
    }

    void RenderResourceManager::ScheduleTextureReload(const uuids::uuid& uuid)
    {
        std::lock_guard lock(m_pending_texture_reload_mutex);
        for (uint32_t i = 0; i < m_pending_texture_reload_count; ++i)
        {
            if (m_pending_texture_reloads[i] == uuid)
            {
                return; // already pending — dedupe
            }
        }
        if (m_pending_texture_reload_count >= MAX_PENDING)
        {
            ZENGINE_LOG_RENDER_WARN("[RRM] Pending texture reload queue full — dropping reload for {}", uuids::to_string(uuid))
            return;
        }
        m_pending_texture_reloads[m_pending_texture_reload_count++] = uuid;
    }

    void RenderResourceManager::FlushPendingTextureReloads()
    {
        uint32_t    count              = 0;
        uuids::uuid local[MAX_PENDING] = {};
        {
            std::lock_guard lock(m_pending_texture_reload_mutex);
            count = m_pending_texture_reload_count;
            secure_memcpy(local, sizeof(local), m_pending_texture_reloads, count * sizeof(local[0]));
            m_pending_texture_reload_count = 0;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            TextureReplacement* available = nullptr;
            bool                active    = false;
            for (auto& replacement : m_texture_replacements)
            {
                if (!replacement.Replacement.Valid())
                {
                    available = &replacement;
                }
                else if (replacement.UUID == local[i])
                {
                    replacement.Superseded = true;
                    active                 = true;
                }
            }
            if (active || !available)
            {
                ScheduleTextureReload(local[i]);
                continue;
            }
            Rendering::Textures::TextureHandle existing = AssetManager::FindTextureHandle(local[i]);
            if (!existing.Valid())
            {
                continue;
            }
            // IngestMutex guards the read: AssetManager::Textures (unlike Meshes/Materials)
            // is arena-backed, so a concurrent IngestTexture on the import thread can
            // reallocate its backing storage mid-read without this lock.
            char full_path[MAX_FILE_PATH_COUNT] = {};
            {
                std::lock_guard lock(AssetManager::Instance()->IngestMutex);
                AssetTexture*   tex = AssetManager::GetAsset<AssetTexture>(local[i]);
                if (!tex || tex->Path.empty())
                {
                    continue;
                }
                const int length = snprintf(full_path, sizeof(full_path), "%s%c%s", AssetManager::Instance()->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, tex->Path.c_str());
                if (length < 0 || static_cast<size_t>(length) >= sizeof(full_path))
                {
                    continue;
                }
            }
            const auto candidate = SubmitTextureFile(full_path, {}, true);
            if (candidate.Valid())
            {
                *available             = {};
                available->UUID        = local[i];
                available->Expected    = existing;
                available->Replacement = candidate;
                m_texture_replacement_count.fetch_add(1, std::memory_order_release);
            }
        }
        PollTextureReplacements();
    }

    void RenderResourceManager::CancelTextureReplacements()
    {
        for (auto& replacement : m_texture_replacements)
        {
            if (replacement.Replacement.Valid())
            {
                replacement.Superseded = true;
            }
        }
    }

    void RenderResourceManager::PollTextureReplacements()
    {
        for (auto& replacement : m_texture_replacements)
        {
            if (!replacement.Replacement.Valid())
            {
                continue;
            }
            if (!replacement.Resolved)
            {
                const auto state = GetTextureDecodeState(replacement.Replacement);
                if (state == TextureDecodeState::Pending)
                {
                    continue;
                }
                if (replacement.Producer.CompletionTimeline && !replacement.ProducerComplete)
                {
                    uint64_t   completed = 0;
                    const auto result    = vkGetSemaphoreCounterValue(m_device->LogicalDevice, replacement.Producer.CompletionTimeline->GetHandle(), &completed);
                    if (result != VK_SUCCESS || completed < replacement.Producer.CompletionValue)
                    {
                        continue;
                    }
                    replacement.ProducerComplete = true;
                }
                if (state != TextureDecodeState::Succeeded)
                {
                    replacement.Superseded = true;
                }
                if (!replacement.Superseded && (!replacement.ProducerComplete || HasUnsubmittedTextureWork()))
                {
                    continue;
                }

                // Match the import-side lock order. Exclude a new reload signal
                // between checking supersession and the checked cache switch.
                auto* assets = AssetManager::Instance();
                if (assets)
                {
                    std::lock_guard ingest_lock(assets->IngestMutex);
                    std::lock_guard reload_lock(m_pending_texture_reload_mutex);
                    for (uint32_t index = 0; index < m_pending_texture_reload_count; ++index)
                    {
                        if (m_pending_texture_reloads[index] == replacement.UUID)
                        {
                            replacement.Superseded = true;
                            break;
                        }
                    }
                    bool retire_previous = false;
                    if (!replacement.Superseded && AssetManager::TryReplaceTexture(replacement.UUID, replacement.Expected, replacement.Replacement, &retire_previous))
                    {
                        replacement.Retiring = retire_previous ? replacement.Expected : Textures::TextureHandle{};
                        ForgetTextureDecode(replacement.Replacement);
                    }
                    else
                    {
                        replacement.Retiring = replacement.Replacement;
                    }
                }
                else
                {
                    replacement.Retiring = replacement.Replacement;
                }
                replacement.Resolved = true;
            }
            if (replacement.Retiring.Valid() && !QueueTextureRelease(replacement.Retiring))
            {
                continue;
            }
            replacement = {};
            m_texture_replacement_count.fetch_sub(1, std::memory_order_release);
        }
    }

    void RenderResourceManager::ReleaseTexture(const uuids::uuid& uuid)
    {
        auto* assets = AssetManager::Instance();
        if (!assets)
        {
            return;
        }
        // Serialize capture with replacement publication; deletion must retire
        // the image whose mapping its deferred CPU patch will remove.
        std::lock_guard                    ingest_lock(assets->IngestMutex);
        // Captured before AssetManager::ReleaseTexture's deferred patch runs — that patch
        // erases the UUID→handle map entry, so the handle must be read now or it's lost.
        Rendering::Textures::TextureHandle handle = AssetManager::FindTextureHandle(uuid);

        AssetManager::ReleaseTexture(uuid);

        const auto fallback = assets->FallbackTextureHandle;
        if (!handle.Valid() || (handle.Index == fallback.Index && handle.Generation == fallback.Generation))
        {
            return;
        }

        if (!QueueTextureRelease(handle))
        {
            // UUID removal has no caller-owned retry record. Keep disposal in
            // the existing per-texture bitmap if the smaller queue is full.
            (void) RetainTextureForRetirement(handle, true);
        }
    }

    bool RenderResourceManager::QueueTextureRelease(const Rendering::Textures::TextureHandle& handle)
    {
        std::lock_guard lock(m_pending_texture_release_mutex);
        for (uint32_t index = 0; index < m_pending_texture_release_count; ++index)
        {
            if (m_pending_texture_releases[index].Index == handle.Index && m_pending_texture_releases[index].Generation == handle.Generation)
            {
                return true;
            }
        }
        if (m_pending_texture_release_count >= MAX_PENDING)
        {
            ZENGINE_LOG_RENDER_WARN("[RRM] Pending texture release queue full — release refused for texture handle (index {})", handle.Index)
            return false;
        }
        m_pending_texture_releases[m_pending_texture_release_count++] = handle;
        // Seal writes under the same version mutex used by upload admission.
        // A deferral's earlier cancellation check is not enough: a release may
        // race the gap before command recording claims its lifetime hold.
        RetainTextureForRetirement(handle);
        return true;
    }

    void RenderResourceManager::ReleaseTexture(const Rendering::Textures::TextureHandle& handle)
    {
        if (!handle.Valid() || !m_device)
        {
            return;
        }
        if (!QueueTextureRelease(handle))
        {
            // Fire-and-forget owners (previews/cancelled ingestion) cannot retry
            // after surrendering their handle. The fixed version ledger owns it.
            (void) RetainTextureForRetirement(handle, true);
        }
    }

    void RenderResourceManager::FlushPendingTextureReleases()
    {
        std::lock_guard lock(m_pending_texture_release_mutex);
        AdmitTextureVersionRetirements();
        for (uint32_t index = 0; index < m_pending_texture_release_count;)
        {
            const auto handle = m_pending_texture_releases[index];
            if (IsTextureVersionPinned(handle))
            {
                ++index;
                continue;
            }
            if (GetTextureDecodeState(handle) == TextureDecodeState::Pending)
            {
                ++index;
                continue;
            }
            if (!TextureProducerComplete(handle))
            {
                ++index;
                continue;
            }
            if (const auto* ticket = FindStreamingUploadTicket(handle))
            {
                // An obsolete preview may never be drawn/imported by the graph.
                // Its producer has finished; remove its otherwise orphaned ticket.
                AcknowledgeStreamingUploadTicket(*ticket);
            }

            ForgetTextureDecode(handle);
            m_device->DestroyTexture(handle);
            m_pending_texture_releases[index] = m_pending_texture_releases[--m_pending_texture_release_count];
        }
    }

    Rendering::Textures::TextureHandle RenderResourceManager::SubmitTextureFile(const char* filename, Rendering::Textures::TextureHandle existing, bool track_decode)
    {
        return SubmitTextureFileInternal(filename, existing, track_decode, 0);
    }

    void RenderResourceManager::CancelStoppedThumbnailCacheWrites()
    {
        // Engine teardown joins/destroys the pool before RRM. Its shutdown can
        // discard jobs that never started. No callback can run once Pool is null.
        if (Helpers::ThreadPoolHelper::IsInitialized())
        {
            return;
        }
        std::lock_guard lock(m_pending_mutex);
        for (auto& task : m_thumbnail_cache_writes)
        {
            if (task.WorkerQueued)
            {
                task.WorkerQueued = false;
                task.Reserved     = false;
                m_pending_texture_decodes.value.fetch_sub(1, std::memory_order_release);
            }
        }
        if (m_thumbnail_cache_cleanup.WorkerQueued)
        {
            m_thumbnail_cache_cleanup.WorkerQueued = false;
            m_thumbnail_maintenance_stats.Pending  = false;
            m_pending_texture_decodes.value.fetch_sub(1, std::memory_order_release);
        }
    }

    bool RenderResourceManager::SubmitThumbnailCacheCleanup(const char* project_root, uint32_t schema_revision, uint32_t renderer_revision)
    {
        std::lock_guard lock(m_pending_mutex);
        if (!Helpers::ThreadPoolHelper::IsInitialized() || !m_registry || !m_accept_texture_decodes.value.load(std::memory_order_acquire) || m_thumbnail_cache_cleanup.WorkerQueued || !project_root || !project_root[0] || Helpers::secure_strlen(project_root) >= MAX_FILE_PATH_COUNT || !schema_revision || !renderer_revision)
        {
            return false;
        }
        auto& task            = m_thumbnail_cache_cleanup;
        task.Owner            = this;
        task.SchemaRevision   = schema_revision;
        task.RendererRevision = renderer_revision;
        Helpers::secure_strcpy(task.ProjectRoot, sizeof(task.ProjectRoot), project_root);
        task.WorkerQueued                     = true;
        m_thumbnail_maintenance_stats.Pending = true;
        m_pending_texture_decodes.value.fetch_add(1, std::memory_order_release);
        if (Helpers::ThreadPoolHelper::Submit(&task, &RenderResourceManager::RunThumbnailCacheCleanup))
        {
            return true;
        }
        task.WorkerQueued                     = false;
        m_thumbnail_maintenance_stats.Pending = false;
        m_pending_texture_decodes.value.fetch_sub(1, std::memory_order_release);
        return false;
    }

    RenderResourceManager::ThumbnailMaintenanceStatistics RenderResourceManager::GetThumbnailMaintenanceStatistics()
    {
        std::lock_guard lock(m_pending_mutex);
        return m_thumbnail_maintenance_stats;
    }

    RenderResourceManager::CpuUploadMemoryStatistics RenderResourceManager::GetCpuUploadMemoryStatistics() const
    {
        CpuUploadMemoryStatistics result = {};
        const auto                add    = [&](const Core::Memory::TLSFSlab& slab) {
            const auto pool                 = slab.GetStatistics();
            result.BackingBytes            += pool.BackingBytes;
            result.LiveBytes               += pool.LiveBytes;
            result.LiveBlocks              += pool.LiveBlocks;
            result.SumOfPoolHighWaterBytes += pool.HighWaterLiveBytes;
        };
        for (const auto& slab : m_upload_slabs)
        {
            add(slab);
        }
        add(m_synchronous_texture_scratch);
        add(m_texture_task_slab);
        result.ActiveDecodeLeases = std::popcount(m_active_texture_decode_slabs.value.load(std::memory_order_acquire));
        result.PendingWorkerTasks = m_pending_texture_decodes.value.load(std::memory_order_acquire);
        return result;
    }

    void RenderResourceManager::RunThumbnailCacheCleanup(void* context)
    {
        ZENGINE_PROFILE_SCOPE("ThumbnailProfile.CacheCleanup");
        auto&                                 task    = *static_cast<ThumbnailCacheCleanupTask*>(context);
        auto*                                 manager = task.Owner;
        ThumbnailCacheMaintenance::Statistics stats{};
        try
        {
            const auto resolve = +[](void* context, const uuids::uuid& uuid, ThumbnailCacheMaintenance::CurrentKey* key) {
                auto& task = *static_cast<ThumbnailCacheCleanupTask*>(context);
                if (auto* engine = Engine::GetContext(); engine && engine->VFSScanner.IsScanning())
                {
                    return ThumbnailCacheMaintenance::Resolution::Unknown;
                }
                AssetRecordSnapshot record{};
                if (!task.Owner->m_registry->TryReadRecord(uuid, &record))
                {
                    return ThumbnailCacheMaintenance::Resolution::Missing;
                }
                if (record.Record.State != AssetState::Loaded && record.Record.State != AssetState::Registered)
                {
                    return ThumbnailCacheMaintenance::Resolution::Unknown;
                }
                key->Identity = {.AssetUUID = uuid, .SourceHash = record.Record.Meta.SourceHash, .SchemaRevision = task.SchemaRevision};
                if (record.Record.Type == AssetType::TEXTURE)
                {
                    return ThumbnailCacheMaintenance::Resolution::Current;
                }
                if (record.Record.Type != AssetType::MATERIAL && record.Record.Type != AssetType::MESH)
                {
                    return ThumbnailCacheMaintenance::Resolution::Missing;
                }
                uint64_t runtime = 0;
                if (!Renderers::AssetPreviewPass::DependencyFingerprint(record, &runtime, &key->Identity.DependencyHash, &key->FullyTransparent))
                {
                    return ThumbnailCacheMaintenance::Resolution::SourceOnly;
                }
                key->Identity.RendererRevision = task.RendererRevision;
                return ThumbnailCacheMaintenance::Resolution::Current;
            };
            stats = ThumbnailCacheMaintenance::Cleanup(task.ProjectRoot, task.SchemaRevision, task.RendererRevision, resolve, &task, &manager->m_accept_texture_decodes.value);
        }
        catch (...)
        {
            ++stats.Errors;
        }
        {
            std::lock_guard lock(manager->m_pending_mutex);
            auto&           total = manager->m_thumbnail_maintenance_stats;
            ++total.Runs;
            total.Examined       += stats.Examined;
            total.Removed        += stats.Removed;
            total.ReclaimedBytes += stats.ReclaimedBytes;
            total.Errors         += stats.Errors;
            total.Pending         = false;
            task.WorkerQueued     = false;
        }
        manager->m_pending_texture_decodes.value.fetch_sub(1, std::memory_order_release);
    }

    void* RenderResourceManager::ReserveThumbnailCacheWrite(const char* filename, const ThumbnailCacheIdentity& identity)
    {
        std::lock_guard lock(m_pending_mutex);
        if (!m_accept_texture_decodes.value.load(std::memory_order_acquire) || !filename || !filename[0] || Helpers::secure_strlen(filename) >= MAX_FILE_PATH_COUNT)
        {
            return nullptr;
        }
        for (auto& task : m_thumbnail_cache_writes)
        {
            if (task.Reserved)
            {
                continue;
            }
            task.Owner    = this;
            task.Identity = identity;
            Helpers::secure_strcpy(task.Filename, sizeof(task.Filename), filename);
            task.Reserved     = true;
            task.Accepted     = false;
            task.WorkerQueued = false;
            return &task;
        }
        return nullptr;
    }

    void RenderResourceManager::AcceptThumbnailCacheWrite(void* context)
    {
        if (!context)
        {
            return;
        }
        std::lock_guard lock(m_pending_mutex);
        static_cast<ThumbnailCacheWriteTask*>(context)->Accepted = true;
    }

    void RenderResourceManager::CancelThumbnailCacheReadback(void* context)
    {
        auto&           task = *static_cast<ThumbnailCacheWriteTask*>(context);
        std::lock_guard lock(task.Owner->m_pending_mutex);
        task.Reserved = false;
    }

    void RenderResourceManager::CompleteThumbnailCacheReadback(const void* data, size_t size, void* context)
    {
        auto&           task    = *static_cast<ThumbnailCacheWriteTask*>(context);
        auto*           manager = task.Owner;
        std::lock_guard lock(manager->m_pending_mutex);
        if (!task.Accepted || !data || size != sizeof(task.Pixels) || !manager->m_accept_texture_decodes.value.load(std::memory_order_acquire) || Helpers::secure_memcpy(task.Pixels, sizeof(task.Pixels), data, size) != Helpers::MEMORY_OP_SUCCESS)
        {
            task.Reserved = false;
            return;
        }
        // Share the shutdown counter with texture workers. Increment while the
        // acceptance mutex is held, before publishing any worker job.
        manager->m_pending_texture_decodes.value.fetch_add(1, std::memory_order_release);
        task.WorkerQueued = true;
        // Bounded admission: reject saturated disk work, never execute it inline
        // (which would also re-enter m_pending_mutex).
        if (!Helpers::ThreadPoolHelper::Submit(&task, &RenderResourceManager::RunThumbnailCacheWrite))
        {
            task.Reserved     = false;
            task.WorkerQueued = false;
            manager->m_pending_texture_decodes.value.fetch_sub(1, std::memory_order_release);
        }
    }

    void RenderResourceManager::RunThumbnailCacheWrite(void* context)
    {
        auto& task     = *static_cast<ThumbnailCacheWriteTask*>(context);
        auto* manager  = task.Owner;
        auto* previous = Helpers::GetWorkerSlab();
        Helpers::SetWorkerSlab(nullptr);
        try
        {
            WriteThumbnailCache(task.Filename, task.Pixels, 128, 128, task.Identity, false);
        }
        catch (...)
        {
            // Optional persistence must not strand a slot or delay shutdown.
        }
        Helpers::SetWorkerSlab(previous);
        {
            std::lock_guard lock(manager->m_pending_mutex);
            task.Reserved     = false;
            task.WorkerQueued = false;
        }
        manager->m_pending_texture_decodes.value.fetch_sub(1, std::memory_order_release);
    }

    Rendering::Textures::TextureHandle RenderResourceManager::SubmitThumbnailTextureFile(const char* filename, uint32_t extent, bool track_decode, const char* cache_filename, const ThumbnailCacheIdentity* identity, bool cache_only)
    {
        constexpr uint32_t kMinThumbnailExtent = 16;
        if (extent < kMinThumbnailExtent || extent > kMaxThumbnailExtent)
        {
            return {};
        }
        return SubmitTextureFileInternal(filename, {}, track_decode, extent, cache_filename, identity, cache_only);
    }

    Rendering::Textures::TextureHandle RenderResourceManager::SubmitTextureFileInternal(const char* filename, Rendering::Textures::TextureHandle existing, bool track_decode, uint32_t thumbnail_extent, const char* cache_filename, const ThumbnailCacheIdentity* identity, bool cache_only)
    {
        using namespace Rendering::Specifications;

        std::unique_lock<std::mutex> l(m_pending_mutex);
        if (existing.Valid() && !BeginTextureVersionWrite(existing))
        {
            return {};
        }
        if (!m_accept_texture_decodes.value.load(std::memory_order_acquire) || !filename || filename[0] == '\0')
        {
            return {};
        }

        const size_t filename_length = Helpers::secure_strlen(filename);
        if (filename_length >= MAX_FILE_PATH_COUNT)
        {
            ZENGINE_CORE_ERROR("Texture path exceeds the {} byte engine limit: {}", MAX_FILE_PATH_COUNT - 1, filename)
            return {};
        }
        if (cache_filename && Helpers::secure_strlen(cache_filename) >= MAX_FILE_PATH_COUNT)
        {
            ZENGINE_CORE_ERROR("Thumbnail cache path exceeds the {} byte engine limit: {}", MAX_FILE_PATH_COUNT - 1, cache_filename)
            return {};
        }
        cstring file_ext = std::strrchr(filename, '.');
        if (!file_ext)
        {
            file_ext = "";
        }
        const bool is_environment_map = Helpers::secure_strcmp(file_ext, ".zenvmap") == 0;
        if (thumbnail_extent != 0 && is_environment_map)
        {
            return {};
        }
        if (Helpers::secure_strcmp(file_ext, ".hdr") == 0 || Helpers::secure_strcmp(file_ext, ".exr") == 0)
        {
            ZENGINE_CORE_ERROR("[RRM] Raw HDR environment maps must be imported into a .zenvmap artifact before runtime upload: {}", filename)
            return {};
        }

        TextureSpecification spec{};

        if (is_environment_map)
        {
            Importers::AssetCodec::EnvironmentMapFileHeader env_header{};
            if (!Importers::AssetCodec::ReadEnvironmentMapFileHeader(filename, env_header))
            {
                ZENGINE_CORE_ERROR("Failed to read .zenvmap header: {}", filename)
                return {};
            }
            spec.IsCubemap  = true;
            spec.LayerCount = static_cast<uint32_t>(env_header.LayerCount);
            spec.Format     = ImageFormat::R32G32B32A32_SFLOAT;
            spec.Width      = static_cast<uint32_t>(env_header.FaceWidth);
            spec.Height     = static_cast<uint32_t>(env_header.FaceHeight);
        }
        else
        {
            if (thumbnail_extent != 0)
            {
                // Preview output dimensions are known up front. Keep source/cache
                // header I/O on the worker, including corrupt-cache failures, so
                // visible-card construction does not synchronously open images.
                spec.Width  = thumbnail_extent;
                spec.Height = thumbnail_extent;
            }
            else
            {
                int w = 0, h = 0, ch = 0;
                if (!stbi_info(filename, &w, &h, &ch))
                {
                    return {};
                }
                spec.Width  = static_cast<uint32_t>(w);
                spec.Height = static_cast<uint32_t>(h);
            }
            // ZUI composites display-encoded RGB into a UNORM swapchain (like its
            // tokens and font atlas). SRGB sampling here would linearize preview
            // bytes without a matching output encode, making them visibly dark.
            spec.Format = thumbnail_extent != 0 ? ImageFormat::R8G8B8A8_UNORM : ImageFormat::R8G8B8A8_SRGB;
        }

        if (spec.IsCubemap)
        {
            const uint32_t largest_dimension = std::max(spec.Width, spec.Height);
            uint32_t       mip_count         = 1;
            for (uint32_t dimension = largest_dimension; dimension > 1; dimension >>= 1)
            {
                ++mip_count;
            }
            spec.MipLevelCount  = mip_count;
            // HDRI mip generation is recorded as a graph compute stage before
            // the cubemap is sampled by the IBL convolution passes.
            spec.IsUsageStorage = true;
        }

        spec.BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];

        Rendering::Textures::TextureHandle tex_handle;
        if (existing.Valid())
        {
            // Reimport — reconstruct in place only if dimensions/format actually changed;
            // same handle, same bindless index either way.
            auto* texture = m_device->GlobalTextures.Access(existing);
            if (texture && (texture->Width != spec.Width || texture->Height != spec.Height || texture->Specification.Format != spec.Format || texture->Specification.MipLevelCount != spec.MipLevelCount))
            {
                m_device->ReconstructTexture(existing, spec);
            }
            tex_handle = existing;
        }
        else
        {
            // Async file admission is recoverable, including byte/retirement
            // budget refusal. Never turn a saturated candidate into an assertion.
            tex_handle = m_device->TryCreateTexture(spec, nullptr, thumbnail_extent ? ThumbnailTextureSlotReserve : 0);
        }

        if (track_decode && (!tex_handle.Valid() || !TrackTextureDecode(tex_handle)))
        {
            if (tex_handle.Valid() && !existing.Valid())
            {
                m_device->DestroyTexture(tex_handle);
            }
            return {};
        }
        if (!tex_handle.Valid())
        {
            return {};
        }

        auto* task = static_cast<TextureDecodeTask*>(m_texture_task_slab.TryAllocate(sizeof(TextureDecodeTask), alignof(TextureDecodeTask)));
        if (!task)
        {
            if (track_decode)
            {
                ForgetTextureDecode(tex_handle);
            }
            if (!existing.Valid())
            {
                m_device->DestroyTexture(tex_handle);
            }
            return {};
        }
        ZConstruct<TextureDecodeTask>(task);
        task->Owner            = this;
        task->Specification    = spec;
        task->Texture          = tex_handle;
        task->IsEnvironmentMap = is_environment_map;
        task->TrackCompletion  = track_decode;
        task->FitIntoSquare    = thumbnail_extent != 0;
        task->CacheOnly        = cache_only;
        if (identity)
        {
            task->CacheIdentity = *identity;
        }
        Helpers::secure_strcpy(task->Filename, sizeof(task->Filename), filename);
        if (cache_filename)
        {
            Helpers::secure_strcpy(task->CacheFilename, sizeof(task->CacheFilename), cache_filename);
        }

        if (!m_queued_texture_decodes.push(task))
        {
            if (task->TrackCompletion)
            {
                if (existing.Valid())
                {
                    PublishTextureDecodeCompletion(tex_handle, false);
                }
                else
                {
                    ForgetTextureDecode(tex_handle);
                }
            }
            if (!existing.Valid())
            {
                m_device->DestroyTexture(tex_handle);
            }
            m_texture_task_slab.Free(task);
            ZENGINE_CORE_ERROR("[RRM] Texture decode queue full — rejecting {}", filename)
            return existing;
        }

        return tex_handle;
    }

    void RenderResourceManager::DispatchQueuedTextureDecodes()
    {
        for (;;)
        {
            uint8_t decode_slab_index = UINT8_MAX;
            if (!TryAcquireTextureDecodeSlab(&decode_slab_index))
            {
                return;
            }

            TextureDecodeTask* task = nullptr;
            if (!m_queued_texture_decodes.pop(task))
            {
                ReleaseTextureDecodeSlab(decode_slab_index);
                return;
            }

            if (TextureReleaseRequested(task->Texture))
            {
                PublishTextureDecodeCompletion(task->Texture, false);
                ReleaseTextureDecodeSlab(decode_slab_index);
                m_texture_task_slab.Free(task);
                continue;
            }

            task->DecodeSlabIndex = decode_slab_index;
            m_pending_texture_decodes.value.fetch_add(1, std::memory_order_release);
            if (Helpers::ThreadPoolHelper::Submit(task, &RenderResourceManager::RunTextureDecodeTask))
            {
                continue;
            }

            if (task->TrackCompletion)
            {
                PublishTextureDecodeCompletion(task->Texture, false);
            }
            ReleaseTextureDecodeSlab(task->DecodeSlabIndex);
            CompleteTextureDecodeTask(task);
            ZENGINE_CORE_WARN("[RRM] Texture decode rejected because the worker pool is unavailable or saturated")
            return;
        }
    }

    void RenderResourceManager::DiscardQueuedTextureDecodes()
    {
        TextureDecodeTask* task = nullptr;
        while (m_queued_texture_decodes.pop(task))
        {
            m_texture_task_slab.Free(task);
        }
    }

    bool RenderResourceManager::AspectFitThumbnail(const uint8_t* source, int width, int height, uint8_t* destination, uint32_t extent)
    {
        if (!source || !destination || width <= 0 || height <= 0 || extent == 0 || extent > kMaxThumbnailExtent)
        {
            return false;
        }

        const size_t byte_size = static_cast<size_t>(extent) * extent * STBI_rgb_alpha;
        Helpers::secure_memset(destination, 0, byte_size, byte_size);
        const uint32_t source_width  = static_cast<uint32_t>(width);
        const uint32_t source_height = static_cast<uint32_t>(height);
        const bool     width_limited = source_width >= source_height;
        const uint32_t scaled_width  = width_limited ? extent : std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(source_width) * extent) / source_height));
        const uint32_t scaled_height = width_limited ? std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(source_height) * extent) / source_width)) : extent;
        uint8_t* const fitted        = destination + ((extent - scaled_height) / 2u) * extent * STBI_rgb_alpha + ((extent - scaled_width) / 2u) * STBI_rgb_alpha;
        return stbir_resize_uint8(source, width, height, 0, fitted, static_cast<int>(scaled_width), static_cast<int>(scaled_height), static_cast<int>(extent * STBI_rgb_alpha), STBI_rgb_alpha) != 0;
    }

    void RenderResourceManager::RunTextureDecodeTask(void* context)
    {
        TextureDecodeTask*      task     = static_cast<TextureDecodeTask*>(context);
        RenderResourceManager*  manager  = task->Owner;
        Core::Memory::TLSFSlab* previous = Helpers::GetWorkerSlab();
        ZENGINE_VALIDATE_ASSERT(task->DecodeSlabIndex < MAX_CONCURRENT_TEXTURE_DECODES, "RenderResourceManager::RunTextureDecodeTask: task has no decode slab lease")
        Core::Memory::TLSFSlab* slab = &manager->m_upload_slabs[task->DecodeSlabIndex];
        Helpers::SetWorkerSlab(slab);
        uint8_t*   pixels    = nullptr;
        size_t     byte_size = 0;
        const bool cancelled = manager->TextureReleaseRequested(task->Texture);

        if (!cancelled && task->Specification.IsCubemap)
        {
            if (task->IsEnvironmentMap)
            {
                Rendering::Buffers::Bitmap cubemap = {};
                if (!Importers::AssetCodec::DeserializeEnvironmentMapFile(task->Filename, cubemap))
                {
                    ZENGINE_CORE_ERROR("Failed to deserialize .zenvmap: {}", task->Filename)
                }
                else
                {
                    byte_size = cubemap.BufferSize;
                    pixels    = static_cast<uint8_t*>(slab->TryRealloc(nullptr, byte_size));
                    if (pixels)
                    {
                        Helpers::secure_memmove(pixels, byte_size, cubemap.Buffer, byte_size);
                    }
                }
            }
            else
            {
                ZENGINE_CORE_ERROR("[RRM] Cubemap uploads require a cooked .zenvmap artifact: {}", task->Filename)
            }
        }
        else if (!cancelled)
        {
            // Rendered previews retain top-down GPU rows; ordinary source previews
            // use the existing bottom-up upload + UI V-flip convention.
            stbi_set_flip_vertically_on_load_thread(task->CacheOnly ? 0 : 1);
            int        width = 0, height = 0, channels = 0;
            stbi_uc*   image_data = task->FitIntoSquare ? ReadThumbnailCache(task->CacheFilename, task->Specification.Width, task->Specification.Height, task->CacheIdentity) : nullptr;
            const bool cache_hit  = image_data != nullptr;
            if (cache_hit)
            {
                width  = static_cast<int>(task->Specification.Width);
                height = static_cast<int>(task->Specification.Height);
            }
            else if (!task->CacheOnly)
            {
                image_data = stbi_load(task->Filename, &width, &height, &channels, STBI_rgb_alpha);
            }
            if (!image_data)
            {
                if (!task->CacheOnly)
                {
                    ZENGINE_CORE_ERROR("Failed to load texture: {}", task->Filename)
                }
            }
            else
            {
                const uint32_t target_width  = task->Specification.Width;
                const uint32_t target_height = task->Specification.Height;
                const size_t   total_pixels  = static_cast<size_t>(target_width) * static_cast<size_t>(target_height);
                byte_size                    = total_pixels * STBI_rgb_alpha;
                // Keep the decoded allocation when its dimensions already match.
                // Two 4K RGBA buffers plus TLSF overhead exceed a 128 MiB lease.
                if (width == static_cast<int>(target_width) && height == static_cast<int>(target_height))
                {
                    pixels = image_data;
                }
                else
                {
                    pixels = static_cast<uint8_t*>(slab->TryRealloc(nullptr, byte_size));
                    if (pixels)
                    {
                        const bool resized = task->FitIntoSquare ? AspectFitThumbnail(image_data, width, height, pixels, target_width) : stbir_resize_uint8(image_data, width, height, 0, pixels, static_cast<int>(target_width), static_cast<int>(target_height), 0, STBI_rgb_alpha) != 0;
                        if (!resized)
                        {
                            slab->Free(pixels);
                            pixels = nullptr;
                        }
                    }
                    stbi_image_free(image_data);
                }
                if (!pixels)
                {
                    ZENGINE_CORE_ERROR("[RRM] Texture resize failed or exceeded the decode memory budget: {}", task->Filename)
                }

                if (pixels && task->FitIntoSquare && !cache_hit && task->CacheFilename[0] != '\0')
                {
                    WriteThumbnailCache(task->CacheFilename, pixels, target_width, target_height, task->CacheIdentity);
                }
            }
        }

        bool decode_succeeded = false;
        if (pixels && byte_size > 0 && !manager->TextureReleaseRequested(task->Texture))
        {
            TextureDeferral deferral = {};
            deferral.Pixels          = pixels;
            deferral.ByteSize        = byte_size;
            deferral.Slab            = slab;
            deferral.DecodeSlabIndex = task->DecodeSlabIndex;
            deferral.TexHandle       = task->Texture;

            if (manager->EnqueueTextureDeferral(deferral))
            {
                decode_succeeded = true; // The upload deferral now owns the slab lease.
                if (!manager->m_device->RequestDeferredDescriptorUpdate(task->Texture))
                {
                    // The upload deferral owns the slab now and still has to
                    // drain. Sticky failure prevents its later upload notification
                    // from admitting a texture whose descriptor was never queued.
                    ZENGINE_CORE_ERROR("[RRM] Descriptor publication queue full: {}", task->Filename)
                    if (task->TrackCompletion)
                    {
                        manager->PublishTextureDecodeCompletion(task->Texture, false);
                    }
                }
            }
            else
            {
                ZENGINE_CORE_ERROR("[RRM] Texture deferral queue full — dropping decoded texture {}", task->Filename)
            }
        }

        // CPU success is not GPU readiness. The render-thread submission callback
        // publishes success only after a streaming ticket exists for this texture.
        if (task->TrackCompletion && !decode_succeeded)
        {
            manager->PublishTextureDecodeCompletion(task->Texture, false);
        }
        Helpers::SetWorkerSlab(previous);
        if (!decode_succeeded)
        {
            if (pixels)
            {
                // Ownership transfers only after a successful enqueue, never
                // on cancellation or queue refusal.
                slab->Free(pixels);
            }
            manager->ReleaseTextureDecodeSlab(task->DecodeSlabIndex);
        }
        manager->CompleteTextureDecodeTask(task);
    }

    void RenderResourceManager::CompleteTextureDecodeTask(TextureDecodeTask* task)
    {
        m_texture_task_slab.Free(task);
        m_pending_texture_decodes.value.fetch_sub(1, std::memory_order_release);
    }

    bool RenderResourceManager::TrackTextureDecode(const Rendering::Textures::TextureHandle& handle)
    {
        if (!handle.Valid() || handle.Index >= TextureDecodeTracker::MaxTracked)
        {
            return false;
        }

        std::lock_guard lock(m_texture_decode_mutex);
        auto&           tracked = m_texture_decode_tracker.Entries[handle.Index];
        if (tracked.Texture.Index == handle.Index && tracked.Texture.Generation == handle.Generation)
        {
            return true;
        }
        if (tracked.Texture.Valid() && tracked.Texture.Generation > handle.Generation)
        {
            return false;
        }
        tracked = {handle, TextureDecodeState::Pending};
        return true;
    }

    RenderResourceManager::TextureDecodeState RenderResourceManager::GetTextureDecodeState(const Rendering::Textures::TextureHandle& handle)
    {
        if (!handle.Valid() || handle.Index >= TextureDecodeTracker::MaxTracked)
        {
            return TextureDecodeState::Untracked;
        }
        std::lock_guard lock(m_texture_decode_mutex);
        const auto&     tracked = m_texture_decode_tracker.Entries[handle.Index];
        if (tracked.Texture.Index == handle.Index && tracked.Texture.Generation == handle.Generation)
        {
            return tracked.State;
        }
        return TextureDecodeState::Untracked;
    }

    void RenderResourceManager::ForgetTextureDecode(const Rendering::Textures::TextureHandle& handle)
    {
        if (!handle.Valid() || handle.Index >= TextureDecodeTracker::MaxTracked)
        {
            return;
        }
        std::lock_guard lock(m_texture_decode_mutex);
        auto&           tracked = m_texture_decode_tracker.Entries[handle.Index];
        if (tracked.Texture.Index == handle.Index && tracked.Texture.Generation == handle.Generation)
        {
            tracked = {};
        }
    }

    void RenderResourceManager::PublishTextureDecodeCompletion(const Rendering::Textures::TextureHandle& handle, bool success)
    {
        if (!handle.Valid() || handle.Index >= TextureDecodeTracker::MaxTracked)
        {
            return;
        }
        std::lock_guard lock(m_texture_decode_mutex);
        auto&           tracked = m_texture_decode_tracker.Entries[handle.Index];
        if (tracked.Texture.Index == handle.Index && tracked.Texture.Generation == handle.Generation && (!success || tracked.State == TextureDecodeState::Pending))
        {
            // Failure is sticky for this observation. A duplicate accepted-upload
            // notification cannot erase an already reported decode/rejection error.
            tracked.State = success ? TextureDecodeState::Succeeded : TextureDecodeState::Failed;
        }
    }

    void RenderResourceManager::MarkTextureUploadSubmitted(const Rendering::Textures::TextureHandle& handle)
    {
        PublishTextureDecodeCompletion(handle, true);
    }

    Rendering::Textures::TextureHandle RenderResourceManager::GetOrCreateFallbackTexture()
    {
        static constexpr const char* kFallbackRelativePath = "Settings/FallbackTexture.png";

        const auto*                  engine_context        = Engine::GetContext();
        if (!engine_context || !engine_context->EngineAssetsNativeRoot)
        {
            ZENGINE_CORE_ERROR("Fallback texture cannot be loaded because the engine asset root is unavailable")
            return {};
        }

        const std::string fallback_path = (std::filesystem::path(engine_context->EngineAssetsNativeRoot) / kFallbackRelativePath).string();
        if (!std::filesystem::exists(fallback_path))
        {
            // Keep GetOrCreateFallbackTexture's recovery behavior for development
            // packages that were produced without the bundled fallback image.
            static constexpr int     W = 4, H = 4;
            static constexpr uint8_t R = 255, G = 20, B = 147, A = 255;
            uint8_t                  pixels[W * H * 4];
            for (int i = 0; i < W * H; ++i)
            {
                pixels[i * 4 + 0] = R;
                pixels[i * 4 + 1] = G;
                pixels[i * 4 + 2] = B;
                pixels[i * 4 + 3] = A;
            }
            if (!stbi_write_png(fallback_path.c_str(), W, H, 4, pixels, W * 4))
            {
                ZENGINE_CORE_ERROR("Failed to create fallback texture: {}", fallback_path)
                return {};
            }
        }

        auto result = SubmitTextureFile(fallback_path.c_str());
        if (result.Valid())
        {
            auto texture = m_device->GlobalTextures.Access(result);
            if (texture)
            {
                auto img_buf = m_device->ImageBufferManager.Access(texture->BufferHandle);
                if (img_buf)
                {
                    m_device->FallbackDescriptorImageInfo             = img_buf->GetDescriptorImageInfo();
                    m_device->FallbackDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
            }
        }
        return result;
    }

    Textures::TextureHandle RenderResourceManager::CreateSynchronousTexture(const Specifications::TextureSpecification& specification, const void* pixels, cstring debug_name)
    {
        using namespace Rendering::Specifications;
        using namespace Rendering::Primitives;

        if (!m_device || !m_upload_cmd_mgr || !m_sync_upload_fence || !pixels)
        {
            return {};
        }

        const auto  handle  = m_device->TryCreateTexture(specification, debug_name);
        auto* const texture = m_device->GlobalTextures.Access(handle);
        auto* const image   = texture ? m_device->ImageBufferManager.Access(texture->BufferHandle) : nullptr;
        if (!texture || !image || image->GetHandle() == VK_NULL_HANDLE)
        {
            if (handle.Valid())
            {
                m_device->DestroyTexture(handle);
            }
            return {};
        }

        ImageMemoryBarrierSpecification to_transfer = {};
        to_transfer.ImageHandle                     = image->GetHandle();
        to_transfer.OldLayout                       = image->Layout;
        to_transfer.NewLayout                       = ImageLayout::TRANSFER_DST_OPTIMAL;
        to_transfer.ImageAspectMask                 = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.SourceAccessMask                = VK_ACCESS_NONE;
        to_transfer.DestinationAccessMask           = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.SourceStageMask                 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        to_transfer.DestinationStageMask            = VK_PIPELINE_STAGE_TRANSFER_BIT;
        to_transfer.LayerCount                      = specification.LayerCount;
        to_transfer.SourceQueueFamily               = m_device->GraphicFamilyIndex;
        to_transfer.DestinationQueueFamily          = m_device->GraphicFamilyIndex;

        ImageMemoryBarrierSpecification to_read     = {};
        to_read.ImageHandle                         = image->GetHandle();
        to_read.OldLayout                           = ImageLayout::TRANSFER_DST_OPTIMAL;
        to_read.NewLayout                           = ImageLayout::SHADER_READ_ONLY_OPTIMAL;
        to_read.ImageAspectMask                     = VK_IMAGE_ASPECT_COLOR_BIT;
        to_read.SourceAccessMask                    = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_read.DestinationAccessMask               = VK_ACCESS_SHADER_READ_BIT;
        to_read.SourceStageMask                     = VK_PIPELINE_STAGE_TRANSFER_BIT;
        to_read.DestinationStageMask                = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        to_read.LayerCount                          = specification.LayerCount;
        to_read.SourceQueueFamily                   = m_device->GraphicFamilyIndex;
        to_read.DestinationQueueFamily              = m_device->GraphicFamilyIndex;

        auto* const command_buffer                  = m_upload_cmd_mgr->GetCommandBuffer(QueueType::GRAPHIC_QUEUE, m_active_frame_index, 0, 0, false);
        m_sync_upload_fence->Wait(UINT64_MAX);
        m_sync_upload_fence->Reset();
        command_buffer->ResetState();
        vkResetCommandBuffer(command_buffer->GetHandle(), 0);
        command_buffer->Begin();
        command_buffer->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
        image->Layout      = to_transfer.NewLayout;
        // This upload is synchronously fenced and does not signal RenderTimeline.
        // Keep its staging allocation independent from the render-frame ring so its
        // reuse cannot depend on an unrelated future frame submission.
        BufferView staging = m_device->WriteTextureData(command_buffer, handle, pixels, nullptr, false);
        command_buffer->TransitionImageLayout(ImageMemoryBarrier{to_read});
        image->Layout = to_read.NewLayout;
        command_buffer->End();

        const VkQueue         queue = m_device->GetQueue(QueueType::GRAPHIC_QUEUE).Handle;
        const VkCommandBuffer raw   = command_buffer->GetHandle();
        VkSubmitInfo          submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &raw;
        if (vkQueueSubmit(queue, 1, &submit, m_sync_upload_fence->GetHandle()) != VK_SUCCESS)
        {
            if (staging)
            {
                m_device->GpuMem.FreeBuffer(staging);
            }
            m_device->DestroyTexture(handle);
            return {};
        }
        m_sync_upload_fence->Wait(UINT64_MAX);
        command_buffer->ResetState();

        if (staging)
        {
            m_device->GpuMem.FreeBuffer(staging);
        }

        return handle;
    }

    Rendering::Textures::TextureHandle RenderResourceManager::GetOrCreateFallbackCubemap()
    {
        using namespace Rendering::Specifications;

        if (m_fallback_cubemap.Valid())
        {
            return m_fallback_cubemap;
        }

        // A dim neutral-blue source keeps the viewport visibly usable while an
        // HDRI or future atmosphere bake is pending. It is a real cubemap, so
        // samplerCube descriptors are always valid on the first frame.
        constexpr uint8_t fallback_pixels[6 * 4] = {
            35, 61, 89, 255, 35, 61, 89, 255, 35, 61, 89, 255, 35, 61, 89, 255, 35, 61, 89, 255, 35, 61, 89, 255,
        };

        TextureSpecification specification = {};
        specification.IsCubemap            = true;
        specification.LayerCount           = 6;
        specification.Width                = 1;
        specification.Height               = 1;
        specification.Format               = ImageFormat::R8G8B8A8_UNORM;

        m_fallback_cubemap                 = CreateSynchronousTexture(specification, fallback_pixels, "SkyEnvironmentFallback");
        return m_fallback_cubemap;
    }

    EnvironmentLightingResources RenderResourceManager::GetOrCreateFallbackEnvironmentLighting()
    {
        using namespace Rendering::Specifications;

        if (m_fallback_environment_lighting.Valid())
        {
            return m_fallback_environment_lighting;
        }

        constexpr BrdfIntegrationLutKey lut_key = {
            .ShaderVersion = 1,
            .Resolution    = kBrdfLutResolution,
            .SampleCount   = kBrdfLutSampleCount,
        };
        const uint16_t neutral_radiance    = FloatToHalf(0.03f);
        const uint16_t opaque_alpha        = FloatToHalf(1.0f);
        uint16_t       neutral_cube[6 * 4] = {};
        for (uint32_t face = 0; face < 6; ++face)
        {
            const uint32_t pixel    = face * 4;
            neutral_cube[pixel]     = neutral_radiance;
            neutral_cube[pixel + 1] = neutral_radiance;
            neutral_cube[pixel + 2] = neutral_radiance;
            neutral_cube[pixel + 3] = opaque_alpha;
        }

        TextureSpecification cube_spec         = {};
        cube_spec.IsCubemap                    = true;
        cube_spec.LayerCount                   = 6;
        cube_spec.Width                        = 1;
        cube_spec.Height                       = 1;
        cube_spec.BytePerPixel                 = sizeof(uint16_t) * 4;
        cube_spec.Format                       = ImageFormat::R16G16B16A16_SFLOAT;

        EnvironmentLightingResources resources = {};
        resources.DiffuseIrradiance            = CreateSynchronousTexture(cube_spec, neutral_cube, "FallbackDiffuseIrradiance");
        resources.SpecularEnvironment          = CreateSynchronousTexture(cube_spec, neutral_cube, "FallbackSpecularEnvironment");
        if (!resources.DiffuseIrradiance.Valid() || !resources.SpecularEnvironment.Valid())
        {
            if (resources.DiffuseIrradiance.Valid())
            {
                m_device->DestroyTexture(resources.DiffuseIrradiance);
            }
            if (resources.SpecularEnvironment.Valid())
            {
                m_device->DestroyTexture(resources.SpecularEnvironment);
            }
            return {};
        }

        const size_t pixel_count = static_cast<size_t>(lut_key.Resolution) * lut_key.Resolution * 4;
        auto* const  lut_pixels  = static_cast<uint16_t*>(m_synchronous_texture_scratch.Alloc(pixel_count * sizeof(uint16_t)));
        if (!lut_pixels)
        {
            m_device->DestroyTexture(resources.DiffuseIrradiance);
            m_device->DestroyTexture(resources.SpecularEnvironment);
            return {};
        }
        GenerateBrdfIntegrationLut(lut_pixels, lut_key.Resolution, lut_key.SampleCount);

        TextureSpecification lut_spec = {};
        lut_spec.Width                = lut_key.Resolution;
        lut_spec.Height               = lut_key.Resolution;
        lut_spec.BytePerPixel         = sizeof(uint16_t) * 4;
        lut_spec.Format               = ImageFormat::R16G16B16A16_SFLOAT;
        resources.BrdfIntegrationLut  = CreateSynchronousTexture(lut_spec, lut_pixels, "BrdfIntegrationLut");
        m_synchronous_texture_scratch.Free(lut_pixels);

        if (!resources.BrdfIntegrationLut.Valid())
        {
            m_device->DestroyTexture(resources.DiffuseIrradiance);
            m_device->DestroyTexture(resources.SpecularEnvironment);
            return {};
        }

        resources.BrdfIntegrationKey    = lut_key;
        resources.SpecularMipCount      = 1;
        m_fallback_environment_lighting = resources;
        return m_fallback_environment_lighting;
    }

} // namespace ZEngine::Rendering
