# ZEngine — Networking Stack

**Priority:** P2 — Required for multiplayer games  
**Status:** Design  
**Depends on:** `actor-ecs-architecture.md`, `system-scheduler.md`, `game-loop.md`  
**Blocks:** Multiplayer game shipping

---

## 1. Design Philosophy

ZEngine targets both indie developers and professional studios. The networking stack is
therefore layered and pluggable — the engine provides the infrastructure, the game
chooses the topology, netcode model, and transport.

**Core principles:**

- **Transport-agnostic.** The game plugs in any transport (GameNetworkingSockets, ENet,
  raw UDP) via a single interface. The rest of the stack is transport-blind.
- **Topology-agnostic.** Client-server and peer-to-peer are both supported. The session
  layer abstracts the difference.
- **Netcode-agnostic.** Rollback and server-authoritative prediction are both provided
  as optional modules. The game registers which model it uses.
- **ECS-native.** Network state is just components. Replication is a system. No
  parallel object hierarchy alongside the ECS.
- **DOD-conformant.** No virtual dispatch in the replication hot path. No `std::function`
  for packet callbacks. No `std::vector` or `std::string` in serialized data.
- **Determinism as a first-class concern.** The fixed-timestep accumulator
  (`game-loop.md`) is the foundation. Rollback is impossible without it.

---

## 2. Layer Architecture

```
┌───────────────────────────────────────────────────────────────┐
│                        Game Code                              │
│   Input prediction   State queries   Authority checks         │
└──────────────────────────┬────────────────────────────────────┘
                           │
┌──────────────────────────▼────────────────────────────────────┐
│                    Netcode Layer                               │
│   RollbackModule  (GGPO-style, for deterministic games)       │
│   PredictionModule (client prediction + server correction)    │
│   ReplicationModule (component state sync)                    │
└──────────────────────────┬────────────────────────────────────┘
                           │
┌──────────────────────────▼────────────────────────────────────┐
│                    Session Layer                               │
│   NetworkSession  (owns peers, ticks, clock sync)             │
│   PeerRegistry    (peer ID → connection handle)               │
│   NetworkClock    (NTP-style offset + drift correction)       │
└──────────────────────────┬────────────────────────────────────┘
                           │
┌──────────────────────────▼────────────────────────────────────┐
│                    Transport Layer                             │
│   INetTransport   (send/recv interface)                       │
│   GNSTransport    (GameNetworkingSockets — Steam relay, NAT)  │
│   ENetTransport   (ENet — lightweight, no Steam dependency)   │
│   UDPTransport    (raw UDP — maximum control)                 │
└───────────────────────────────────────────────────────────────┘
```

---

## 3. Transport Layer

### 3.1 `INetTransport`

The transport interface. All higher layers communicate through this. One implementation
is registered per session.

```cpp
// ZEngine/Network/Transport/INetTransport.h
#pragma once
#include <Core/Containers/Array.h>
#include <cstdint>

namespace ZEngine::Network {

    using PeerID    = uint32_t;
    using ChannelID = uint8_t;

    constexpr PeerID INVALID_PEER = UINT32_MAX;

    // Delivery guarantees per channel.
    enum class Reliability : uint8_t {
        Unreliable        = 0,  // fire and forget — inputs, position updates
        UnreliableOrdered = 1,  // latest packet wins — high-freq state
        Reliable          = 2,  // TCP-like — RPCs, state corrections, events
        ReliableOrdered   = 3,  // reliable + in-order — chat, game events
    };

    struct NetPacket {
        PeerID    From      = INVALID_PEER;
        ChannelID Channel   = 0;
        uint8_t*  Data      = nullptr;  // points into transport-owned buffer; valid until next Poll()
        uint32_t  Size      = 0;
    };

    // Plain function pointer callbacks — no std::function.
    using OnConnectedFn    = void (*)(void* ctx, PeerID peer);
    using OnDisconnectedFn = void (*)(void* ctx, PeerID peer, uint32_t reason);

    struct NetCallbacks {
        OnConnectedFn    OnConnected    = nullptr;
        OnDisconnectedFn OnDisconnected = nullptr;
        void*            Ctx            = nullptr;
    };

    struct INetTransport {
        virtual ~INetTransport() = default;

        // Lifecycle
        virtual bool Initialize(Core::Memory::ArenaAllocator* arena) = 0;
        virtual void Shutdown()                                       = 0;

        // Connection management
        struct ConnectResult {
            PeerID      Peer    = INVALID_PEER;
            bool        Success = false;
            const char* Error   = nullptr;  // valid until next API call; copy if needed
        };

        // Initiates a connection. Returns immediately; connection state changes
        // are delivered via NetCallbacks::OnConnected / OnDisconnected.
        [[nodiscard]] virtual ConnectResult Connect(const char* address, uint16_t port) = 0;
        virtual void   Disconnect(PeerID peer, uint32_t reason = 0)   = 0;
        virtual bool   IsConnected(PeerID peer) const                  = 0;
        virtual void   SetCallbacks(const NetCallbacks& callbacks)     = 0;

        // I/O — called once per frame on the network thread
        virtual void   Poll()                                          = 0;

        // Send a packet on the given channel with the specified reliability.
        // Data must remain valid until Send returns.
        virtual bool   Send(PeerID peer, ChannelID channel,
                            const uint8_t* data, uint32_t size,
                            Reliability reliability)                   = 0;

        // Read the next received packet. Returns false if the queue is empty.
        // Packet data is valid until the next Poll() call.
        virtual bool   Receive(NetPacket& out_packet)                  = 0;

        // Round-trip time to peer in milliseconds. 0 if unknown.
        virtual uint32_t GetPingMs(PeerID peer) const                  = 0;

        virtual const char* TransportName() const                      = 0;
    };

}  // namespace ZEngine::Network
```

