#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>

namespace ZEngine::Rendering
{
    class RenderResourceManager;
}

namespace ZEngine::Rendering::Scenes
{
    struct GridConfig
    {
        float CellSize      = 0.025f;
        float FadeRadius    = 500.0f;
        float FadeStrength  = 0.5f;
        float LineWidth     = 1.5f;
        float GroundY       = 0.0f;
        int   MaxLOD        = 5;
        float ColorThin[4]  = {0.6f, 0.6f, 0.6f, 1.0f};
        float ColorThick[4] = {0.3f, 0.3f, 0.3f, 1.0f};
        float ColorXAxis[4] = {0.9f, 0.2f, 0.2f, 1.0f};
        float ColorZAxis[4] = {0.2f, 0.4f, 1.0f, 1.0f};
        bool  Enabled       = true;
    };

    // Sky rendering configuration — stored per scene, serialized in .zescene.
    // Supported modes: "atmosphere" (default), "hdri", "skySphere".
    struct SkyConfig
    {
        ZEngine::Core::Containers::String Mode           = {}; // "atmosphere", "hdri", "skySphere"
        ZEngine::Core::Containers::String EnvironmentMap = {}; // .zenvmap filename (hdri mode only)

        bool                              IsHDRI() const
        {
            return Mode.c_str() && (strcmp(Mode.c_str(), "hdri") == 0);
        }
        bool IsAtmosphere() const
        {
            return Mode.empty() || (strcmp(Mode.c_str(), "atmosphere") == 0);
        }
        bool IsSkySphere() const
        {
            return Mode.c_str() && (strcmp(Mode.c_str(), "skySphere") == 0);
        }
    };

    struct GpuDirectionalLight
    {
        ZEngine::Core::Maths::Vec4f Direction = {};
        ZEngine::Core::Maths::Vec4f Color     = {};
        float                       Intensity = 0.f;
        float                       _pad[3]   = {};
    };

    struct GpuPointLight
    {
        ZEngine::Core::Maths::Vec4f Position  = {};
        ZEngine::Core::Maths::Vec4f Color     = {};
        float                       Intensity = 0.f;
        float                       Radius    = 0.f;
        float                       _pad[2]   = {};
    };

    struct LightArrayUBO
    {
        GpuDirectionalLight DirectionalLights[4] = {};
        GpuPointLight       PointLights[8]       = {};
        uint32_t            DirectionalCount     = 0;
        uint32_t            PointCount           = 0;
        uint32_t            _pad[2]              = {};
    };

    // std430-compatible input record for GPU frustum culling. The compute shader
    // copies Command verbatim and changes instanceCount for culled entries only.
    struct FrustumCullingInput
    {
        Core::Maths::Vec4f    WorldBounds = {}; // xyz = world-space sphere center, w <= 0 means always visible
        VkDrawIndirectCommand Command     = {};
    };

    // Explicit tail padding makes this exactly match the GLSL push-constant block.
    struct FrustumCullingPushConstants
    {
        Core::Maths::Vec4f FrustumPlanes[6] = {};
        uint32_t           DrawCount        = 0;
        uint32_t           Padding[3]       = {};
    };

    static_assert(sizeof(FrustumCullingInput) == 32, "FrustumCullingInput must match its std430 GLSL representation");
    static_assert(sizeof(FrustumCullingPushConstants) == 112, "FrustumCullingPushConstants must match its GLSL representation");

    struct SceneData
    {
        static constexpr uint32_t   MAX_FRAMES_IN_FLIGHT                        = 3;
        static constexpr uint32_t   MAX_DRAW_COMMANDS                           = 8192;

        // Camera UBO — migrated to PerFrameUploadHeap; offset updated each frame in DrawScene
        uint32_t                    CameraHeapOffset                            = 0;

        // CPU-built draw candidates. The compute pass writes a matching indirect
        // array with instanceCount = 0 for frustum-culled candidates.
        uint32_t                    IndirectCommandCount                        = 0;
        FrustumCullingPushConstants CullingPushConstants                        = {};

        // Per-frame buffers prevent CPU uploads for frame N + 1 from racing GPU
        // reads issued for an earlier in-flight frame.
        Core::Memory::BufferView    TransformBuffers[MAX_FRAMES_IN_FLIGHT]      = {};
        Core::Memory::BufferView    MaterialBuffers[MAX_FRAMES_IN_FLIGHT]       = {};
        Core::Memory::BufferView    RenderDataBuffers[MAX_FRAMES_IN_FLIGHT]     = {};
        Core::Memory::BufferView    LightBuffers[MAX_FRAMES_IN_FLIGHT]          = {};

        // Per-frame ownership prevents frame N + 1 compute from overwriting the
        // commands still consumed by graphics for frame N.
        Core::Memory::BufferView    CullingInputBuffers[MAX_FRAMES_IN_FLIGHT]   = {};
        Core::Memory::BufferView    CulledIndirectBuffers[MAX_FRAMES_IN_FLIGHT] = {};

        // RRM vertex buffer handle — index buffer is paired via RRM::GetIndexBuffer(RMMVertexHandle).
        Rendering::BufferHandle     RMMVertexHandle                             = {};
    };
    ZDEFINE_PTR(SceneData);

    // One placed instance of a mesh asset in the scene.
    // Every drag-drop creates a new MeshInstance — even the same mesh dropped
    // twice produces two independent instances with separate transforms.
    struct MeshInstance
    {
        Core::Maths::Mat4f Transform = {}; // 64 bytes — owns its own cache line
        uuids::uuid        MeshUUID  = {}; // 16 bytes
        uint32_t           Id        = 0;
        char               Name[128] = {};
    };

    // Seqlock-protected scene state.
    //   Main thread:   Add/Remove/SetTransform + MarkInstancesDirty
    //   Render thread: GetInstancesSnapshot (spin-wait when seq is odd)
    //
    // Sequence counter convention:
    //   even → data stable (safe to snapshot)
    //   odd  → write in progress (reader spins)
    //
    // Arena allocators never free: even a "torn" pointer to Instances.m_data
    // points at still-valid memory, so retry-on-mismatch is safe.
    struct RenderScene
    {
        Core::Containers::Array<MeshInstance> Instances          = {};
        Core::Memory::ArenaAllocator          InstanceArena      = {};
        uint32_t                              NextInstanceId     = 1;

        SkyConfig                             Sky                = {};
        GridConfig                            Grid               = {};
        LightArrayUBO                         PendingLights      = {};

        PaddedAtomic<uint64_t>                m_seq              = {};
        PaddedAtomic<int32_t>                 SelectedInstanceId = {};
        PaddedAtomic<bool>                    InstancesDirty[3]  = {};
        PaddedAtomic<bool>                    SkyDirty[3]        = {};
        PaddedAtomic<bool>                    GridDirty[3]       = {};

        uint32_t                              AddMeshInstance(const uuids::uuid& uuid, const char* name);
        void                                  RemoveMeshInstance(uint32_t id, ZEngine::Rendering::RenderResourceManager* rrm = nullptr);
        void                                  SetInstanceTransform(uint32_t id, const Core::Maths::Mat4f& t);
        void                                  MarkInstancesDirty();

        // Fills `out` with a consistent copy; retries if a write was in progress.
        void                                  GetInstancesSnapshot(Core::Memory::ArenaAllocator* scratch, Core::Containers::Array<MeshInstance>& out) const;

    protected:
        void SeqBeginWrite();
        void SeqEndWrite();
    };
    ZDEFINE_PTR(RenderScene);

} // namespace ZEngine::Rendering::Scenes
