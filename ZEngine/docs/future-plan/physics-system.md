# ZEngine — Physics System

**Priority:** P1 — Required before any game with movement or collision can ship
**Status:** Design
**Depends on:** `actor-ecs-architecture.md`, `system-scheduler.md`
**Blocks:** Character movement, collision response, trigger volumes, raycasting
**Recommended library:** Jolt Physics (MIT license, C++17, https://github.com/jrouwe/JoltPhysics)

---

## 1. Why Jolt Physics

Jolt Physics is the correct choice for ZEngine for the following reasons:

- **License:** MIT. No royalties, no attribution requirements in builds, no GPL contamination.
- **Language standard:** Written in C++17, which is fully compatible with ZEngine's C++20 baseline.
- **Determinism:** Jolt provides deterministic simulation (same inputs → same outputs across runs and platforms when using fixed-point time steps), which is a prerequisite for game replay systems and networked physics.
- **Allocator injection:** Jolt exposes `JPH::Allocate`, `JPH::Free`, `JPH::AlignedAllocate`, and `JPH::AlignedFree` as global function pointers that can be replaced before `JPH::RegisterDefaultAllocator()` is called. This lets ZEngine route all Jolt internal allocations through a tracked system allocator without patching Jolt source.
- **Thread model:** Jolt's `JobSystem` interface maps cleanly onto ZEngine's `ThreadPoolHelper`. The physics step can dispatch collision detection jobs in parallel without ZEngine managing Jolt internals.
- **Industry adoption:** Used in Horizon Forbidden West (Guerrilla), and increasingly in indie and mid-tier studios. The codebase is actively maintained and battle-hardened.
- **No virtual dispatch in hot path:** Jolt's contact callbacks use a listener interface that is called outside the simulation inner loop, which aligns with ZEngine's no-virtual-in-hot-path policy.
- **Self-contained:** Jolt has zero runtime dependencies. It vendored cleanly as a single CMake subdirectory.

---

## 2. Integration Strategy

Jolt sits entirely below the ECS layer. ZEngine owns a `PhysicsWorld` singleton that wraps the Jolt `PhysicsSystem`. ECS components are thin handles — they store a `JPH::BodyID` (a 32-bit opaque integer) and scalar parameters. No Jolt type ever appears in an ECS component header.

```
┌─────────────────────────────────────────────────────────────┐
│                        ECS::Scene                           │
│  RigidBodyComponent    ColliderComponent    TransformComponent│
│  (JPH::BodyID handle)  (shape parameters)  (position/rot)   │
└──────────────────────────┬──────────────────────────────────┘
                           │  EntityID ↔ BodyID mapping
              ┌────────────▼────────────────────────┐
              │          PhysicsWorld                │
              │  ZEngine::Physics::PhysicsWorld      │
              │  owns JPH::PhysicsSystem             │
              │  owns JPH::BodyInterface             │
              │  owns EntityID ↔ BodyID map          │
              └────────────┬────────────────────────┘
                           │  JPH API calls
              ┌────────────▼────────────────────────┐
              │          Jolt Physics                │
              │  JPH::PhysicsSystem                  │
              │  JPH::BroadPhaseLayerInterface       │
              │  JPH::ObjectLayerPairFilter           │
              │  JPH::ContactListener                │
              └─────────────────────────────────────┘
```

**Key invariants:**
1. `#include <Jolt/Jolt.h>` never appears in any ECS component header or Actor header.
2. All Jolt includes are confined to `ZEngine/Physics/`.
3. `PhysicsWorld` is created and destroyed by the engine boot sequence, not by gameplay code.
4. The `EntityID ↔ BodyID` mapping is owned by `PhysicsWorld` and is the authoritative source of truth.

---

## 3. ECS Components

All components are plain data structs. No virtual methods. No behavior. Lives in `ZEngine::ECS::Components`. Headers are in `ZEngine/ECS/Components/`.

### 3.1 `RigidBodyComponent`

```cpp
// ZEngine/ECS/Components/RigidBodyComponent.h
#pragma once
#include <cstdint>

namespace ZEngine::ECS::Components {

    enum class RigidBodyType : uint8_t {
        Static    = 0,  // never moves, contributes to collision but never integrates
        Kinematic = 1,  // moved by game code via velocity; not affected by forces
        Dynamic   = 2,  // fully simulated; responds to gravity, forces, impulses
    };

    // Thin handle component. Stores the Jolt body ID as an opaque 32-bit integer
    // so that no Jolt header needs to be included here.
    struct RigidBodyComponent {
        uint32_t      JoltBodyID      = UINT32_MAX;  // JPH::BodyID::GetIndexAndSequenceNumber()
        RigidBodyType Type            = RigidBodyType::Dynamic;
        float         Mass            = 1.0f;         // kg; ignored for Static/Kinematic
        float         Restitution     = 0.3f;         // [0, 1] — bounciness coefficient
        float         Friction        = 0.6f;         // [0, 1] — Coulomb friction
        float         LinearDamping   = 0.05f;        // drag applied to linear velocity per second
        float         AngularDamping  = 0.05f;        // drag applied to angular velocity per second
        bool          IsSensor        = false;        // if true: detects overlaps, no collision response
        bool          GravityEnabled  = true;         // if false: no gravity applied to this body
    };

}  // namespace ZEngine::ECS::Components
```

### 3.2 `ColliderComponent`

```cpp
// ZEngine/ECS/Components/ColliderComponent.h
#pragma once
#include <Core/Maths/Vector.h>
#include <cstdint>

namespace ZEngine::ECS::Components {

    enum class ColliderShape : uint8_t {
        Box          = 0,   // axis-aligned box; defined by HalfExtents
        Sphere       = 1,   // sphere; defined by Radius
        Capsule      = 2,   // upright capsule; defined by Radius + HalfHeight
        ConvexHull   = 3,   // arbitrary convex shape; data in ShapeDataHandle
        TriangleMesh = 4,   // static concave mesh; only valid on Static bodies
    };

    struct ColliderComponent {
        ColliderShape Shape          = ColliderShape::Box;

        // Box / shared half-extents for non-uniform shapes
        Core::Maths::Vec3f HalfExtents = { 0.5f, 0.5f, 0.5f };

        // Sphere / Capsule
        float Radius     = 0.5f;
        float HalfHeight = 0.5f;   // capsule half-height (excluding hemispheres)

        // ConvexHull / TriangleMesh: handle into PhysicsWorld::ShapeCache
        // Built from mesh data by the cook pipeline; zero means uninitialized
        uint32_t ShapeDataHandle = 0;

        // Local offset of the collider center from the entity's transform origin
        Core::Maths::Vec3f LocalOffset = { 0.0f, 0.0f, 0.0f };
    };

}  // namespace ZEngine::ECS::Components
```

### 3.3 `CharacterControllerComponent`

```cpp
// ZEngine/ECS/Components/CharacterControllerComponent.h
#pragma once
#include <Core/Maths/Vector.h>

namespace ZEngine::ECS::Components {

    // Kinematic character controller. Uses Jolt's JPH::CharacterVirtual internally.
    // The entity must also have a RigidBodyComponent with Type = Kinematic
    // and a ColliderComponent with Shape = Capsule.
    struct CharacterControllerComponent {
        float CapsuleRadius    = 0.35f;   // metres
        float CapsuleHeight    = 1.8f;    // metres (total height, hemispheres included)
        float MaxSlopeAngle    = 45.0f;   // degrees; steeper slopes are treated as walls
        float StepHeight       = 0.3f;    // metres; max height of obstacle to auto-step over
        float MaxSpeedGround   = 6.0f;    // m/s horizontal
        float MaxSpeedAir      = 3.0f;    // m/s horizontal while airborne
        float JumpImpulse      = 5.0f;    // m/s vertical impulse applied on jump

        // Written each frame by gameplay code; read by CharacterControllerSystem
        Core::Maths::Vec3f DesiredVelocity = { 0.0f, 0.0f, 0.0f };
        bool               WantsJump       = false;

        // Set by CharacterControllerSystem after integration; read by gameplay code
        Core::Maths::Vec3f Velocity       = { 0.0f, 0.0f, 0.0f };
        bool               IsGrounded     = false;
    };

}  // namespace ZEngine::ECS::Components
```

---

## 4. `PhysicsWorld`

`PhysicsWorld` owns the Jolt `PhysicsSystem` and all associated Jolt-side state. It is the only translation unit that includes `<Jolt/Jolt.h>`.

### 4.1 Supporting types (Physics-internal, not ECS-visible)

```cpp
// ZEngine/Physics/PhysicsTypes.h
#pragma once
#include <Core/Maths/Vector.h>
#include <ECS/EntityID.h>
#include <cstdint>

namespace ZEngine::Physics {

    // Opaque body identifier returned to callers outside Physics/
    using BodyID = uint32_t;
    static constexpr BodyID INVALID_BODY_ID = UINT32_MAX;

    struct RaycastResult {
        ECS::EntityID      HitEntity  = ECS::INVALID_ENTITY;
        Core::Maths::Vec3f HitPoint   = {};
        Core::Maths::Vec3f HitNormal  = {};
        float              Distance   = 0.0f;
        bool               Hit        = false;
    };

    struct ShapeCastResult {
        ECS::EntityID      HitEntity      = ECS::INVALID_ENTITY;
        Core::Maths::Vec3f HitPoint       = {};
        Core::Maths::Vec3f HitNormal      = {};
        float              PenetrationDepth = 0.0f;
        bool               Hit            = false;
    };

    struct ContactEvent {
        ECS::EntityID EntityA;
        ECS::EntityID EntityB;
        bool          IsEnter;  // true = OnContactAdded, false = OnContactRemoved
    };

}  // namespace ZEngine::Physics
```

### 4.2 Collision layer definitions

```cpp
// ZEngine/Physics/CollisionLayers.h
#pragma once
#include <cstdint>

namespace ZEngine::Physics {

    // Object layers (assigned to each body)
    namespace ObjectLayer {
        static constexpr uint16_t NonMoving  = 0;  // static world geometry
        static constexpr uint16_t Moving     = 1;  // dynamic / kinematic bodies
        static constexpr uint16_t Player     = 2;  // player character
        static constexpr uint16_t Sensor     = 3;  // trigger volumes (no collision response)
        static constexpr uint16_t Projectile = 4;  // high-speed small objects
        static constexpr uint16_t Debris     = 5;  // low-priority debris (may skip collision vs debris)
        static constexpr uint16_t Count      = 6;
    }

    // Broad-phase layers (coarse bucket; reduces broad-phase pairs)
    namespace BroadPhaseLayer {
        static constexpr uint8_t NonMoving = 0;
        static constexpr uint8_t Moving    = 1;
        static constexpr uint8_t Count     = 2;
    }

    // Collision filter table (true = these layers collide with each other)
    //
    //                  NonMoving  Moving  Player  Sensor  Projectile  Debris
    // NonMoving           -        yes     yes     no       yes        yes
    // Moving              yes      yes     yes     no       yes        yes
    // Player              yes      yes     -       no       no         no
    // Sensor              no       yes     yes     no       no         no
    // Projectile          yes      yes     no      no       no         no
    // Debris              yes      yes     no      no       no         no
    //
    // Sensors overlap Moving and Player but produce contact events, not responses.

}  // namespace ZEngine::Physics
```

### 4.3 Jolt allocator adapter

```cpp
// ZEngine/Physics/JoltAllocatorAdapter.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <cstdlib>

namespace ZEngine::Physics {

    // Routes Jolt's internal memory requests through a dedicated system heap allocator.
    // Called once before JPH::Factory::sInstance is set.
    //
    // Note: Jolt's allocations are not arena-scoped; they live for the lifetime of the
    // PhysicsSystem. We use a general-purpose heap adapter here rather than an arena,
    // because Jolt frees individual allocations rather than frame-scoped bulk frees.
    // The arena is still used for per-frame ZEngine-side bookkeeping (contact event lists,
    // raycast result staging, etc.).
    void InstallJoltAllocatorHooks();

}  // namespace ZEngine::Physics
```

```cpp
// ZEngine/Physics/JoltAllocatorAdapter.cpp
#include "JoltAllocatorAdapter.h"
#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>
#include <cstdlib>
#include <cstring>

namespace ZEngine::Physics {

    static void* JoltAlloc(size_t size) {
        return std::malloc(size);
    }

    static void JoltFree(void* ptr) {
        std::free(ptr);
    }

    static void* JoltAlignedAlloc(size_t size, size_t alignment) {
#if defined(_MSC_VER)
        return _aligned_malloc(size, alignment);
#else
        void* ptr = nullptr;
        ::posix_memalign(&ptr, alignment, size);
        return ptr;
#endif
    }

    static void JoltAlignedFree(void* ptr) {
#if defined(_MSC_VER)
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }

    void InstallJoltAllocatorHooks() {
        JPH::Allocate        = JoltAlloc;
        JPH::Free            = JoltFree;
        JPH::AlignedAllocate = JoltAlignedAlloc;
        JPH::AlignedFree     = JoltAlignedFree;
    }

}  // namespace ZEngine::Physics
```

### 4.4 `PhysicsWorld` class declaration

```cpp
// ZEngine/Physics/PhysicsWorld.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Maths/Vector.h>
#include <Core/Maths/Quaternion.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <ECS/EntityID.h>
#include <ECS/Components/RigidBodyComponent.h>
#include <ECS/Components/ColliderComponent.h>
#include <Physics/PhysicsTypes.h>
#include <Physics/CollisionLayers.h>
#include <cstdint>
#include <functional>

// Forward-declare Jolt types so that this header stays Jolt-free.
namespace JPH {
    class PhysicsSystem;
    class JobSystemThreadPool;
    class TempAllocatorImpl;
    class BroadPhaseLayerInterfaceTable;
    class ObjectVsBroadPhaseLayerFilterTable;
    class ObjectLayerPairFilterTable;
    class ContactListener;
    class BodyID;
}

namespace ZEngine::Physics {

    // Callback for contact (trigger/sensor) events fired after each sim step.
    // Plain function pointer + user context — no std::function, no heap allocation.
    // DOD rationale: contact events fire per-collision-pair, potentially hundreds
    // of times per frame. std::function carries heap allocation and indirect-call
    // overhead that is unacceptable in a simulation hot path.
    using ContactEventFn = void (*)(void* ctx, const ContactEvent& event);

    struct ContactEventCallback {
        ContactEventFn Fn  = nullptr;
        void*          Ctx = nullptr;   // non-owning; caller manages lifetime
    };

    class PhysicsWorld {
    public:
        PhysicsWorld() = default;
        ~PhysicsWorld();

        // Non-copyable, non-movable — singleton ownership
        PhysicsWorld(const PhysicsWorld&)            = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        // ------------------------------------------------------------------ //
        //  Lifecycle
        // ------------------------------------------------------------------ //

        // Initialize Jolt and allocate body storage.
        //   arena               — ZEngine arena for per-frame bookkeeping
        //   max_bodies          — upper bound on simultaneous bodies (e.g. 65536)
        //   num_body_mutexes    — for parallel body locking (e.g. 0 = auto)
        //   max_body_pairs      — broad-phase pair cache size (e.g. 65536)
        //   max_contact_constraints — contact constraint pool size (e.g. 10240)
        void Initialize(
            Core::Memory::ArenaAllocator* arena,
            uint32_t max_bodies              = 65536u,
            uint32_t num_body_mutexes        = 0u,
            uint32_t max_body_pairs          = 65536u,
            uint32_t max_contact_constraints = 10240u
        );

        void Shutdown();

        // ------------------------------------------------------------------ //
        //  Simulation step
        // ------------------------------------------------------------------ //

        // Advance the simulation by dt seconds using collision_steps sub-steps.
        // collision_steps = 1 is sufficient for 60Hz; use 2 for fast objects at 30Hz.
        void Update(float dt, int collision_steps = 1);

        // ------------------------------------------------------------------ //
        //  Body management
        // ------------------------------------------------------------------ //

        // Create a Jolt body from ECS component data and register it.
        // Returns the opaque BodyID stored in RigidBodyComponent::JoltBodyID.
        [[nodiscard]] BodyID CreateBody(
            ECS::EntityID                       entity,
            const ECS::Components::RigidBodyComponent& rb,
            const ECS::Components::ColliderComponent&  col,
            const Core::Maths::Vec3f&           position,
            const Core::Maths::Quaternion<float>& rotation
        );

        // Remove a body from simulation and free its Jolt resources.
        void DestroyBody(BodyID id);

        // Sync a Kinematic body's position/rotation from ECS to Jolt.
        // Must be called before Update() for kinematic bodies each frame.
        void SetBodyPositionAndRotation(
            BodyID                               id,
            const Core::Maths::Vec3f&            position,
            const Core::Maths::Quaternion<float>& rotation
        );

        // Read back a dynamic body's position/rotation from Jolt to ECS.
        void GetBodyPositionAndRotation(
            BodyID                          id,
            Core::Maths::Vec3f&             out_position,
            Core::Maths::Quaternion<float>& out_rotation
        ) const;

        // Apply a world-space impulse to a dynamic body (gameplay use; thread-safe via lock).
        void ApplyImpulse(BodyID id, const Core::Maths::Vec3f& impulse);

        // Apply a world-space force (accumulated, cleared after each Update).
        void ApplyForce(BodyID id, const Core::Maths::Vec3f& force);

        // Set the linear velocity of a Kinematic body directly.
        void SetKinematicVelocity(BodyID id, const Core::Maths::Vec3f& velocity);

        // ------------------------------------------------------------------ //
        //  Queries
        // ------------------------------------------------------------------ //

        // Cast a ray from origin along direction up to max_dist metres.
        // Returns the closest hit.
        // layer_mask: bitmask of ObjectLayer values to test against.
        // Bit N is set if queries should collide with ObjectLayer N.
        // Default 0xFFFF tests all 16 possible layers; use collision layer
        // constants (ObjectLayers::NonMoving, etc.) to restrict.
        // Note: only the first 6 bits (ObjectLayer::Count) are used in v1.
        [[nodiscard]] RaycastResult Raycast(
            const Core::Maths::Vec3f& origin,
            const Core::Maths::Vec3f& direction,
            float                     max_dist,
            uint16_t                  layer_mask = 0xFFFFu
        ) const;

        // Sweep a sphere of given radius along direction up to max_dist metres.
        [[nodiscard]] ShapeCastResult SphereCast(
            const Core::Maths::Vec3f& origin,
            float                     radius,
            const Core::Maths::Vec3f& direction,
            float                     max_dist,
            uint16_t                  layer_mask = 0xFFFFu
        ) const;

        // Sweep an axis-aligned box along direction up to max_dist metres.
        [[nodiscard]] ShapeCastResult BoxCast(
            const Core::Maths::Vec3f& origin,
            const Core::Maths::Vec3f& half_extents,
            const Core::Maths::Vec3f& direction,
            float                     max_dist,
            uint16_t                  layer_mask = 0xFFFFu
        ) const;

        // ------------------------------------------------------------------ //
        //  Contact / Sensor events
        // ------------------------------------------------------------------ //

        // Register a callback to receive contact enter/exit events.
        // Callbacks fire on the main thread after each Update().
        void SetContactEventCallback(ContactEventCallback callback);

        // ------------------------------------------------------------------ //
        //  EntityID ↔ BodyID mapping
        // ------------------------------------------------------------------ //

        ECS::EntityID GetEntityForBody(BodyID id) const;
        BodyID        GetBodyForEntity(ECS::EntityID entity) const;

        // ------------------------------------------------------------------ //
        //  Gravity
        // ------------------------------------------------------------------ //

        void SetGravity(const Core::Maths::Vec3f& gravity);
        Core::Maths::Vec3f GetGravity() const;

        // Returns or creates a JPH::CharacterVirtual for this entity.
        // Lifetime: managed by PhysicsWorld; freed automatically on DestroyBody().
        // Must be called on the main thread, before physics step.
        JPH::CharacterVirtual* GetOrCreateCharacterVirtual(EntityID id,
            const ECS::Components::CharacterControllerComponent& cc);

    private:
        // Converts a ColliderComponent into a Jolt shape ref.
        // Returns a raw pointer to a JPH::ShapeSettings-derived object.
        // Caller is responsible for ref-counting via Jolt's own ref system.
        void* BuildJoltShape(const ECS::Components::ColliderComponent& col) const;

        // Internal contact listener; dispatches to m_ContactCallback after step.
        class InternalContactListener;

        Core::Memory::ArenaAllocator*                    m_Arena              = nullptr;
        JPH::PhysicsSystem*                              m_PhysicsSystem      = nullptr;
        JPH::JobSystemThreadPool*                        m_JobSystem          = nullptr;
        JPH::TempAllocatorImpl*                          m_TempAllocator      = nullptr;
        JPH::BroadPhaseLayerInterfaceTable*              m_BPLayerInterface   = nullptr;
        JPH::ObjectVsBroadPhaseLayerFilterTable*         m_ObjVsBPFilter      = nullptr;
        JPH::ObjectLayerPairFilterTable*                 m_ObjLayerFilter     = nullptr;
        InternalContactListener*                         m_ContactListener    = nullptr;

        // EntityID ↔ BodyID bidirectional mapping
        Core::Containers::UnorderedHashMap<uint32_t, ECS::EntityID> m_BodyToEntity;
        Core::Containers::UnorderedHashMap<ECS::EntityID, uint32_t> m_EntityToBody;

        // Pending contact events staged during Jolt callbacks; drained after Update()
        Core::Containers::Array<ContactEvent> m_PendingContactEvents;

        ContactEventCallback m_ContactCallback;

        bool m_Initialized = false;
    };

    // Global accessor — set by engine boot; never null after Initialize()
    PhysicsWorld* GetPhysicsWorld();
    void          SetPhysicsWorld(PhysicsWorld* world);

}  // namespace ZEngine::Physics
```

### 4.5 `PhysicsWorld::Initialize` implementation notes

```cpp
// ZEngine/Physics/PhysicsWorld.cpp  (key sections)

void PhysicsWorld::Initialize(
    Core::Memory::ArenaAllocator* arena,
    uint32_t max_bodies,
    uint32_t num_body_mutexes,
    uint32_t max_body_pairs,
    uint32_t max_contact_constraints)
{
    ZENGINE_VALIDATE_ASSERT(arena != nullptr, "PhysicsWorld::Initialize — arena must not be null");
    ZENGINE_VALIDATE_ASSERT(!m_Initialized,   "PhysicsWorld::Initialize — already initialized");

    m_Arena = arena;

    // Install allocator hooks before any Jolt object is constructed
    InstallJoltAllocatorHooks();

    // NOTE: Jolt requires ownership via raw new/delete for its Factory,
    // TempAllocator, and JobSystem singletons. These are initialized ONCE
    // at engine startup and freed at shutdown — they are NOT on any hot path.
    // All runtime physics allocations (body creation, constraint data) flow
    // through InstallJoltAllocatorHooks() which routes them through ZEngine's
    // ArenaAllocator. The three lines below are the ONLY permitted raw new calls
    // in the physics system.
    // Jolt factory and registered shapes
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    // 16 MB scratch allocator for Jolt's per-step temporary data
    m_TempAllocator = new JPH::TempAllocatorImpl(16u * 1024u * 1024u);

    // Thread pool: use hardware_concurrency - 1 so the main thread is free
    const int num_threads = static_cast<int>(std::thread::hardware_concurrency()) - 1;
    m_JobSystem = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        num_threads
    );

    // Broad-phase layer mapping
    m_BPLayerInterface = new JPH::BroadPhaseLayerInterfaceTable(
        ObjectLayer::Count,
        BroadPhaseLayer::Count
    );
    m_BPLayerInterface->MapObjectToBroadPhaseLayer(ObjectLayer::NonMoving,  BroadPhaseLayer::NonMoving);
    m_BPLayerInterface->MapObjectToBroadPhaseLayer(ObjectLayer::Moving,     BroadPhaseLayer::Moving);
    m_BPLayerInterface->MapObjectToBroadPhaseLayer(ObjectLayer::Player,     BroadPhaseLayer::Moving);
    m_BPLayerInterface->MapObjectToBroadPhaseLayer(ObjectLayer::Sensor,     BroadPhaseLayer::Moving);
    m_BPLayerInterface->MapObjectToBroadPhaseLayer(ObjectLayer::Projectile, BroadPhaseLayer::Moving);
    m_BPLayerInterface->MapObjectToBroadPhaseLayer(ObjectLayer::Debris,     BroadPhaseLayer::Moving);

    // Object-vs-broadphase filter
    m_ObjVsBPFilter = new JPH::ObjectVsBroadPhaseLayerFilterTable(
        *m_BPLayerInterface, BroadPhaseLayer::Count,
        ObjectLayer::Count
    );
    // Only NonMoving objects skip the Moving broad-phase bucket
    m_ObjVsBPFilter->DisableCollision(ObjectLayer::NonMoving, BroadPhaseLayer::Moving);

    // Object-layer pair filter (built from the table in CollisionLayers.h)
    m_ObjLayerFilter = new JPH::ObjectLayerPairFilterTable(ObjectLayer::Count);
    // Wire up the table described in CollisionLayers.h...
    // (omitted for brevity; follows the matrix in the header comment)

    // Main physics system
    m_PhysicsSystem = new JPH::PhysicsSystem();
    m_PhysicsSystem->Init(
        max_bodies,
        num_body_mutexes,
        max_body_pairs,
        max_contact_constraints,
        *m_BPLayerInterface,
        *m_ObjVsBPFilter,
        *m_ObjLayerFilter
    );

    // Contact listener
    m_ContactListener = new InternalContactListener(m_PendingContactEvents);
    m_PhysicsSystem->SetContactListener(m_ContactListener);

    m_Initialized = true;
}
```

---

## 5. ECS Systems

Each system is a free function registered with `WorldTick::RegisterSystem`. Component masks use `MaskBit(ComponentTypeOf<T>())`.

### 5.1 `PhysicsSyncTransformToBodySystem`

Reads `TransformComponent`, writes `RigidBodyComponent` (only `JoltBodyID` is used; the write mask is declared to prevent concurrent mutation of the component's cached state).

For **Static** bodies: no-op.
For **Kinematic** bodies: calls `PhysicsWorld::SetBodyPositionAndRotation`.
For **Dynamic** bodies: no-op pre-step (Jolt owns position).

```cpp
// ZEngine/Physics/PhysicsSystems.h
#pragma once
#include <ECS/WorldTick.h>

namespace ZEngine::Physics {

    void PhysicsSyncTransformToBodySystem(ECS::WorldTickContext& ctx);
    void PhysicsStepSystem(ECS::WorldTickContext& ctx);
    void PhysicsSyncBodyToTransformSystem(ECS::WorldTickContext& ctx);
    void CharacterControllerSystem(ECS::WorldTickContext& ctx);

    // Call this during engine boot to register all four systems in order.
    void RegisterPhysicsSystems(ECS::WorldTick& world);

}  // namespace ZEngine::Physics
```

```cpp
// ZEngine/Physics/PhysicsSystems.cpp

void PhysicsSyncTransformToBodySystem(ECS::WorldTickContext& ctx) {
    using namespace ECS::Components;
    PhysicsWorld* pw = GetPhysicsWorld();
    ZENGINE_VALIDATE_ASSERT(pw != nullptr, "PhysicsSyncTransformToBodySystem: PhysicsWorld is null");

    ctx.Scene.ForEach<TransformComponent, RigidBodyComponent>(
        [&](ECS::EntityID id, const TransformComponent& xf, RigidBodyComponent& rb)
        {
            if (rb.Type != RigidBodyType::Kinematic) return;
            if (rb.JoltBodyID == UINT32_MAX) return;

            Core::Maths::Quaternion<float> rot = Core::Maths::Quaternion<float>::FromEuler(
                xf.Rotation.X, xf.Rotation.Y, xf.Rotation.Z
            );
            pw->SetBodyPositionAndRotation(rb.JoltBodyID, xf.Position, rot);
        }
    );
}
```

**`SystemDeps`:**
```cpp
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<TransformComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
}
```

### 5.2 `PhysicsStepSystem`

Steps the entire simulation. Reads and writes `RigidBodyComponent` (marks the body state as updated).

```cpp
void PhysicsStepSystem(ECS::WorldTickContext& ctx) {
    PhysicsWorld* pw = GetPhysicsWorld();
    ZENGINE_VALIDATE_ASSERT(pw != nullptr, "PhysicsStepSystem: PhysicsWorld is null");
    pw->Update(ctx.DeltaTime, /* collision_steps = */ 1);
}
```

**`SystemDeps`:**
```cpp
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
}
```

### 5.3 `PhysicsSyncBodyToTransformSystem`

Reads `RigidBodyComponent`, writes `TransformComponent`. Copies the post-step Jolt body position and rotation back into `TransformComponent`. Skips Static and Sensor bodies.

```cpp
void PhysicsSyncBodyToTransformSystem(ECS::WorldTickContext& ctx) {
    using namespace ECS::Components;
    PhysicsWorld* pw = GetPhysicsWorld();
    ZENGINE_VALIDATE_ASSERT(pw != nullptr, "PhysicsSyncBodyToTransformSystem: PhysicsWorld is null");

    ctx.Scene.ForEach<RigidBodyComponent, TransformComponent>(
        [&](ECS::EntityID id, const RigidBodyComponent& rb, TransformComponent& xf)
        {
            if (rb.Type == RigidBodyType::Static) return;
            if (rb.JoltBodyID == UINT32_MAX)      return;

            Core::Maths::Vec3f             pos;
            Core::Maths::Quaternion<float> rot;
            pw->GetBodyPositionAndRotation(rb.JoltBodyID, pos, rot);

            xf.Position = pos;
            xf.Rotation = rot.ToEuler();
        }
    );
}
```

**`SystemDeps`:**
```cpp
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<TransformComponent>()),
}
```

### 5.4 `CharacterControllerSystem`

Reads `CharacterControllerComponent`, writes `TransformComponent`. Uses `JPH::CharacterVirtual` (obtained from a handle stored in a `PhysicsWorld`-owned pool keyed by `EntityID`).

```cpp
void CharacterControllerSystem(ECS::WorldTickContext& ctx) {
    using namespace ECS::Components;
    PhysicsWorld* pw = GetPhysicsWorld();
    ZENGINE_VALIDATE_ASSERT(pw != nullptr, "CharacterControllerSystem: PhysicsWorld is null");

    ctx.Scene.ForEach<CharacterControllerComponent, TransformComponent>(
        [&](ECS::EntityID id, CharacterControllerComponent& cc, TransformComponent& xf)
        {
            // Retrieve or create JPH::CharacterVirtual from internal pool
            JPH::CharacterVirtual* character = pw->GetOrCreateCharacterVirtual(id, cc);

            JPH::Vec3 jolt_desired_vel(
                cc.DesiredVelocity.X,
                cc.DesiredVelocity.Y,
                cc.DesiredVelocity.Z
            );

            // Apply gravity if airborne
            if (!character->IsSupported()) {
                jolt_desired_vel += pw->GetGravityJolt() * ctx.DeltaTime;
            }

            if (cc.WantsJump && character->IsSupported()) {
                jolt_desired_vel.SetY(cc.JumpImpulse);
                cc.WantsJump = false;
            }

            character->SetLinearVelocity(jolt_desired_vel);

            JPH::CharacterVirtual::ExtendedUpdateSettings update_settings;
            update_settings.mStickToFloorStepDown = JPH::Vec3(0, -cc.StepHeight, 0);
            update_settings.mWalkStairsStepUp     = JPH::Vec3(0,  cc.StepHeight, 0);

            character->ExtendedUpdate(
                ctx.DeltaTime,
                pw->GetGravityJolt(),
                update_settings,
                pw->GetPhysicsSystemRef().GetDefaultBroadPhaseLayerFilter(ObjectLayer::Player),
                pw->GetPhysicsSystemRef().GetDefaultLayerFilter(ObjectLayer::Player),
                {},
                {},
                *pw->GetTempAllocator()
            );

            // Write back position and grounded state
            JPH::Vec3 new_pos = character->GetPosition();
            xf.Position = { new_pos.GetX(), new_pos.GetY(), new_pos.GetZ() };
            cc.IsGrounded = character->IsSupported();

            JPH::Vec3 vel = character->GetLinearVelocity();
            cc.Velocity = { vel.GetX(), vel.GetY(), vel.GetZ() };
        }
    );
}
```

**`SystemDeps`:**
```cpp
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<CharacterControllerComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<TransformComponent>())
               | MaskBit(ComponentTypeOf<CharacterControllerComponent>()),
}
```

---

## 6. Contact Events / Triggers

### 6.1 `InternalContactListener`

Jolt calls `OnContactAdded` and `OnContactRemoved` from within its simulation threads during `PhysicsSystem::Update`. These callbacks must be lock-free and must not touch ECS data. They only append `ContactEvent` to a staging buffer.

```cpp
// ZEngine/Physics/PhysicsWorld.cpp  (nested class)

class PhysicsWorld::InternalContactListener : public JPH::ContactListener {
public:
    explicit InternalContactListener(Core::Containers::Array<ContactEvent>& pending)
        : m_Pending(pending) {}

    JPH::ValidateResult OnContactValidate(
        const JPH::Body& body1,
        const JPH::Body& body2,
        JPH::RVec3Arg    base_offset,
        const JPH::CollideShapeResult& /* collision_result */) override
    {
        // Accept all contacts — filtering is done via layer filter
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override
    {
        if (!body1.IsSensor() && !body2.IsSensor()) return;

        ContactEvent ev;
        ev.EntityA = GetEntityFromUserData(body1.GetUserData());
        ev.EntityB = GetEntityFromUserData(body2.GetUserData());
        ev.IsEnter = true;
        ev.PenetrationDepth = manifold.mPenetrationDepth;

        // Spinlock: Jolt calls this from simulation threads.
        // Array<T>::PushBack is not thread-safe without protection.
        while (m_PendingLock.test_and_set(std::memory_order_acquire)) {}
        m_Pending.PushBack(ev);
        m_PendingLock.clear(std::memory_order_release);
    }

    void OnContactRemoved(
        const JPH::SubShapeIDPair& sub_shape_pair) override
    {
        // Resolve EntityIDs from the pair's body IDs
        // (requires a lock on the BodyInterface to convert BodyID → UserData)
        // Deferred to post-step flush for correctness
        m_PendingRemovals.PushBack(sub_shape_pair);
    }

private:
    static ECS::EntityID GetEntityFromUserData(uint64_t user_data) {
        return static_cast<ECS::EntityID>(user_data & 0xFFFFFFFFu);
    }

    Core::Containers::Array<ContactEvent>&           m_Pending;
    Core::Containers::Array<JPH::SubShapeIDPair>     m_PendingRemovals;
    std::atomic_flag                                 m_PendingLock = ATOMIC_FLAG_INIT;
};
```

**Thread safety note:** `Array<T>::PushBack` is not thread-safe by default. Two options:
1. Wrap `m_Pending` with a spinlock (preferred — simple, correct).
2. Use per-thread staging buffers merged after step.

Option 1 is recommended for ZEngine's initial physics implementation.

### 6.2 Dispatching to the ECS event bus

After `PhysicsWorld::Update()` returns (i.e., the simulation is quiescent), drain `m_PendingContactEvents` and dispatch each event through `ECS::EventBus`:

```cpp
// ZEngine/Physics/PhysicsWorld.cpp

void PhysicsWorld::Update(float dt, int collision_steps) {
    ZENGINE_VALIDATE_ASSERT(m_Initialized, "PhysicsWorld::Update — not initialized");

    m_PhysicsSystem->Update(dt, collision_steps, m_TempAllocator, m_JobSystem);

    // Drain contact events — safe to access ECS now that simulation is quiescent
    if (m_ContactCallback) {
        for (const ContactEvent& ev : m_PendingContactEvents) {
            m_ContactCallback(ev);
        }
    }
    m_PendingContactEvents.Clear();

    // Process deferred removal events from OnContactRemoved
    // (resolve BodyID → EntityID via BodyInterface, then fire events)
}
```

Gameplay code registers sensor callbacks via:
```cpp
GetPhysicsWorld()->SetContactEventCallback([&](const Physics::ContactEvent& ev) {
    if (ev.IsEnter) {
        // ev.EntityA and ev.EntityB just started overlapping
    } else {
        // they stopped overlapping
    }
});
```

---

## 7. Collision Layers

The full collision matrix is encoded in `CollisionLayers.h` (Section 2) and wired into `JPH::ObjectLayerPairFilterTable` during `Initialize`. The table is an O(1) bitset lookup with no virtual calls in the Jolt hot path.

**Adding a new layer (e.g., `Water = 6`):**
1. Add the constant to `ObjectLayer::` namespace.
2. Increment `ObjectLayer::Count`.
3. Add a `MapObjectToBroadPhaseLayer` call in `PhysicsWorld::Initialize`.
4. Set desired collision booleans in `m_ObjLayerFilter`.

---

## 8. Raycasting API

```cpp
RaycastResult PhysicsWorld::Raycast(
    const Core::Maths::Vec3f& origin,
    const Core::Maths::Vec3f& direction,
    float                     max_dist,
    uint16_t                  layer_mask) const
{
    ZENGINE_VALIDATE_ASSERT(m_Initialized, "PhysicsWorld::Raycast — not initialized");

    // Direction must be unit-length. Non-unit direction scales the effective max_dist.
    ZENGINE_VALIDATE_ASSERT(
        Core::Maths::abs(direction.magnitude() - 1.f) < 0.001f,
        "PhysicsWorld::Raycast: direction must be unit-length (magnitude was %.4f)",
        direction.magnitude());

    JPH::RRayCast ray{
        JPH::Vec3(origin.X, origin.Y, origin.Z),
        JPH::Vec3(direction.X, direction.Y, direction.Z) * max_dist
    };

    JPH::RayCastResult jolt_result;
    const bool hit = m_PhysicsSystem->GetNarrowPhaseQuery().CastRay(
        ray,
        jolt_result,
        JPH::BroadPhaseLayerFilter{},  // could be refined with layer_mask
        JPH::ObjectLayerFilter{}
    );

    if (!hit) return RaycastResult{ .Hit = false };

    JPH::Vec3 hit_point  = ray.GetPointOnRay(jolt_result.mFraction);
    JPH::Vec3 hit_normal = m_PhysicsSystem->GetBodyInterface()
        .GetWorldSpaceSurfaceNormal(
            jolt_result.mBodyID,
            jolt_result.mSubShapeID2,
            hit_point
        );

    const uint64_t user_data = m_PhysicsSystem->GetBodyInterface()
        .GetUserData(jolt_result.mBodyID);
    ECS::EntityID entity = static_cast<ECS::EntityID>(user_data & 0xFFFFFFFFu);

    return RaycastResult{
        .HitEntity = entity,
        .HitPoint  = { hit_point.GetX(),  hit_point.GetY(),  hit_point.GetZ()  },
        .HitNormal = { hit_normal.GetX(), hit_normal.GetY(), hit_normal.GetZ() },
        .Distance  = jolt_result.mFraction * max_dist,
        .Hit       = true,
    };
}
```

`ShapeCastResult SphereCast(...)` and `BoxCast(...)` follow the same pattern using `JPH::NarrowPhaseQuery::CastShape` with `JPH::SphereShapeSettings` / `JPH::BoxShapeSettings`.

---

## 9. Scheduler Registration

All four systems are registered during engine boot in `RegisterPhysicsSystems`. The wave layout after registration is:

```
Wave 0: PhysicsSyncTransformToBodySystem
Wave 1: PhysicsStepSystem
Wave 2: PhysicsSyncBodyToTransformSystem, CharacterControllerSystem  (independent — different write masks)
```

`CharacterControllerSystem` writes `TransformComponent` and must therefore run after `PhysicsStepSystem` and cannot run concurrently with `PhysicsSyncBodyToTransformSystem` if both write `TransformComponent`. An explicit `OrderBefore` is required:

```cpp
void Physics::RegisterPhysicsSystems(ECS::WorldTick& world) {
    SystemID sync_to_body = world.RegisterSystem(
        PhysicsSyncTransformToBodySystem, {
            .ReadMask  = MaskBit(ComponentTypeOf<TransformComponent>()),
            .WriteMask = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
        }
    );

    SystemID step = world.RegisterSystem(
        PhysicsStepSystem, {
            .ReadMask  = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
            .WriteMask = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
        }
    );

    SystemID sync_to_transform = world.RegisterSystem(
        PhysicsSyncBodyToTransformSystem, {
            .ReadMask  = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
            .WriteMask = MaskBit(ComponentTypeOf<TransformComponent>()),
        }
    );

    SystemID character_ctrl = world.RegisterSystem(
        CharacterControllerSystem, {
            .ReadMask  = MaskBit(ComponentTypeOf<CharacterControllerComponent>()),
            .WriteMask = MaskBit(ComponentTypeOf<TransformComponent>())
                       | MaskBit(ComponentTypeOf<CharacterControllerComponent>()),
        }
    );

    // Ordering chain: sync_to_body → step → sync_to_transform
    world.OrderBefore(sync_to_body,      step);
    world.OrderBefore(step,              sync_to_transform);
    world.OrderBefore(step,              character_ctrl);

    // Both sync_to_transform and character_ctrl write TransformComponent — must serialize
    world.OrderBefore(sync_to_transform, character_ctrl);
}
```

**Position relative to other systems:**
- `OrderBefore(character_ctrl, RenderCullSystem)` — render must see final positions
- `OrderBefore(sync_to_transform, AnimationSampleSystem)` — animation may read transform
- `OrderBefore(sync_to_body, AudioListenerSystem)` — audio reads transform; must be post-physics

---

## 10. File Layout

```
ZEngine/Physics/
├── CMakeLists.txt
├── CollisionLayers.h          — layer constants and collision matrix documentation
├── JoltAllocatorAdapter.h
├── JoltAllocatorAdapter.cpp
├── PhysicsTypes.h             — BodyID, RaycastResult, ShapeCastResult, ContactEvent
├── PhysicsWorld.h             — PhysicsWorld class declaration (no Jolt includes)
├── PhysicsWorld.cpp           — implementation (includes <Jolt/Jolt.h>)
├── PhysicsSystems.h           — system function declarations
├── PhysicsSystems.cpp         — system function implementations
└── CharacterVirtualPool.h     — JPH::CharacterVirtual pool (internal to Physics/)

ZEngine/ECS/Components/
├── RigidBodyComponent.h       — new
├── ColliderComponent.h        — new
└── CharacterControllerComponent.h  — new
```

---

## 11. CMakeLists.txt Changes

```cmake
# ZEngine/CMakeLists.txt  (additions)

# ── Jolt Physics ──────────────────────────────────────────────────────────────
set(JOLT_PHYSICS_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(JOLT_DEBUG_RENDERER      OFF CACHE BOOL "" FORCE)  # disable Jolt's own debug UI
add_subdirectory(${ZENGINE_VENDOR_DIR}/JoltPhysics/Build JoltPhysics EXCLUDE_FROM_ALL)

# ── Physics module ─────────────────────────────────────────────────────────────
add_library(ZEnginePhysics STATIC
    Physics/JoltAllocatorAdapter.cpp
    Physics/PhysicsWorld.cpp
    Physics/PhysicsSystems.cpp
)

target_include_directories(ZEnginePhysics PRIVATE
    ${ZENGINE_VENDOR_DIR}/JoltPhysics   # gives <Jolt/Jolt.h>
)

target_link_libraries(ZEnginePhysics
    PUBLIC  ZEngineCore
    PRIVATE Jolt
)

target_compile_features(ZEnginePhysics PUBLIC cxx_std_20)

# Enable Jolt's SSE4.2/AVX2 path on x86; NEON on ARM
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(ZEnginePhysics PRIVATE -msse4.2)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    target_compile_options(ZEnginePhysics PRIVATE -mfpu=neon)
endif()

# Main engine links physics
target_link_libraries(ZEngineRuntime PUBLIC ZEnginePhysics)
```

**Vendoring:** Clone Jolt as a git submodule:
```
git submodule add https://github.com/jrouwe/JoltPhysics.git vendor/JoltPhysics
```
Pin to the latest stable tag (e.g., `v5.3.0`).

---

## 12. Deliverables Checklist

### New files
- [ ] `ZEngine/Physics/CollisionLayers.h`
- [ ] `ZEngine/Physics/JoltAllocatorAdapter.h`
- [ ] `ZEngine/Physics/JoltAllocatorAdapter.cpp`
- [ ] `ZEngine/Physics/PhysicsTypes.h`
- [ ] `ZEngine/Physics/PhysicsWorld.h`
- [ ] `ZEngine/Physics/PhysicsWorld.cpp`
- [ ] `ZEngine/Physics/PhysicsSystems.h`
- [ ] `ZEngine/Physics/PhysicsSystems.cpp`
- [ ] `ZEngine/Physics/CharacterVirtualPool.h`
- [ ] `ZEngine/Physics/CMakeLists.txt`
- [ ] `ZEngine/ECS/Components/RigidBodyComponent.h`
- [ ] `ZEngine/ECS/Components/ColliderComponent.h`
- [ ] `ZEngine/ECS/Components/CharacterControllerComponent.h`

### Modified files
- [ ] `ZEngine/CMakeLists.txt` — add Jolt subdir, ZEnginePhysics target
- [ ] `ZEngine/ECS/Components/ComponentRegistry.h` — register new component types
- [ ] `ZEngine/Engine/EngineStartup.cpp` — call `Physics::RegisterPhysicsSystems(world)`
- [ ] `.gitmodules` — add Jolt submodule entry

### Tests
- [ ] `Tests/Physics/PhysicsWorldTest.cpp` — Initialize/Shutdown, CreateBody/DestroyBody, gravity
- [ ] `Tests/Physics/RaycastTest.cpp` — Raycast against static box, miss case, hit-normal direction
- [ ] `Tests/Physics/ContactEventTest.cpp` — sensor overlap enter/exit events fire correctly
- [ ] `Tests/Physics/CharacterControllerTest.cpp` — IsGrounded state, step-up over small obstacle
- [ ] `Tests/Physics/CollisionLayerTest.cpp` — verify Player ↔ Sensor collides, Debris ↔ Debris does not
- [ ] `Tests/ECS/RigidBodyComponentTest.cpp` — component layout, default values
- [ ] `Tests/ECS/ColliderComponentTest.cpp` — component layout, default values