### 3.2 `GNSTransport` — GameNetworkingSockets

Recommended for Steam games. Provides built-in encryption, NAT punch-through, and
Steam relay fallback. Requires the Steamworks SDK.

```cpp
// ZEngine/Network/Transport/GNSTransport.h
#pragma once
#include <Network/Transport/INetTransport.h>

namespace ZEngine::Network {

    // GameNetworkingSockets (Valve, MIT) transport.
    // Requires ZENGINE_STEAM build flag.
    // Provides: TLS encryption, Steam relay (no open ports needed),
    //           NAT punch-through, bandwidth estimation.
    struct GNSTransport final : public INetTransport {
        bool          Initialize(Core::Memory::ArenaAllocator* arena) override;
        void          Shutdown()                                       override;
        ConnectResult Connect(const char* address, uint16_t port)     override;
        void     Disconnect(PeerID peer, uint32_t reason)        override;
        bool     IsConnected(PeerID peer) const                   override;
        void     SetCallbacks(const NetCallbacks& cbs)            override;
        void     Poll()                                           override;
        bool     Send(PeerID, ChannelID, const uint8_t*, uint32_t, Reliability) override;
        bool     Receive(NetPacket& out)                          override;
        uint32_t GetPingMs(PeerID peer) const                     override;
        const char* TransportName() const override { return "GNS"; }
    };

}  // namespace ZEngine::Network
```

### 3.3 `ENetTransport` — ENet

Recommended for games that do not require Steam. Zero external dependencies beyond
the ENet library (MIT). No encryption built-in — add DTLS or application-layer
encryption if needed.

### 3.4 `UDPTransport` — Raw UDP

For studios that need full control of the transport stack. Provides only raw send/recv
with no reliability layer. The game or netcode module handles sequencing, ACKs, and
fragmentation.

---

## 4. Session Layer

`NetworkSession` owns the lifecycle of a multiplayer session. It holds the active
transport, the peer registry, and the network clock. One session exists per running
game instance.

```cpp
// ZEngine/Network/Session/NetworkSession.h
#pragma once
#include <Network/Transport/INetTransport.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <atomic>

namespace ZEngine::Network {

    enum class SessionRole : uint8_t {
        Offline    = 0,  // no network
        Host       = 1,  // server or P2P host
        Client     = 2,  // client connecting to host
        DedicatedServer = 3,
    };

    struct PeerInfo {
        PeerID   ID           = INVALID_PEER;
        uint32_t PingMs       = 0;
        bool     IsConnected  = false;
        bool     IsLocalPeer  = false;
    };

    struct NetworkSession {
        // Initialize with a transport implementation.
        // Transport must outlive the session.
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        INetTransport*               transport,
                        SessionRole                  role);
        void Shutdown();

        // Tick — call once per fixed simulation step.
        // Sends queued packets, receives and dispatches incoming packets,
        // updates clock sync.
        void Tick(float fixed_dt);

        // Peer management
        // Initiates a connection via the transport. Returns immediately.
        // Connection state changes are delivered via NetCallbacks::OnConnected / OnDisconnected.
        [[nodiscard]] INetTransport::ConnectResult Connect(const char* address, uint16_t port);
        void                        Disconnect(PeerID peer);
        [[nodiscard]] PeerInfo      GetPeerInfo(PeerID peer) const;
        void                        ForEachPeer(void (*fn)(void*, const PeerInfo&), void* ctx) const;
        [[nodiscard]] uint32_t      PeerCount() const;

        // Send — routes through the transport.
        // Called by higher layers (replication, RPC, rollback).
        bool Send(PeerID peer, ChannelID channel,
                  const uint8_t* data, uint32_t size,
                  Reliability reliability);

        // Broadcast to all connected peers.
        void Broadcast(ChannelID channel,
                       const uint8_t* data, uint32_t size,
                       Reliability reliability);

        [[nodiscard]] SessionRole   GetRole()      const { return m_role; }
        [[nodiscard]] INetTransport* GetTransport() const { return m_transport; }

        // Network clock — synchronized across all peers.
        [[nodiscard]] int64_t       GetNetworkTimeMs() const;
        [[nodiscard]] int32_t       GetClockOffsetMs() const;

    private:
        INetTransport*                                        m_transport  = nullptr;
        SessionRole                                           m_role       = SessionRole::Offline;
        Core::Containers::UnorderedHashMap<PeerID, PeerInfo> m_peers;
        Core::Memory::ArenaAllocator*                         m_arena      = nullptr;
        NetCallbacks                                          m_callbacks;

        // Network clock (NTP-style offset + drift correction)
        std::atomic<int64_t>  m_clock_offset_ms{0};

        void OnPeerConnected(PeerID peer);
        void OnPeerDisconnected(PeerID peer, uint32_t reason);
        void ProcessIncoming();
        void TickClock(float dt);
    };

}  // namespace ZEngine::Network
```

---

## 5. Network Clock

A synchronized clock is the foundation of every netcode model. Without it, rollback
cannot agree on frame numbers and server correction cannot apply at the right time.

ZEngine uses an **NTP-style two-way exchange** to compute the clock offset between
peers:

```
Client sends:  { t1 = local_time }
Server replies: { t1, t2 = server_receive_time, t3 = server_send_time }
Client receives at t4.

RTT    = (t4 - t1) - (t3 - t2)       // network round-trip time
Offset = ((t2 - t1) + (t3 - t4)) / 2 // estimated clock difference
```

