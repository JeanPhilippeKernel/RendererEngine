# ZEngine — Audio System

**Priority:** P1 — Required before any game with sound can ship
**Status:** Design
**Depends on:** `actor-ecs-architecture.md`, `vfs-design.md` (Ticket 1)
**Blocks:** Sound effects, music, spatial audio
**Recommended library:** miniaudio (public domain, single-header, https://miniaud.io)

---

## 1. Why miniaudio

miniaudio is the correct choice for ZEngine for the following reasons:

- **License:** Public domain (unlicense). Zero legal overhead; can be embedded in proprietary games without attribution.
- **Distribution model:** Single-header (`miniaudio.h`). No build system integration complexity, no DLL, no static lib to maintain. One `#define MINIAUDIO_IMPLEMENTATION` in one `.cpp` file and it is linked.
- **No external dependencies:** miniaudio uses only the OS audio APIs. It does not require SDL, OpenAL, FMOD, or any third-party runtime.
- **Cross-platform:** CoreAudio (macOS/iOS), WASAPI (Windows), ALSA/PulseAudio/JACK (Linux), Web Audio API (WASM). One codebase, no ifdefs in ZEngine code.
- **3D spatial audio built-in:** `ma_engine` provides a built-in spatial audio pipeline using `ma_sound`'s `ma_sound_set_position`, `ma_sound_set_velocity`, and a `ma_listener` per scene. Attenuation models (linear, exponential, inverse) are configurable per-sound.
- **Streaming support:** miniaudio supports both in-memory and streaming decoding. Long music tracks decode from disk on a background thread without ZEngine managing threading.
- **Custom allocator injection:** `ma_allocation_callbacks` is passed to `ma_engine_init` and propagated to every internal allocation. This routes miniaudio's memory through ZEngine's tracked allocator with zero hot-path overhead.
- **Zero allocation in hot path:** Per-frame positional updates call `ma_sound_set_position` and `ma_sound_set_velocity`, which are lock-free writes to internal state. No heap allocation occurs during `WorldTick`.

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                           ECS::Scene                                │
│  AudioSourceComponent       AudioListenerComponent  TransformComponent│
│  (ClipHandle, voice state)  (Primary flag)          (position/rot)  │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ reads/writes via ECS systems
              ┌────────────▼────────────────────────────┐
              │          AudioEngine singleton           │
              │  ZEngine::Audio::AudioEngine             │
              │  owns ma_engine                          │
              │  owns AudioManager (clip pool)           │
              │  owns AudioCommandBuffer                 │
              └────────────┬────────────────────────────┘
                           │ ma_* API calls
              ┌────────────▼────────────────────────────┐
              │       miniaudio (ma_engine)              │
              │  ma_sound per voice                      │
              │  ma_listener for 3D spatialization       │
              │  Streaming decoder for music tracks      │
              └─────────────────────────────────────────┘
```

**Design constraints:**
1. Gameplay code never calls `AudioEngine` or `AudioManager` directly during `WorldTick`. It posts `AudioCommand` entries to the command buffer.
2. `AudioEngine::Flush()` drains the command buffer after all ECS systems complete in the same frame, serializing audio state changes.
3. 3D positional updates are the only per-frame work in ECS systems — these are lightweight `ma_sound_set_position` calls that touch no heap allocator.
4. `#include <miniaudio.h>` is confined to `ZEngine/Audio/*.cpp` files. No miniaudio type leaks into ECS components or Audio public headers.
5. `AudioSourceComponent` stores only handle indices (uint32_t). The actual `ma_sound` objects live in `AudioManager`'s voice pool.

---

## 3. ECS Components

All components are plain data structs. No virtual methods. No behavior. Lives in `ZEngine::ECS::Components`.

### 3.1 `AudioSourceComponent`

```cpp
// ZEngine/ECS/Components/AudioSourceComponent.h
#pragma once
#include <cstdint>

namespace ZEngine::ECS::Components {

    // Attached to any entity that can emit sound.
    // The entity must also have a TransformComponent if Is3D == true.
    struct AudioSourceComponent {
        // Handle into AudioManager's clip pool (loaded via VFS).
        // UINT32_MAX means no clip assigned.
        uint32_t ClipHandle  = UINT32_MAX;

        // Active voice handle returned by AudioManager::Play().
        // UINT32_MAX means no voice currently playing.
        uint32_t VoiceHandle = UINT32_MAX;

        float Volume      = 1.0f;   // [0, 1]; multiplied with master volume
        float Pitch       = 1.0f;   // [0.25, 4.0]; 1.0 = original pitch
        float MinDistance = 1.0f;   // metres; full volume within this radius
        float MaxDistance = 20.0f;  // metres; inaudible beyond this radius

        bool Loop        = false;   // loop the clip when it reaches the end
        bool PlayOnCreate = false;  // auto-play when component is added to an entity
        bool Is3D        = true;    // if false: 2D pan (UI sounds, music stingers)
        bool IsPlaying   = false;   // set to true to start, false to stop

        // Attenuation model for 3D sounds:
        //   0 = linear (ma_attenuation_model_linear)
        //   1 = exponential (ma_attenuation_model_exponential)
        //   2 = inverse (ma_attenuation_model_inverse)
        uint8_t AttenuationModel = 0;
    };

}  // namespace ZEngine::ECS::Components
```

### 3.2 `AudioListenerComponent`

```cpp
// ZEngine/ECS/Components/AudioListenerComponent.h
#pragma once

namespace ZEngine::ECS::Components {

    // Marks an entity as the audio listener. Should be added to the camera entity
    // (or the player entity if no camera exists). Only one entity should have
    // Primary == true; if multiple entities have Primary == true, the first one
    // found by AudioListenerSystem is used and a debug warning is emitted.
    struct AudioListenerComponent {
        bool Primary = true;
    };

}  // namespace ZEngine::ECS::Components
```

---

## 4. `AudioManager`

`AudioManager` owns the clip asset pool and the active voice pool. It is not a public API for gameplay code — it is used only by ECS systems and `AudioEngine::Flush`.

### 4.1 Supporting types

```cpp
// ZEngine/Audio/AudioTypes.h
#pragma once
#include <cstdint>

namespace ZEngine::Audio {

    static constexpr uint32_t INVALID_CLIP_HANDLE  = UINT32_MAX;
    static constexpr uint32_t INVALID_VOICE_HANDLE = UINT32_MAX;
    static constexpr uint32_t INVALID_MUSIC_HANDLE = UINT32_MAX;

    // Supported audio formats; detected from file extension at load time
    enum class AudioFormat : uint8_t {
        Unknown = 0,
        OGG     = 1,   // OGG Vorbis — preferred for music and long SFX
        WAV     = 2,   // PCM WAV — preferred for short SFX (lowest decode latency)
        MP3     = 3,   // MP3 — use only when OGG is unavailable (patents expired, but OGG is cleaner)
        FLAC    = 4,   // lossless — editor-side; not recommended in shipping builds
    };

    // Detect format from file path extension (case-insensitive)
    AudioFormat DetectFormat(const char* path);

    // Opaque handle to a streaming music track
    using MusicHandle = uint32_t;

    // Command types for the AudioCommandBuffer
    enum class AudioCommandType : uint8_t {
        Play,
        Stop,
        SetVolume,
        SetPitch,
        SetLoop,
        PlayMusic,
        StopMusic,
        FadeMusic,
    };

    struct AudioCommand {
        AudioCommandType Type       = AudioCommandType::Play;
        uint32_t         ClipHandle = UINT32_MAX;
        uint32_t         EntityHint = UINT32_MAX;  // entity ID index
        uint32_t         EntityGen  = 0;           // entity ID generation for back-fill
        float            ParamA     = 1.f;
        float            ParamB     = 0.f;
        bool             ParamBool  = false;
    };

}  // namespace ZEngine::Audio
```

### 4.2 `AudioManager` class declaration

```cpp
// ZEngine/Audio/AudioManager.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Containers/String.h>
#include <Core/Maths/Vector.h>
#include <Audio/AudioTypes.h>
#include <cstdint>

// Forward-declare VFS types to keep this header VFS-free for tests
namespace ZEngine::VFS { struct IVFSContext; struct VFSPath; }

// Forward-declare miniaudio types so miniaudio.h is confined to AudioManager.cpp
struct ma_engine;
struct ma_sound;
struct ma_decoder;
struct ma_allocation_callbacks;

namespace ZEngine::Audio {

    // Per-clip record stored in the clip pool.
    // The ma_sound here is a template sound — cloned for each playing voice.
    struct ClipAsset {
        Core::Containers::String  Path;      // VFS path, for reload / debug
        ma_sound*                 Template  = nullptr;
        uint32_t                  RefCount  = 0;
        bool                      Valid     = false;

        // NEVER copy a ClipAsset — the copy constructor is deleted to prevent double-free.
        // Non-copyable: ma_sound* ownership is exclusive.
        ClipAsset() = default;
        ClipAsset(const ClipAsset&) = delete;
        ClipAsset& operator=(const ClipAsset&) = delete;
        ClipAsset(ClipAsset&& o) noexcept {
            Path     = std::move(o.Path);
            Template = o.Template;  o.Template = nullptr;   // transfer ownership immediately
            RefCount = o.RefCount;  o.RefCount = 0;
            Valid    = o.Valid;     o.Valid    = false;
        }
        ~ClipAsset() {
            if (Template) {
                ma_sound_uninit(Template);
                Template = nullptr;  // idempotent: safe to call dtor multiple times (e.g. in tests)
            }
        }
    };

    // Per-voice record: one playing instance of a clip.
    struct VoiceRecord {
        ma_sound*     Sound      = nullptr;
        uint32_t      ClipHandle = INVALID_CLIP_HANDLE;
        uint32_t      EntityHint = UINT32_MAX;  // entity that owns this voice
        bool          InUse      = false;
    };

    class AudioManager {
    public:
        AudioManager() = default;
        ~AudioManager();

        AudioManager(const AudioManager&)            = delete;
        AudioManager& operator=(const AudioManager&) = delete;

        // ------------------------------------------------------------------ //
        //  Lifecycle
        // ------------------------------------------------------------------ //

        // Must be called after ma_engine is initialized.
        //   arena    — for internal bookkeeping arrays (not per-sound memory)
        //   vfs      — for clip loading
        //   engine   — the initialized ma_engine
        void Initialize(
            Core::Memory::ArenaAllocator* arena,
            VFS::IVFSContext*             vfs,
            ma_engine*                    engine
        );

        void Shutdown();

        // ------------------------------------------------------------------ //
        //  Clip management
        // ------------------------------------------------------------------ //

        // Load a clip from VFS into the template pool.
        // Returns INVALID_CLIP_HANDLE on failure.
        [[nodiscard]] uint32_t LoadClip(const VFS::VFSPath& path);

        // Decrement refcount; unload if it reaches zero.
        void UnloadClip(uint32_t handle);

        // Check whether a handle is valid and loaded.
        bool IsClipLoaded(uint32_t handle) const;

        // ------------------------------------------------------------------ //
        //  Voice management
        // ------------------------------------------------------------------ //

        // Clone the template sound and start playing it.
        //   entity_hint — EntityID of the entity driving this sound (for 3D position)
        //   loop        — loop the clip
        //   volume      — [0, 1]
        // Returns INVALID_VOICE_HANDLE on failure.
        [[nodiscard]] uint32_t Play(
            uint32_t clip_handle,
            uint32_t entity_hint,
            bool     loop,
            float    volume
        );

        // Stop and release a voice.
        void Stop(uint32_t voice_handle);

        // Set the 3D position of a voice (called every frame by AudioSourceSystem).
        void SetVoicePosition(
            uint32_t                    voice_handle,
            const Core::Maths::Vec3f&   position,
            const Core::Maths::Vec3f&   velocity  // for doppler; zero if not needed
        );

        void SetVoiceVolume(uint32_t voice_handle, float volume);
        void SetVoicePitch(uint32_t voice_handle, float pitch);
        void SetVoiceLoop(uint32_t voice_handle, bool loop);

        bool IsVoicePlaying(uint32_t voice_handle) const;

        // ------------------------------------------------------------------ //
        //  Listener
        // ------------------------------------------------------------------ //

        // Called by AudioListenerSystem each frame.
        void SetListenerTransform(
            const Core::Maths::Vec3f& position,
            const Core::Maths::Vec3f& forward,
            const Core::Maths::Vec3f& up
        );

    private:
        uint32_t AllocateVoiceSlot();
        void     FreeVoiceSlot(uint32_t index);

        Core::Memory::ArenaAllocator* m_Arena  = nullptr;
        VFS::IVFSContext*             m_VFS    = nullptr;
        ma_engine*                    m_Engine = nullptr;

        // Clip pool: dense array; handle == index.
        // Max 4096 clips; resize if a project needs more.
        static constexpr uint32_t MAX_CLIPS  = 4096u;
        static constexpr uint32_t MAX_VOICES = 512u;

        ClipAsset   m_Clips[MAX_CLIPS]    = {};
        VoiceRecord m_Voices[MAX_VOICES]  = {};

        bool m_Initialized = false;
    };

}  // namespace ZEngine::Audio
```

### 4.3 Allocation callback wiring

```cpp
// ZEngine/Audio/AudioEngine.cpp  (key snippet)

// Miniaudio allocation callbacks — route through a dedicated heap-backed pool.
// ArenaAllocator cannot be used here because miniaudio requires realloc semantics
// (arbitrary resize of existing allocations) which arenas do not support.
// Instead, use a dedicated fixed-size pool tracked separately from the engine arena.
// The pool is initialized in AudioEngine::Initialize and freed in Shutdown.
//
// DOD note: miniaudio's internal allocations (decoder state, engine nodes) occur
// only during LoadClip/PlayMusic — never in the per-frame hot path (positional
// updates use lock-free writes to pre-allocated sound state).
static void* MiniaudioAlloc(size_t sz, void* userdata) {
    // userdata is a pointer to a simple heap pool counter for budget tracking.
    // Allocation routes through the OS heap — isolated from engine arenas.
    auto* budget = static_cast<std::atomic<size_t>*>(userdata);
    if (budget) budget->fetch_add(sz, std::memory_order_relaxed);
    return ::malloc(sz);
}
static void MiniaudioFree(void* ptr, void* userdata) {
    // Budget tracking on free is approximate (size unknown here).
    (void)userdata;
    ::free(ptr);
}
static void* MiniaudioRealloc(void* ptr, size_t sz, void* userdata) {
    auto* budget = static_cast<std::atomic<size_t>*>(userdata);
    if (budget) budget->fetch_add(sz, std::memory_order_relaxed);
    return ::realloc(ptr, sz);
}

// Used when constructing ma_engine_config:
ma_allocation_callbacks alloc_callbacks;
alloc_callbacks.pUserData  = nullptr;  // could pass arena ptr here
alloc_callbacks.onMalloc   = MiniaudioAlloc;
alloc_callbacks.onRealloc  = MiniaudioRealloc;
alloc_callbacks.onFree     = MiniaudioFree;

ma_engine_config engine_config      = ma_engine_config_init();
engine_config.allocationCallbacks   = alloc_callbacks;
engine_config.listenerCount         = 1;
engine_config.channels              = 2;   // stereo output
engine_config.sampleRate            = 48000;
```

---

## 5. `AudioEngine`

`AudioEngine` is the top-level singleton. It owns `ma_engine`, `AudioManager`, the command buffer, and the streaming music pool.

```cpp
// ZEngine/Audio/AudioEngine.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>
#include <Core/Maths/Vector.h>
#include <Audio/AudioTypes.h>
#include <Audio/AudioManager.h>

namespace ZEngine::VFS { struct IVFSContext; }
struct ma_engine;
struct ma_sound;

namespace ZEngine::Audio {

    // Streaming music record — one per active music track.
    struct MusicTrack {
        ma_sound*   Sound          = nullptr;
        float       TargetVolume   = 1.0f;
        float       FadeRemaining  = 0.0f;   // seconds remaining in current fade
        float       FadeDuration   = 0.0f;
        bool        InUse          = false;
    };

    class AudioEngine {
    public:
        AudioEngine() = default;
        ~AudioEngine();

        AudioEngine(const AudioEngine&)            = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        // ------------------------------------------------------------------ //
        //  Lifecycle
        // ------------------------------------------------------------------ //

        // Initialize ma_engine and AudioManager.
        //   arena    — for bookkeeping (not per-sample memory)
        //   vfs      — for all asset loading
        void Initialize(
            Core::Memory::ArenaAllocator* arena,
            VFS::IVFSContext*             vfs
        );

        void Shutdown();

        // ------------------------------------------------------------------ //
        //  Frame update
        // ------------------------------------------------------------------ //

        // Called by the engine frame loop AFTER all ECS systems complete.
        // Drains the command buffer and processes music fades.
        // scene: required for VoiceHandle back-fill (writing back to AudioSourceComponent).
        // Must be called on the main thread after WorldTick::Tick completes.
        void Flush(float dt, ECS::Scene& scene);

        // ------------------------------------------------------------------ //
        //  Command buffer (gameplay code posts here; never calls AudioManager directly)
        // ------------------------------------------------------------------ //

        // Post a command. Thread-safe — may be called from any thread.
        void PostCommand(const AudioCommand& cmd);

        // Convenience helpers that construct and post commands:
        uint32_t RequestPlay(uint32_t clip_handle, uint32_t entity_hint, bool loop, float volume);
        void     RequestStop(uint32_t voice_handle);
        void     RequestSetVolume(uint32_t voice_handle, float volume);
        void     RequestSetPitch(uint32_t voice_handle, float pitch);

        // ------------------------------------------------------------------ //
        //  Music streaming
        // ------------------------------------------------------------------ //

        // Begin streaming a music track from VFS. Returns INVALID_MUSIC_HANDLE on failure.
        // Streaming happens on a miniaudio-managed background thread.
        MusicHandle PlayMusic(
            const VFS::VFSPath& path,
            float               volume = 1.0f,
            bool                loop   = true
        );

        void StopMusic(MusicHandle handle);

        // Ramp the volume of a music track over duration_seconds.
        // Setting target_volume to 0 and calling StopMusic after the fade is complete
        // is the correct way to cross-fade.
        void FadeMusic(MusicHandle handle, float target_volume, float duration_seconds);

        // ------------------------------------------------------------------ //
        //  Master controls
        // ------------------------------------------------------------------ //

        void  SetMasterVolume(float volume);
        float GetMasterVolume() const;

        // ------------------------------------------------------------------ //
        //  Internal accessors for ECS systems
        // ------------------------------------------------------------------ //

        AudioManager& GetManager();

    private:
        void DrainCommandBuffer();
        void UpdateMusicFades(float dt);

        Core::Memory::ArenaAllocator*     m_Arena         = nullptr;
        ma_engine*                        m_Engine         = nullptr;
        AudioManager                      m_Manager;

        // Lock-free ring buffer for commands from gameplay code.
        // Commands are written from game thread; read during Flush() on main thread.
        // Max 1024 commands per frame; assert if overflow.
        static constexpr uint32_t CMD_BUFFER_CAPACITY = 1024u;
        AudioCommand              m_CommandBuffer[CMD_BUFFER_CAPACITY] = {};
        uint32_t                  m_CmdWriteIdx = 0;
        uint32_t                  m_CmdReadIdx  = 0;

        // Music track pool
        static constexpr uint32_t MAX_MUSIC_TRACKS = 8u;
        MusicTrack m_MusicTracks[MAX_MUSIC_TRACKS] = {};

        bool m_Initialized = false;
    };

    // Global accessor — set during engine boot
    AudioEngine* GetAudioEngine();
    void         SetAudioEngine(AudioEngine* engine);

}  // namespace ZEngine::Audio
```

### 5.1 `AudioManager::LoadClip` implementation notes

```cpp
uint32_t AudioManager::LoadClip(const VFS::VFSPath& path) {
    ZENGINE_VALIDATE_ASSERT(m_Initialized, "AudioManager::LoadClip — not initialized");

    // Check whether this path is already loaded (simple linear scan; pool is small)
    for (uint32_t i = 0; i < MAX_CLIPS; ++i) {
        if (m_Clips[i].Valid && m_Clips[i].Path == path.AsString()) {
            m_Clips[i].RefCount++;
            return i;
        }
    }

    // Find a free slot
    uint32_t slot = INVALID_CLIP_HANDLE;
    for (uint32_t i = 0; i < MAX_CLIPS; ++i) {
        if (!m_Clips[i].Valid) { slot = i; break; }
    }
    ZENGINE_VALIDATE_ASSERT(slot != INVALID_CLIP_HANDLE, "AudioManager::LoadClip — clip pool full");

    // Load raw bytes from VFS into a temporary buffer, then give to miniaudio
    // VFS returns a byte span; miniaudio decodes it via ma_sound_init_from_data_source
    // (exact VFS API depends on vfs-design.md Ticket 1 IVFSContext interface)
    //
    // Pattern:
    //   VFS::FileHandle fh = m_VFS->Open(path, VFS::OpenMode::Read);
    //   size_t size = m_VFS->GetSize(fh);
    //   void*  data = std::malloc(size);   // temp; freed after ma_sound_init
    //   m_VFS->Read(fh, data, size);
    //   m_VFS->Close(fh);
    //   ma_sound* snd = new ma_sound{};
    //   ma_result result = ma_sound_init_from_memory(m_Engine, data, size,
    //       MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, snd);
    //   std::free(data);

    // For this design document: assume above pattern.
    // MA_SOUND_FLAG_DECODE = decode to PCM at load time (lowest runtime latency for SFX)
    // MA_SOUND_FLAG_NO_SPATIALIZATION = cleared later by Play() when Is3D is true

    // m_Clips[slot] = ClipAsset{ .Path = path.AsString(), .Template = snd,
    //                             .RefCount = 1, .Valid = true };
    return slot;
}
```

---

## 6. ECS Systems

### 6.1 `AudioListenerSystem`

Reads `AudioListenerComponent` and `TransformComponent`. Calls `AudioManager::SetListenerTransform`. Only the first entity with `Primary == true` is processed; extras emit a `ZENGINE_VALIDATE_ASSERT` warning.

```cpp
// ZEngine/Audio/AudioSystems.h
#pragma once
#include <ECS/WorldTick.h>

namespace ZEngine::Audio {

    void AudioListenerSystem(ECS::WorldTickContext& ctx);
    void AudioSourceSystem(ECS::WorldTickContext& ctx);

    void RegisterAudioSystems(ECS::WorldTick& world);

}  // namespace ZEngine::Audio
```

```cpp
// ZEngine/Audio/AudioSystems.cpp

void AudioListenerSystem(ECS::WorldTickContext& ctx) {
    using namespace ECS::Components;
    AudioEngine* ae = GetAudioEngine();
    ZENGINE_VALIDATE_ASSERT(ae != nullptr, "AudioListenerSystem: AudioEngine is null");

    bool found_primary = false;

    ctx.Scene.ForEach<AudioListenerComponent, TransformComponent>(
        [&](ECS::EntityID /*id*/,
            const AudioListenerComponent& listener,
            const TransformComponent&     xf)
        {
            if (!listener.Primary) return;

            ZENGINE_VALIDATE_ASSERT(
                !found_primary,
                "AudioListenerSystem: multiple entities with Primary == true; only the first is used"
            );
            found_primary = true;

            // Reconstruct forward and up from Euler rotation
            Core::Maths::Quaternion<float> rot =
                Core::Maths::Quaternion<float>::FromEuler(xf.Rotation.X, xf.Rotation.Y, xf.Rotation.Z);

            Core::Maths::Vec3f forward = rot.RotateVector({ 0.0f, 0.0f, -1.0f });
            Core::Maths::Vec3f up      = rot.RotateVector({ 0.0f, 1.0f,  0.0f });

            ae->GetManager().SetListenerTransform(xf.Position, forward, up);
        }
    );
}
```

**`SystemDeps`:**
```cpp
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<AudioListenerComponent>())
               | MaskBit(ComponentTypeOf<TransformComponent>()),
    .WriteMask = 0,
}
```

### 6.2 `AudioSourceSystem`

Reads `AudioSourceComponent` and `TransformComponent`. For each source:
- If `IsPlaying == true` and `VoiceHandle == INVALID_VOICE_HANDLE`: start the voice via `RequestPlay`.
- If `IsPlaying == false` and `VoiceHandle != INVALID_VOICE_HANDLE`: stop the voice.
- If voice is playing and `Is3D == true`: update 3D position via `AudioManager::SetVoicePosition`.
- Check `PlayOnCreate` flag on first observation.

```cpp
void AudioSourceSystem(ECS::WorldTickContext& ctx) {
    using namespace ECS::Components;
    AudioEngine* ae = GetAudioEngine();
    ZENGINE_VALIDATE_ASSERT(ae != nullptr, "AudioSourceSystem: AudioEngine is null");

    ctx.Scene.ForEach<AudioSourceComponent, TransformComponent>(
        [&](ECS::EntityID           id,
            AudioSourceComponent&   src,
            const TransformComponent& xf)
        {
            if (src.ClipHandle == UINT32_MAX) return;

            // Auto-play on creation
            if (src.PlayOnCreate && src.VoiceHandle == UINT32_MAX && !src.IsPlaying) {
                src.IsPlaying    = true;
                src.PlayOnCreate = false;  // consume the flag
            }

            // Start voice
            if (src.IsPlaying && src.VoiceHandle == UINT32_MAX) {
                AudioCommand cmd{};
                cmd.Type       = AudioCommandType::Play;
                cmd.ClipHandle = src.ClipHandle;
                cmd.EntityHint = static_cast<uint32_t>(id);
                cmd.ParamA     = src.Volume;
                cmd.ParamBool  = src.Loop;
                ae->PostCommand(cmd);
                // VoiceHandle will be assigned during Flush(); mark as pending
                // by setting to a sentinel that is not INVALID_VOICE_HANDLE
                // (actual assignment requires Flush() to run first)
            }

            // Stop voice
            if (!src.IsPlaying && src.VoiceHandle != UINT32_MAX) {
                AudioCommand cmd{};
                cmd.Type        = AudioCommandType::Stop;
                cmd.VoiceHandle = src.VoiceHandle;
                ae->PostCommand(cmd);
                src.VoiceHandle = UINT32_MAX;
            }

            // Update 3D position for playing voices
            if (src.IsPlaying && src.Is3D && src.VoiceHandle != UINT32_MAX) {
                ae->GetManager().SetVoicePosition(
                    src.VoiceHandle,
                    xf.Position,
                    { 0.0f, 0.0f, 0.0f }  // velocity for doppler; extend with RigidBodyComponent read if needed
                );
            }
        }
    );
}
```

**`SystemDeps`:**
```cpp
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<AudioSourceComponent>())
               | MaskBit(ComponentTypeOf<TransformComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<AudioSourceComponent>()),
    // WriteMask because VoiceHandle and PlayOnCreate are modified
}
```

---

## 7. Audio Command Buffer

Gameplay code that wants to trigger audio effects does not call `AudioEngine` directly. It posts an `AudioCommand` to the command buffer. This decouples simulation logic from audio state and ensures audio changes are applied atomically after all ECS systems complete.

```
Frame N:
  ┌─────────────────────────────────────────────────────────────┐
  │  WorldTick                                                  │
  │   Wave 0: PhysicsSyncTransformToBodySystem                  │
  │   Wave 1: PhysicsStepSystem                                 │
  │   Wave 2: PhysicsSyncBodyToTransformSystem                  │
  │           CharacterControllerSystem                         │
  │           AudioListenerSystem       ← posts no commands     │
  │           AudioSourceSystem         ← posts Play/Stop cmds  │
  │   (gameplay Actor::OnTick)          ← posts Play/Stop cmds  │
  └──────────────────────┬──────────────────────────────────────┘
                         │ after WorldTick completes
                         ▼
              AudioEngine::Flush(dt, scene)
                DrainCommandBuffer(scene)
                UpdateMusicFades(dt)
```

### 7.1 Command buffer implementation

The command buffer is a fixed-capacity ring buffer. `PostCommand` is called from game thread; `DrainCommandBuffer` is called from main thread after `WorldTick`. Since both happen on the same thread in ZEngine's single-threaded tick model, no locks are needed. If multi-threaded game code is ever introduced, wrap with a spinlock.

```cpp
void AudioEngine::PostCommand(const AudioCommand& cmd) {
    uint32_t next = (m_CmdWriteIdx + 1) % CMD_BUFFER_CAPACITY;
    ZENGINE_VALIDATE_ASSERT(
        next != m_CmdReadIdx,
        "AudioEngine::PostCommand — command buffer overflow; increase CMD_BUFFER_CAPACITY"
    );
    m_CommandBuffer[m_CmdWriteIdx] = cmd;
    m_CmdWriteIdx = next;
}

void AudioEngine::DrainCommandBuffer() {
    while (m_CmdReadIdx != m_CmdWriteIdx) {
        const AudioCommand& cmd = m_CommandBuffer[m_CmdReadIdx];
        m_CmdReadIdx = (m_CmdReadIdx + 1) % CMD_BUFFER_CAPACITY;

        switch (cmd.Type) {
            case AudioCommandType::Play: {
                uint32_t voice = m_Manager.Play(cmd.ClipHandle, cmd.EntityHint,
                                                 cmd.ParamBool /*loop*/, cmd.ParamA /*volume*/);
                // Back-fill: write VoiceHandle back into the entity's AudioSourceComponent.
                // This is safe: Flush() runs on the main thread after WorldTick completes.
                if (cmd.EntityHint != UINT32_MAX && voice != INVALID_VOICE_HANDLE && scene) {
                    auto* src = scene->TryGetComponent<AudioSourceComponent>(
                        EntityID{ cmd.EntityHint, cmd.EntityGen });
                    if (src) src->VoiceHandle = voice;
                }
                break;
            }
            case AudioCommandType::Stop:
                m_Manager.Stop(cmd.VoiceHandle);
                break;
            case AudioCommandType::SetVolume:
                m_Manager.SetVoiceVolume(cmd.VoiceHandle, cmd.ParamA);
                break;
            case AudioCommandType::SetPitch:
                m_Manager.SetVoicePitch(cmd.VoiceHandle, cmd.ParamA);
                break;
            case AudioCommandType::SetLoop:
                m_Manager.SetVoiceLoop(cmd.VoiceHandle, cmd.ParamBool);
                break;
            default:
                ZENGINE_VALIDATE_ASSERT(false, "AudioEngine::DrainCommandBuffer — unknown command type");
                break;
        }
    }
}
```

### 7.2 VoiceHandle back-fill

When `AudioSourceSystem` posts a `Play` command, it cannot immediately store the returned `VoiceHandle` because `AudioManager::Play` runs in `Flush`, not during the ECS tick. Two options:

**Option A (recommended):** Store `EntityHint` in the command. In `DrainCommandBuffer`, after `m_Manager.Play` returns a voice handle, look up the entity in the scene and write back `VoiceHandle` to `AudioSourceComponent`:
```cpp
// In DrainCommandBuffer, Play case:
if (cmd.EntityHint != UINT32_MAX && voice != INVALID_VOICE_HANDLE) {
    auto* src = ctx.Scene.TryGetComponent<AudioSourceComponent>(cmd.EntityHint);
    if (src) src->VoiceHandle = voice;
}
```
This requires passing `ECS::Scene&` to `Flush`.

**Option B:** Use a deferred response queue — `AudioSourceSystem` polls `AudioEngine::GetPendingVoiceAssignment(EntityID)` at the start of the next frame.

Option A is simpler and correct for single-threaded ticks.

---

## 8. Streaming Music

Music tracks are different from SFX in three ways:
1. They are too large to decode entirely into memory — they stream from disk.
2. They have no 3D position — they are always 2D (panned to center).
3. They support volume fading without posting commands (the fade is managed by `AudioEngine::Flush`).

```cpp
MusicHandle AudioEngine::PlayMusic(
    const VFS::VFSPath& path,
    float               volume,
    bool                loop)
{
    ZENGINE_VALIDATE_ASSERT(m_Initialized, "AudioEngine::PlayMusic — not initialized");

    // Find a free music track slot
    MusicHandle handle = INVALID_MUSIC_HANDLE;
    for (uint32_t i = 0; i < MAX_MUSIC_TRACKS; ++i) {
        if (!m_MusicTracks[i].InUse) { handle = i; break; }
    }
    ZENGINE_VALIDATE_ASSERT(handle != INVALID_MUSIC_HANDLE,
        "AudioEngine::PlayMusic — music track pool full (max 8 simultaneous tracks)");

    // Load via VFS into a temporary file path that miniaudio can stream from.
    // If VFS is an OS-backed mount, pass the real path to ma_sound_init_from_file.
    // If VFS is a PAK-backed mount, extract to a temp file or use a custom data source.
    // Music track storage is arena-allocated at PlayMusic time.
    // The arena is the AudioEngine's dedicated audio arena (never cleared mid-session).
    ma_sound* snd = ZPushStructCtor(m_Arena, ma_sound);
    ma_result result = ma_sound_init_from_file(
        m_Engine,
        path.AsNativePath(),                   // resolved OS path from VFS
        MA_SOUND_FLAG_STREAM                   // do NOT decode up-front
        | MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr,
        nullptr,
        snd
    );
    ZENGINE_VALIDATE_ASSERT(result == MA_SUCCESS, "AudioEngine::PlayMusic — ma_sound_init_from_file failed");

    ma_sound_set_volume(snd, volume);
    ma_sound_set_looping(snd, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(snd);

    m_MusicTracks[handle] = MusicTrack{
        .Sound         = snd,
        .TargetVolume  = volume,
        .FadeRemaining = 0.0f,
        .FadeDuration  = 0.0f,
        .InUse         = true,
    };

    return handle;
}

void AudioEngine::StopMusic(MusicHandle handle) {
    ZENGINE_VALIDATE_ASSERT(handle < MAX_MUSIC_TRACKS, "AudioEngine::StopMusic — invalid handle");
    MusicTrack& track = m_MusicTracks[handle];
    if (!track.InUse) return;

    ma_sound_stop(track.Sound);
    // ma_sound cleanup — uninit before the arena reclaims memory at shutdown.
    ma_sound_uninit(track.Sound);
    track.Sound = nullptr;
    // Arena memory is reclaimed in bulk at AudioEngine::Shutdown.
    track = MusicTrack{};
}

void AudioEngine::FadeMusic(MusicHandle handle, float target_volume, float duration_seconds) {
    ZENGINE_VALIDATE_ASSERT(handle < MAX_MUSIC_TRACKS, "AudioEngine::FadeMusic — invalid handle");
    ZENGINE_VALIDATE_ASSERT(duration_seconds > 0.0f, "AudioEngine::FadeMusic — duration must be positive");

    MusicTrack& track = m_MusicTracks[handle];
    if (!track.InUse) return;

    track.TargetVolume  = target_volume;
    track.FadeDuration  = duration_seconds;
    track.FadeRemaining = duration_seconds;
}

void AudioEngine::UpdateMusicFades(float dt) {
    for (uint32_t i = 0; i < MAX_MUSIC_TRACKS; ++i) {
        MusicTrack& track = m_MusicTracks[i];
        if (!track.InUse || track.FadeRemaining <= 0.0f) continue;

        track.FadeRemaining -= dt;
        if (track.FadeRemaining <= 0.0f) {
            track.FadeRemaining = 0.0f;
            ma_sound_set_volume(track.Sound, track.TargetVolume);
        } else {
            float t       = 1.0f - (track.FadeRemaining / track.FadeDuration);
            float current = ma_sound_get_volume(track.Sound);
            float interp  = current + t * (track.TargetVolume - current);
            ma_sound_set_volume(track.Sound, interp);
        }
    }
}
```

**Cross-fading pattern (gameplay code):**
```cpp
// Fade out current track over 2 seconds, then start next track
ae->FadeMusic(current_music, 0.0f, 2.0f);
// After 2 seconds (checked via a timer Actor):
ae->StopMusic(current_music);
current_music = ae->PlayMusic("music/level2_theme.ogg", 0.0f, true);
ae->FadeMusic(current_music, 1.0f, 2.0f);
```

---

## 9. Supported Formats

| Format    | Extension | Decode mode  | Recommended use |
|-----------|-----------|--------------|-----------------|
| OGG Vorbis | `.ogg`   | In-memory (SFX) or Streaming (music) | Music tracks, long ambient loops, voice lines |
| WAV (PCM)  | `.wav`   | In-memory always | Short SFX (footsteps, gunshots, UI clicks) |
| MP3        | `.mp3`   | Streaming only (avoid in-memory for licensing hygiene) | Legacy assets only; prefer OGG |
| FLAC       | `.flac`  | In-memory (small) or Streaming (large) | Editor previews; not recommended in shipping builds |

Format detection:
```cpp
// ZEngine/Audio/AudioTypes.cpp
AudioFormat DetectFormat(const char* path) {
    const char* ext = std::strrchr(path, '.');
    if (!ext) return AudioFormat::Unknown;
    ext++;  // skip the dot

    if (::strcasecmp(ext, "ogg")  == 0) return AudioFormat::OGG;
    if (::strcasecmp(ext, "wav")  == 0) return AudioFormat::WAV;
    if (::strcasecmp(ext, "mp3")  == 0) return AudioFormat::MP3;
    if (::strcasecmp(ext, "flac") == 0) return AudioFormat::FLAC;
    return AudioFormat::Unknown;
}
```

miniaudio detects format automatically from the file header (magic bytes), so `DetectFormat` is used only to validate inputs and select decode strategy (in-memory vs. streaming), not to override miniaudio's decoder.

---

## 10. Platform Notes

miniaudio selects its backend at compile time based on the target platform:

| Platform | Backend | Notes |
|----------|---------|-------|
| macOS    | CoreAudio | Default; hardware-accelerated. `ma_backend_coreaudio`. |
| iOS      | CoreAudio | Same API; AVAudioSession managed by miniaudio. |
| Windows  | WASAPI  | Default (Windows Vista+). Fallback to DirectSound for Win XP (not supported by ZEngine). |
| Linux    | PulseAudio | Default. Falls back to ALSA if PulseAudio unavailable. JACK supported via `ma_backend_jack`. |
| WASM     | Web Audio API | Requires Emscripten. `ma_backend_webaudio`. |

No extra libraries are required on any platform. miniaudio includes all backend glue code in the single header.

**macOS-specific:** CoreAudio requires the `CoreAudio.framework` and `AudioToolbox.framework`. Add to `CMakeLists.txt`:
```cmake
if(APPLE)
    target_link_libraries(ZEngineAudio PRIVATE
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
endif()
```

**Linux-specific:** PulseAudio headers (`libpulse-dev`) must be available at compile time. ALSA headers (`libasound2-dev`) for the fallback. Add a CMake find_package or pkg_check_modules call.

---

## 11. Scheduler Registration

```cpp
void Audio::RegisterAudioSystems(ECS::WorldTick& world) {
    SystemID listener_sys = world.RegisterSystem(
        AudioListenerSystem, {
            .ReadMask  = MaskBit(ComponentTypeOf<AudioListenerComponent>())
                       | MaskBit(ComponentTypeOf<TransformComponent>()),
            .WriteMask = 0,
        }
    );

    SystemID source_sys = world.RegisterSystem(
        AudioSourceSystem, {
            .ReadMask  = MaskBit(ComponentTypeOf<AudioSourceComponent>())
                       | MaskBit(ComponentTypeOf<TransformComponent>()),
            .WriteMask = MaskBit(ComponentTypeOf<AudioSourceComponent>()),
        }
    );

    // AudioListenerSystem and AudioSourceSystem are independent — no ordering needed
    // between them. However, both must run after physics writes TransformComponent.
    SystemID physics_step = world.GetSystemID("PhysicsStepSystem");
    SystemID phys_sync    = world.GetSystemID("PhysicsSyncBodyToTransformSystem");
    SystemID char_ctrl    = world.GetSystemID("CharacterControllerSystem");

    world.OrderBefore(phys_sync, listener_sys);
    world.OrderBefore(phys_sync, source_sys);
    world.OrderBefore(char_ctrl, listener_sys);
    world.OrderBefore(char_ctrl, source_sys);
}
```

**Wave placement:**

```
Wave 0: PhysicsSyncTransformToBodySystem
Wave 1: PhysicsStepSystem
Wave 2: PhysicsSyncBodyToTransformSystem, CharacterControllerSystem
Wave 3: AudioListenerSystem, AudioSourceSystem,
        RenderCullSystem, AnimationSampleSystem   ← all depend on final transforms
```

`AudioEngine::Flush(dt, scene)` is called by the engine frame loop after `WorldTick::Tick()` returns, outside the scheduler. The `scene` parameter is required so `DrainCommandBuffer` can write back `VoiceHandle` values to `AudioSourceComponent` (VoiceHandle back-fill, §7.2). It is not an ECS system — it runs on the main thread after the scheduler completes.

---

## 12. File Layout

```
ZEngine/Audio/
├── CMakeLists.txt
├── AudioTypes.h               — AudioFormat, MusicHandle, AudioCommand, AudioCommandType
├── AudioTypes.cpp             — DetectFormat implementation
├── AudioManager.h             — AudioManager class declaration (no miniaudio includes)
├── AudioManager.cpp           — implementation (includes miniaudio.h via AudioEngine.cpp)
├── AudioEngine.h              — AudioEngine class declaration
├── AudioEngine.cpp            — implementation + MINIAUDIO_IMPLEMENTATION define
├── AudioSystems.h             — AudioListenerSystem, AudioSourceSystem declarations
└── AudioSystems.cpp           — system implementations

ZEngine/ECS/Components/
├── AudioSourceComponent.h     — new
└── AudioListenerComponent.h   — new

vendor/
└── miniaudio/
    └── miniaudio.h            — single-header; no subdirectory needed
```

**IMPORTANT:** `#define MINIAUDIO_IMPLEMENTATION` must appear exactly once in the codebase, in `AudioEngine.cpp`, before `#include "miniaudio.h"`. All other `.cpp` files in `ZEngine/Audio/` include only `AudioManager.h` / `AudioEngine.h`.

```cpp
// ZEngine/Audio/AudioEngine.cpp  — top of file
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>
// ... rest of implementation
```

---

## 13. CMakeLists.txt Changes

```cmake
# ZEngine/CMakeLists.txt  (additions)

# ── Audio module ─────────────────────────────────────────────────────────────
add_library(ZEngineAudio STATIC
    Audio/AudioTypes.cpp
    Audio/AudioManager.cpp
    Audio/AudioEngine.cpp
    Audio/AudioSystems.cpp
)

target_include_directories(ZEngineAudio PRIVATE
    ${ZENGINE_VENDOR_DIR}/miniaudio   # gives <miniaudio/miniaudio.h>
)

target_link_libraries(ZEngineAudio
    PUBLIC  ZEngineCore
)

target_compile_features(ZEngineAudio PUBLIC cxx_std_20)

# Platform audio framework links
if(APPLE)
    target_link_libraries(ZEngineAudio PRIVATE
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSE libpulse)
    if(PULSE_FOUND)
        target_include_directories(ZEngineAudio PRIVATE ${PULSE_INCLUDE_DIRS})
        target_link_libraries(ZEngineAudio PRIVATE ${PULSE_LIBRARIES})
    endif()
    # ALSA fallback
    find_package(ALSA)
    if(ALSA_FOUND)
        target_link_libraries(ZEngineAudio PRIVATE ALSA::ALSA)
    endif()
endif()

if(WIN32)
    target_link_libraries(ZEngineAudio PRIVATE ole32 winmm)
endif()

# Main engine links audio
target_link_libraries(ZEngineRuntime PUBLIC ZEngineAudio)
```

**Vendoring miniaudio:**
```
# No submodule needed — single header. Copy manually:
mkdir -p vendor/miniaudio
curl -L https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h \
     -o vendor/miniaudio/miniaudio.h
```
Pin the commit hash in a `vendor/miniaudio/VERSION` file for reproducibility.

---

## 14. Deliverables Checklist

### New files
- [ ] `ZEngine/Audio/AudioTypes.h`
- [ ] `ZEngine/Audio/AudioTypes.cpp`
- [ ] `ZEngine/Audio/AudioManager.h`
- [ ] `ZEngine/Audio/AudioManager.cpp`
- [ ] `ZEngine/Audio/AudioEngine.h`
- [ ] `ZEngine/Audio/AudioEngine.cpp`
- [ ] `ZEngine/Audio/AudioSystems.h`
- [ ] `ZEngine/Audio/AudioSystems.cpp`
- [ ] `ZEngine/Audio/CMakeLists.txt`
- [ ] `ZEngine/ECS/Components/AudioSourceComponent.h`
- [ ] `ZEngine/ECS/Components/AudioListenerComponent.h`
- [ ] `vendor/miniaudio/miniaudio.h`
- [ ] `vendor/miniaudio/VERSION`

### Modified files
- [ ] `ZEngine/CMakeLists.txt` — add ZEngineAudio target, platform libs
- [ ] `ZEngine/ECS/Components/ComponentRegistry.h` — register `AudioSourceComponent`, `AudioListenerComponent`
- [ ] `ZEngine/Engine/EngineStartup.cpp` — create `AudioEngine`, call `RegisterAudioSystems(world)`, call `AudioEngine::Flush` after `WorldTick::Tick`

### Tests
- [ ] `Tests/Audio/AudioEngineInitTest.cpp` — Initialize/Shutdown with null VFS stub; verify no crash
- [ ] `Tests/Audio/LoadClipTest.cpp` — LoadClip with a known WAV file; IsClipLoaded returns true; UnloadClip; IsClipLoaded returns false
- [ ] `Tests/Audio/PlayStopVoiceTest.cpp` — Play returns valid handle; IsVoicePlaying == true; Stop; IsVoicePlaying == false
- [ ] `Tests/Audio/CommandBufferTest.cpp` — post CMD_BUFFER_CAPACITY - 1 commands without overflow; assert on overflow
- [ ] `Tests/Audio/MusicFadeTest.cpp` — FadeMusic changes volume monotonically over dt steps; StopMusic frees slot
- [ ] `Tests/Audio/AudioListenerSystemTest.cpp` — scene with one AudioListenerComponent + TransformComponent; SetListenerTransform called with correct position
- [ ] `Tests/Audio/AudioSourceSystemTest.cpp` — IsPlaying flip to true posts Play command; flip to false posts Stop command; VoiceHandle back-filled after Flush
- [ ] `Tests/Audio/FormatDetectionTest.cpp` — DetectFormat(".ogg") == OGG; ".wav" == WAV; ".mp3" == MP3; unknown == Unknown
- [ ] `Tests/ECS/AudioSourceComponentTest.cpp` — default values, struct layout
- [ ] `Tests/ECS/AudioListenerComponentTest.cpp` — default values