`NetworkClock` runs the exchange every 5 seconds, collects 8 samples, and uses
the median offset to filter outliers (spikes from packet loss). The result is
`m_clock_offset_ms` — added to `local_time` to get `network_time`.

---

## 6. Packet Serialization

All network packets use a compact binary format. No JSON, no YAML, no `std::string`.

```cpp
// ZEngine/Network/Serialization/NetBitWriter.h / NetBitReader.h

struct NetBitWriter {
    NetBitWriter(uint8_t* buffer, uint32_t capacity);

    void WriteBool(bool v);
    void WriteUInt8(uint8_t v);
    void WriteUInt16(uint16_t v);
    void WriteUInt32(uint32_t v);
    void WriteUInt64(uint64_t v);
    void WriteFloat(float v);
    void WriteCompressedFloat(float v, float min, float max, float precision);
    void WriteVec3(const Core::Maths::Vec3f& v);
    void WriteQuat(const Core::Maths::Quaternion<float>& q);  // compressed to 3 floats (smallest-3)
    void WriteBytes(const uint8_t* data, uint32_t size);

    [[nodiscard]] uint32_t BytesWritten() const;
    [[nodiscard]] bool     Overflowed()   const;

private:
    uint8_t*  m_buffer;
    uint32_t  m_capacity;
    uint32_t  m_bit_pos = 0;
};

struct NetBitReader {
    NetBitReader(const uint8_t* buffer, uint32_t size);

    bool     ReadBool();
    uint8_t  ReadUInt8();
    uint16_t ReadUInt16();
    uint32_t ReadUInt32();
    uint64_t ReadUInt64();
    float    ReadFloat();
    float    ReadCompressedFloat(float min, float max, float precision);
    Core::Maths::Vec3f             ReadVec3();
    Core::Maths::Quaternion<float> ReadQuat();
    void     ReadBytes(uint8_t* out, uint32_t size);

    [[nodiscard]] bool Overflowed() const;
};
```

**Quaternion compression (smallest-3):** Drop the largest component (sign-encoded in
2 bits), store the other 3 as 10-bit fixed-point values in `[-1/sqrt(2), 1/sqrt(2)]`.
Total: 32 bits vs 128 bits for 4 floats — 4x reduction with negligible error.

**CompressedFloat:** Maps `[min, max]` to a fixed-point integer of the required
precision bits. Used for health, velocity, angles where exact float representation
is unnecessary.

---

## 7. Replication System

Replication is an ECS system that serializes component state and sends it over the
network. It runs after all simulation systems, before the render thread.

### 7.1 `NetReplicatedComponent`

Marks an entity as network-replicated. Every replicated entity carries this component.

```cpp
// ZEngine/ECS/Components/NetReplicatedComponent.h
struct NetReplicatedComponent {
    uint32_t  NetID        = 0;       // stable network-wide entity identifier
    PeerID    OwnerPeer    = INVALID_PEER; // peer that has authority over this entity
    bool      IsAuthority  = false;   // true on the authoritative side
    bool      IsPredicted  = false;   // true on clients during prediction
    uint8_t   ReplicaGroup = 0;       // used for relevance / interest management
};
```

### 7.2 Component replication descriptors

Each component type that should be replicated registers a descriptor:

```cpp
struct NetComponentDescriptor {
    ComponentTypeID TypeID;
    uint32_t        SerializedSizeBytes;  // max size; used for packet budget

    // Plain function pointers — no std::function, no virtual dispatch.
    using SerializeFn   = void (*)(const void* component, NetBitWriter& w);
    using DeserializeFn = void (*)(void* component, NetBitReader& r);
    using InterpolateFn = void (*)(void* component,
                                   const void* from, const void* to, float alpha);

    SerializeFn   Serialize   = nullptr;
    DeserializeFn Deserialize = nullptr;
    InterpolateFn Interpolate = nullptr;  // optional; for smooth rendering
};
```

Registration (once at startup):
```cpp
NetReplicationRegistry::Register({
    .TypeID            = ComponentTypeOf<TransformComponent>(),
    .SerializedSizeBytes = 40,
    .Serialize   = [](const void* c, NetBitWriter& w) {
        auto& t = *static_cast<const TransformComponent*>(c);
        w.WriteVec3(t.Position);
        w.WriteQuat(Core::Maths::fromEulerAngles(t.Rotation));
        w.WriteVec3(t.Scale);
    },
    .Deserialize = [](void* c, NetBitReader& r) {
        auto& t     = *static_cast<TransformComponent*>(c);
        t.Position  = r.ReadVec3();
        t.Rotation  = Core::Maths::toEulerAngle(r.ReadQuat());
        t.Scale     = r.ReadVec3();
    },
    .Interpolate = [](void* c, const void* from, const void* to, float alpha) {
        auto& cur       = *static_cast<TransformComponent*>(c);
        auto& f         = *static_cast<const TransformComponent*>(from);
        auto& t         = *static_cast<const TransformComponent*>(to);
        cur.Position    = Core::Maths::lerp(f.Position, t.Position, alpha);
        cur.Rotation    = Core::Maths::lerp(f.Rotation, t.Rotation, alpha);
        cur.Scale       = Core::Maths::lerp(f.Scale,    t.Scale,    alpha);
    },
});
```

**IMPORTANT:** Serialize/Deserialize/Interpolate function pointers MUST be plain
function pointers or stateless lambdas (no captures). A lambda with any captured
variable cannot convert to a plain function pointer and will fail to compile.
For serializers that require context (e.g., a resource manager), use a free function
with a global or pass context via the component data itself.

### 7.3 `ReplicationSystem`

ECS system that runs on the authority side (server or P2P host) once per fixed step.
Collects dirty components, serializes them into packets, sends via `NetworkSession`.

```cpp
// SystemDeps:
SystemDeps {
    .ReadMask  = MaskBit(ComponentTypeOf<NetReplicatedComponent>())
               | MaskBit(ComponentTypeOf<TransformComponent>())
               | /* all registered replicated types */,
    .WriteMask = 0,  // authority only reads; clients receive via ReplicationReceiveSystem
}
```

**Delta compression:** Each component tracks a `uint64_t DirtyMask` — one bit per
replicated field. Only fields that changed since the last acknowledged packet are
written. The receiver applies only the fields present in the packet.

**Relevance / interest management:** `ReplicaGroup` on `NetReplicatedComponent`
controls which peers receive updates. Group 0 = all peers. Custom groups allow
area-of-interest filtering for large worlds.

---

## 8. Netcode Module A — Rollback (GGPO-style)

For deterministic games: fighting games, co-op action, racing.

**Prerequisites:** Fixed timestep (`game-loop.md`), fully deterministic simulation
(same inputs → same state on all peers).

### 8.1 How it works

1. Each peer simulates locally using its own inputs + predicted inputs for remote peers.
2. Each peer sends its input for frame N to all other peers.
3. When a peer receives a late input for a past frame (frame N-K), it:
   - Saves the current state (ECS snapshot at frame N).
   - Rolls back to frame N-K.
   - Re-simulates frames N-K through N with the now-correct inputs.
   - Restores to frame N.

### 8.2 `RollbackModule`

```cpp
// ZEngine/Network/Rollback/RollbackModule.h
struct RollbackModule {
    // Initialize pre-divides m_snapshot_arena into max_rollback_frames fixed slots:
    //   slot_size = max_snapshot_bytes_per_frame (computed from NetReplicationRegistry)
    //   slot[i]   = m_snapshot_arena.base + i * slot_size
    // The arena pointer never advances after Initialize(). No reallocation occurs.
    void Initialize(Core::Memory::ArenaAllocator* arena,
                    ECS::Scene*                   scene,
                    NetworkSession*               session,
                    uint32_t                      max_rollback_frames = 8);

    // Call at the start of each fixed step.
    // Returns the frame number to simulate this tick.
    uint32_t BeginFrame();

    // Call after simulating the frame.
    // Records local input + ECS state snapshot.
    void     EndFrame(const InputFrame& local_input);

    // Called when a remote input arrives (may trigger rollback).
    // Returns true if a rollback occurred.
    bool     OnRemoteInput(PeerID peer, uint32_t frame, const InputFrame& input);

    // Query: is this frame fully confirmed (all peers' inputs received)?
    [[nodiscard]] bool IsFrameConfirmed(uint32_t frame) const;

    // The maximum number of frames we can roll back.
    [[nodiscard]] uint32_t MaxRollback() const { return m_max_rollback; }

private:
    struct FrameState {
        uint32_t FrameNumber  = 0;
        // Snapshot is allocated from a fixed slot in m_snapshot_arena.
        // Slot index = FrameNumber % max_rollback_frames.
        // INVARIANT: the arena is pre-divided into max_rollback_frames fixed-size slots
        // at Initialize() time. The arena NEVER wraps — each slot is permanently assigned
        // to a frame index slot. Writing frame N clobbers slot (N % max_rollback_frames),
        // which must no longer be needed (i.e., frame N - max_rollback_frames is confirmed).
        // If N - max_rollback_frames is NOT confirmed, this is a programming error.
        uint8_t* Snapshot     = nullptr;  // points into a fixed arena slot; never dangling
        uint32_t SnapshotSize = 0;
    };

    ECS::Scene*      m_scene        = nullptr;
    NetworkSession*  m_session      = nullptr;
    uint32_t         m_max_rollback = 8;
    uint32_t         m_current_frame = 0;

    // Ring buffers — capacity = max_rollback + 1
    Core::Containers::Array<FrameState>  m_snapshots;
    Core::Containers::Array<InputFrame>  m_local_inputs;
    // Per-peer input buffers
    Core::Containers::UnorderedHashMap<PeerID,
        Core::Containers::Array<InputFrame>> m_remote_inputs;

    Core::Memory::ArenaAllocator m_snapshot_arena;  // owns all snapshot memory
    uint32_t m_max_snapshot_bytes = 0;              // set in Initialize from component registry

    void SaveSnapshot(uint32_t frame);
    void RestoreSnapshot(uint32_t frame);
    void Resimulate(uint32_t from_frame, uint32_t to_frame);
};
```

### 8.3 ECS snapshot protocol

The snapshot captures only the components registered with `NetReplicationRegistry`.
It iterates all entities with `NetReplicatedComponent` and serializes each registered
component into a compact arena-allocated buffer. Restore replays the buffer back.

---

## 9. Netcode Module B — Server-Authoritative with Client Prediction

For non-deterministic games: FPS, battle royale, MMO, any game where cheat prevention
requires an authoritative server.

### 9.1 How it works

1. **Client** applies input immediately (prediction) without waiting for server.
2. **Client** sends input to server.
3. **Server** receives input, simulates, and sends the authoritative state back.
4. **Client** receives server state for a past frame, compares to its predicted state.
   - If match: discard — prediction was correct.
   - If mismatch: roll back to the server frame, re-simulate from there to present
     using buffered inputs.

### 9.2 `PredictionModule`

```cpp
// ZEngine/Network/Prediction/PredictionModule.h
struct PredictionModule {
    void Initialize(Core::Memory::ArenaAllocator* arena,
                    ECS::Scene*                  scene,
                    NetworkSession*              session);

    // Client side: record predicted state for this frame before simulation.
    void RecordPrediction(uint32_t frame);

    // Client side: receive server correction for a past frame.
    // Triggers re-simulation if prediction was wrong.
    void ApplyCorrection(uint32_t server_frame,
                         NetBitReader& state_reader);

    // Server side: authoritative simulation result to broadcast.
    void BroadcastState(uint32_t frame);

    // Reconciliation threshold — if position error < this, snap silently.
    float PositionErrorThreshold = 0.05f; // metres

private:
    ECS::Scene*     m_scene   = nullptr;
    NetworkSession* m_session = nullptr;
    Core::Memory::ArenaAllocator* m_arena = nullptr;

    // Client-side predicted state ring buffer
    struct PredictedFrame {
        uint32_t FrameNumber;
        uint8_t* Snapshot;
        uint32_t SnapshotSize;
    };
    Core::Containers::Array<PredictedFrame> m_predicted;

    bool NeedsReconciliation(uint32_t frame, NetBitReader& server_state) const;
    void Reconcile(uint32_t from_frame);
};
```

---

## 10. Input System Integration

Both netcode modules require inputs to be frame-stamped and serializable.

```cpp
// ZEngine/Network/Input/InputFrame.h
struct InputFrame {
    uint32_t FrameNumber = 0;
    PeerID   PeerID      = INVALID_PEER;

    // Game-defined input payload.
    // Serialized via NetBitWriter — must be deterministic across platforms.
    // Max 64 bytes to keep packets small.
    uint8_t  Data[64]   = {};
    uint8_t  DataSize   = 0;

    bool operator==(const InputFrame& other) const {
        return FrameNumber == other.FrameNumber
            && PeerID == other.PeerID
            && DataSize == other.DataSize
            && Helpers::secure_memcmp(Data, DataSize, other.Data, other.DataSize, DataSize) == 0;
    }
};
```

The game defines its input struct and serializes it into `InputFrame::Data`:
```cpp
struct PlayerInput {
    Vec2f    MoveAxis;        // 8 bytes
    bool     Jump;
    bool     Fire;
    uint8_t  _pad[2];
};

// Serialize:
NetBitWriter w(frame.Data, sizeof(frame.Data));
w.WriteCompressedFloat(input.MoveAxis.x, -1.f, 1.f, 0.01f);
w.WriteCompressedFloat(input.MoveAxis.y, -1.f, 1.f, 0.01f);
w.WriteBool(input.Jump);
w.WriteBool(input.Fire);
frame.DataSize = w.BytesWritten();
```

---

## 11. RPC System

Remote procedure calls for non-simulation events: damage dealt, item pickup,
chat messages, game state transitions.

```cpp
// ZEngine/Network/RPC/NetRPC.h

using RPCHandlerFn = void (*)(void* ctx, PeerID sender,
                               NetBitReader& reader);

struct RPCDescriptor {
    uint16_t      ID;           // stable, game-assigned ID
    RPCHandlerFn  Handler;
    void*         Ctx;
    Reliability   Delivery = Reliability::Reliable;
};

struct RPCRegistry {
    static void Register(const RPCDescriptor& desc);
    static void Dispatch(PeerID sender, NetBitReader& reader); // called by session on receive

    // Send an RPC to a specific peer.
    static void Send(NetworkSession& session, PeerID peer,
                     uint16_t rpc_id, NetBitWriter& payload);

    // Broadcast to all peers.
    static void Broadcast(NetworkSession& session,
                          uint16_t rpc_id, NetBitWriter& payload);
};
```

RPCs are sent on the `Reliable` channel. The receiver's session dispatches incoming
RPC packets to the registry, which invokes the registered handler. No virtual dispatch —
dispatch is a flat array lookup by RPC ID.

---

## 12. Lag Compensation

For server-authoritative games where hit detection must account for client latency.

The server maintains a short history of world state (typically 100–200ms worth of
frames). When a client fires a weapon, it sends:
- The client's current frame number
- The target entity's `NetID`
- The firing direction

The server rewinds to the frame the client was seeing, checks the hit, then fast-forwards
back to the current frame and applies the result.

```cpp
// ZEngine/Network/LagComp/LagCompensator.h
struct LagCompensator {
    void Initialize(Core::Memory::ArenaAllocator* arena,
                    ECS::Scene*                  scene,
                    uint32_t                     history_frames = 12);

    // Call every server fixed step to record the world state.
    void RecordFrame(uint32_t frame);

    // Rewind world state to the given frame for hit detection.
    // Returns false if the frame is too old (outside history).
    bool BeginRewind(uint32_t target_frame);

    // Restore the world to the current frame.
    void EndRewind();

    // Query: is a rewind currently active?
    [[nodiscard]] bool IsRewound() const { return m_rewound; }

private:
    ECS::Scene*                               m_scene         = nullptr;
    uint32_t                                  m_history_frames = 12;
    uint32_t                                  m_current_frame  = 0;
    bool                                      m_rewound        = false;
    Core::Containers::Array<uint8_t*>         m_snapshots;     // ring buffer
    Core::Containers::Array<uint32_t>         m_frame_numbers;
    Core::Memory::ArenaAllocator*             m_arena          = nullptr;
};
```

---

## 13. Interest Management / Relevance

For large worlds — open-world multiplayer, battle royale, MMO — replicating every
entity to every client is not viable. A 100-player server with 10 000 dynamic entities
would saturate bandwidth if every entity was sent to every client every frame.

Interest management solves this: the server only replicates entities that are
**relevant** to each client. Relevance is evaluated once per fixed step per peer.

### 13.1 Relevance model

ZEngine uses a **spatial grid** as the primary relevance filter, combined with an
optional **explicit relevance callback** for game-defined rules (e.g. "always replicate
the player's own character regardless of distance", "replicate all projectiles to
everyone").

```
Relevant to peer P if ANY of the following are true:
  1. Entity is within m_view_radius metres of P's controlled entity (spatial check)
  2. Entity has AlwaysRelevant = true on NetReplicatedComponent
  3. Game-registered relevance callback returns true for (entity, peer)
```

### 13.2 `NetReplicatedComponent` additions

```cpp
struct NetReplicatedComponent {
    uint32_t  NetID           = 0;
    PeerID    OwnerPeer       = INVALID_PEER;
    bool      IsAuthority     = false;
    bool      IsPredicted     = false;
    uint8_t   ReplicaGroup    = 0;
    bool      AlwaysRelevant  = false;  // bypass spatial check — use for players, HUD actors
    float     RelevanceRadius = 0.f;    // 0 = use session default; >0 overrides per entity
};
```

### 13.3 `NetRelevanceSystem`

Runs on the server once per fixed step. For each connected peer, builds a
`RelevanceSet` — the set of `NetID`s that are relevant to that peer this frame.
`ReplicationSystem` uses this set to decide which entities to include in each
peer's replication packet.

```cpp
// ZEngine/Network/Relevance/NetRelevanceSystem.h
#pragma once
#include <Network/Session/NetworkSession.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Containers/Array.h>
#include <ECS/Scene.h>

namespace ZEngine::Network {

    // Game-defined relevance override — return true to force an entity relevant
    // to a given peer regardless of distance. Plain function pointer, no std::function.
    using RelevanceOverrideFn = bool (*)(void* ctx, EntityID entity, PeerID peer);

    struct NetRelevanceConfig {
        float                DefaultViewRadius = 200.f; // metres; replicate within this range
        uint32_t             GridCellSize      = 50;    // metres per spatial grid cell
        RelevanceOverrideFn  Override          = nullptr;
        void*                OverrideCtx       = nullptr;
    };

    struct NetRelevanceSystem {
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        NetworkSession*               session,
                        const NetRelevanceConfig&     config);

        // ECS system function — called once per fixed step on the server.
        // Populates m_relevance_sets for use by ReplicationSystem.
        void Tick(ECS::Scene& scene, float dt);

        // Query: is `net_id` relevant to `peer` this frame?
        // Called by ReplicationSystem during packet build.
        [[nodiscard]] bool IsRelevant(PeerID peer, uint32_t net_id) const;

        // Returns the set of NetIDs that became relevant to `peer` this frame
        // (entered relevance — trigger a full state snapshot for these).
        [[nodiscard]] const Core::Containers::Array<uint32_t>&
            GetNewlyRelevant(PeerID peer) const;

        // Returns the set of NetIDs that became irrelevant to `peer` this frame
        // (left relevance — send a destroy message to the client).
        [[nodiscard]] const Core::Containers::Array<uint32_t>&
            GetNewlyIrrelevant(PeerID peer) const;

    private:
        // Spatial grid — maps grid cell (x,z) to list of NetIDs in that cell.
        // Rebuilt each tick from TransformComponent positions.
        struct GridCell {
            int32_t X = 0;
            int32_t Z = 0;
            Core::Containers::Array<uint32_t> NetIDs;
        };

        NetworkSession*    m_session = nullptr;
        NetRelevanceConfig m_config;
        Core::Memory::ArenaAllocator* m_arena = nullptr;

        // Per-peer relevance sets — rebuilt each tick.
        Core::Containers::UnorderedHashMap<
            PeerID,
            Core::Containers::Array<uint32_t>> m_current;   // currently relevant
        Core::Containers::UnorderedHashMap<
            PeerID,
            Core::Containers::Array<uint32_t>> m_previous;  // relevant last tick

        Core::Containers::UnorderedHashMap<
            PeerID,
            Core::Containers::Array<uint32_t>> m_newly_relevant;
        Core::Containers::UnorderedHashMap<
            PeerID,
            Core::Containers::Array<uint32_t>> m_newly_irrelevant;

        // Flat spatial grid — arena-allocated, cleared and rebuilt each tick.
        Core::Containers::Array<GridCell> m_grid;

        void RebuildGrid(ECS::Scene& scene);
        void QueryRelevantForPeer(ECS::Scene& scene, PeerID peer,
                                  EntityID controller_entity,
                                  Core::Containers::Array<uint32_t>& out);
        void DiffRelevanceSets(PeerID peer);

        [[nodiscard]] int32_t GridCoord(float world) const {
            // Floor division: ensures negative coordinates map correctly.
            // C++ integer division truncates toward zero; e.g. (-0.5 / 50) = 0 (wrong: should be -1).
            // floor(-0.5 / 50.0) = floor(-0.01) = -1 (correct).
            return static_cast<int32_t>(
                Core::Maths::floor(world / static_cast<float>(m_config.GridCellSize)));
        }
    };

}  // namespace ZEngine::Network
```

### 13.4 Algorithm

Each tick on the server:

```
1. RebuildGrid:
   ForEach<TransformComponent, NetReplicatedComponent>(scene, [](EntityID, Transform& t, NetReplicated& r) {
       cell = GridCell{ GridCoord(t.Position.x), GridCoord(t.Position.z) }
       grid[cell].push(r.NetID)
   })

2. For each connected peer P:
   a. Find P's controlled entity (OwnerPeer == P.ID && IsAuthority == false on client,
      or look up via PeerID → EntityID map)
   b. Get controller position C
   c. radius = config.DefaultViewRadius
   d. Enumerate all grid cells within radius of C (circle in XZ plane)
   e. For each entity in those cells:
        if entity.AlwaysRelevant OR distance(entity, C) <= max(radius, entity.RelevanceRadius):
            add to current[P]
   f. Also add all AlwaysRelevant entities regardless of grid

3. DiffRelevanceSets(P):
   newly_relevant[P]   = current[P] - previous[P]   (newly entered relevance)
   newly_irrelevant[P] = previous[P] - current[P]   (left relevance)
   previous[P]         = current[P]
```

### 13.5 Replication integration

`ReplicationSystem` is updated to consult `NetRelevanceSystem`:

- **Newly relevant** entities: send a full state snapshot (not delta) for that entity
  to the peer. Client spawns the entity locally.
- **Currently relevant** entities: send delta-compressed update as normal.
- **Newly irrelevant** entities: send a destroy message. Client despawns the entity.
- **Never relevant** entities: skipped entirely — zero bandwidth.

### 13.6 Bandwidth impact

A 100-player server with a 200m view radius on a 50m grid cell size will evaluate
~16 grid cells per peer per tick (4x4 cell square). With 10 000 entities distributed
across a 1km² map, each cell holds ~25 entities on average, so each peer sees ~400
entities — 4% of the world. At 40 bytes per entity per frame at 60Hz:

```
400 entities × 40 bytes × 60Hz = 960 KB/s per peer
100 peers × 960 KB/s = 96 MB/s server outbound — within datacenter limits
```

Without relevance filtering the same scenario would be:
```
10 000 × 40 × 60 × 100 = 2.4 GB/s — not viable
```

---

## 14. Scheduler Integration

Network systems run in dedicated waves after all simulation systems and before the
render systems.

```cpp
// App startup — after all game systems are registered:

SystemID net_recv_id = world.RegisterSystem(ReplicationReceiveSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<NetReplicatedComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<TransformComponent>()),
});

// Server only: relevance must run before replication so ReplicationSystem
// can query IsRelevant() during packet build.
SystemID relevance_id = world.RegisterSystem(NetRelevanceSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<NetReplicatedComponent>())
               | MaskBit(ComponentTypeOf<TransformComponent>()),
    .WriteMask = 0,
});

SystemID replication_id = world.RegisterSystem(ReplicationSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<NetReplicatedComponent>())
               | MaskBit(ComponentTypeOf<TransformComponent>()),
    .WriteMask = 0,
});

// Ordering:
// 1. Receive incoming state before physics reads transforms (client side)
// 2. Relevance evaluation before replication packet build (server side)
// 3. Replication packet build after physics has updated transforms
// 4. Replication before render reads final transforms
world.OrderBefore(net_recv_id,     physics_sync_id);
world.OrderBefore(physics_step_id, relevance_id);
world.OrderBefore(relevance_id,    replication_id);
world.OrderBefore(replication_id,  render_cull_id);

world.Commit();
```

---

## 16. Channels

Define these as compile-time constants in `ZEngine/Network/Transport/INetTransport.h`
so game code never uses magic numbers:

```cpp
namespace ZEngine::Network::Channels {
    constexpr ChannelID Input      = 0;  // UnreliableOrdered — per-frame player input
    constexpr ChannelID State      = 1;  // UnreliableOrdered — high-frequency replication
    constexpr ChannelID Correction = 2;  // Reliable          — server corrections, snapshots
    constexpr ChannelID RPC        = 3;  // Reliable          — game events, damage, chat
    constexpr ChannelID Clock      = 4;  // Unreliable        — clock sync probes
    constexpr ChannelID Control    = 5;  // ReliableOrdered   — handshake, session config
}
```

| Channel | ID | Reliability | Used for |
|---|---|---|---|
| `Channels::Input` | 0 | UnreliableOrdered | Per-frame player input |
| `Channels::State` | 1 | UnreliableOrdered | High-frequency component replication |
| `Channels::Correction` | 2 | Reliable | Server state corrections, snapshots |
| `Channels::RPC` | 3 | Reliable | Game events, damage, chat |
| `Channels::Clock` | 4 | Unreliable | Clock sync probes |
| `Channels::Control` | 5 | ReliableOrdered | Connection handshake, session config |

---

## 17. File Layout

```
ZEngine/
  Network/
    Transport/
      INetTransport.h
      GNSTransport.h / .cpp      (GameNetworkingSockets — ZENGINE_STEAM guard)
      ENetTransport.h / .cpp     (ENet)
      UDPTransport.h / .cpp      (raw UDP)

    Session/
      NetworkSession.h / .cpp
      NetworkClock.h / .cpp
      PeerRegistry.h / .cpp

    Serialization/
      NetBitWriter.h / .cpp
      NetBitReader.h / .cpp

    Replication/
      NetReplicatedComponent.h
      NetComponentDescriptor.h
      NetReplicationRegistry.h / .cpp
      ReplicationSystem.h / .cpp
      ReplicationReceiveSystem.h / .cpp

    Rollback/
      RollbackModule.h / .cpp
      InputFrame.h

    Prediction/
      PredictionModule.h / .cpp

    Relevance/
      NetRelevanceSystem.h / .cpp

    RPC/
      NetRPC.h / .cpp

    LagComp/
      LagCompensator.h / .cpp

    Input/
      InputFrame.h

  ECS/Components/
    NetReplicatedComponent.h
```

---

## 18. Dependencies

| Dependency | Why |
|---|---|
| `actor-ecs-architecture.md` | ECS::Scene, ComponentStorage, EntityID |
| `system-scheduler.md` | WorldTick registration, OrderBefore |
| `game-loop.md` | Fixed timestep — required for rollback and determinism |
| `physics-system.md` | Physics state is replicated; lag compensation rewinds physics snapshots |
| GameNetworkingSockets | Optional transport — `ZENGINE_STEAM` guard |
| ENet | Optional transport — no Steam dependency |

---

## 19. Deliverables Checklist

- [ ] `INetTransport.h` — interface + `NetPacket`, `NetCallbacks`, `Reliability`
- [ ] `GNSTransport.h/.cpp` — GameNetworkingSockets backend (`ZENGINE_STEAM`)
- [ ] `ENetTransport.h/.cpp` — ENet backend
- [ ] `UDPTransport.h/.cpp` — raw UDP backend
- [ ] `NetworkSession.h/.cpp` — peer registry, broadcast, tick
- [ ] `NetworkClock.h/.cpp` — NTP-style two-way sync, median filter
- [ ] `NetBitWriter.h/.cpp` — bit-packing, compressed float, smallest-3 quat
- [ ] `NetBitReader.h/.cpp`
- [ ] `NetReplicatedComponent.h`
- [ ] `NetReplicationRegistry.h/.cpp` — descriptor table, serialize/deserialize dispatch
- [ ] `ReplicationSystem.h/.cpp` — authority-side dirty-flag delta compression
- [ ] `ReplicationReceiveSystem.h/.cpp` — client-side apply + interpolate
- [ ] `RollbackModule.h/.cpp` — snapshot ring buffer, rollback, resimulate
- [ ] `PredictionModule.h/.cpp` — client prediction, server correction reconciliation
- [ ] `NetRPC.h/.cpp` — flat dispatch table, send/broadcast helpers
- [ ] `LagCompensator.h/.cpp` — server-side history, rewind/restore
- [ ] `NetRelevanceSystem.h/.cpp` — spatial grid, per-peer relevance sets, diff (newly in/out)
- [ ] `InputFrame.h`
- [ ] `tests/Network/TransportTest.cpp` — loopback send/recv, reliability modes
- [ ] `tests/Network/RollbackTest.cpp` — rollback with injected input delay, verify state match
- [ ] `tests/Network/ReplicationTest.cpp` — serialize/deserialize round-trip per component
- [ ] `tests/Network/ClockTest.cpp` — offset computation, drift correction
- [ ] `tests/Network/RPCTest.cpp` — register, send, dispatch
- [ ] `tests/Network/RelevanceTest.cpp` — grid query, newly relevant/irrelevant diff, AlwaysRelevant override

---

## 20. V2 Considerations — Industry Standard Deviations

The following items are **not in v1** but represent where the industry is heading or
where ZEngine deliberately departs from legacy engine conventions. They should be
revisited after the core v1 stack is live and battle-tested.

### 20.1 Shipping both rollback and server-authoritative in one engine

Most engines pick one model. Unreal is server-authoritative only. GGPO is rollback only.
Shipping both as first-class modules increases design surface area and test coverage
requirements. In v1 both modules are provided but the game must pick one per session.

**V2 consideration:** A hybrid model — rollback for client-side cosmetics
(animations, particles, sound) layered on top of server-authority for game state
(health, positions, inventory). This is used by Overwatch and is sometimes called
"visual rollback." Requires separating simulation state (authoritative) from
presentation state (rollback-safe).

### 20.2 Input compression

V1 `InputFrame` is 64 bytes. At 60Hz with 100 players this is 384 KB/s server inbound
just for inputs. Overwatch uses 6 bytes per input frame. Competitive games at scale
require aggressive bit-packing per input field rather than a fixed-size payload.

**V2 consideration:** Replace the fixed `Data[64]` payload with a game-registered
`InputSerializeFn` that writes into `NetBitWriter` directly. The game defines exactly
which bits each input field occupies. Expected savings: 64 bytes → 8–16 bytes per frame
for typical inputs.

### 20.3 Interest management — BVH vs. spatial grid

V1 uses a uniform spatial grid (fixed cell size). This works well for evenly distributed
entities but degrades for clustered scenarios (everyone in a city vs. empty countryside).

**V2 consideration:** Replace the grid with a dynamic BVH (bounding volume hierarchy)
for relevance queries. BVH queries are O(log N) vs. O(cells_in_radius) for a grid and
handle non-uniform entity distribution without tuning `GridCellSize`. Unreal uses a
spatial hash; Unity DOTS networking uses a custom BVH. The API surface (`IsRelevant`,
`GetNewlyRelevant`) does not change — only the internal query structure.

### 20.4 Snapshot interpolation for non-predicted entities

V1 `ReplicationReceiveSystem` applies received state immediately (snap). For entities
the client does not predict (remote players, AI), snapping causes visible jitter at
any non-zero packet loss.

**V2 consideration:** Buffer 2–3 received snapshots per entity and interpolate between
them with a 100ms delay. This is the standard approach used by Source engine, Unreal,
and all modern FPS titles. Requires storing a short snapshot history per
`NetReplicatedComponent` and an interpolation cursor, using the existing
`NetComponentDescriptor::Interpolate` function pointer (already designed in §7.2).

### 20.5 QUIC / WebTransport as a transport backend

V1 transports are all UDP-based. QUIC (RFC 9000) provides multiplexed streams, built-in
TLS 1.3, and connection migration over a UDP foundation. It is increasingly used for
game backends (Google Stadia used QUIC, Epic's EOS is moving toward it).

**V2 consideration:** A `QUICTransport` backend via `msquic` (MIT, Microsoft) or
`quiche` (BSD, Cloudflare). Maps cleanly onto `INetTransport` — the interface does not
change. Provides encryption and multiplexed channels without GameNetworkingSockets
dependency, which matters for non-Steam distribution.
