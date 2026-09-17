# ZEngine — Audio System

**Priority:** P1 for games that ship sound
**Status:** Design — the engine has no Audio module, audio device, audio ECS
components, or selected third-party audio backend.
**Depends on:** VFS/asset import contracts, ECS lifetime rules, and a game runtime
boundary.

## Purpose

The audio system must turn authored sound assets and scene/gameplay events into a
bounded, real-time-safe output graph. It must not make the render thread, ECS storage,
or a particular middleware's types part of the public authoring format.

Earlier miniaudio-specific APIs and component layouts were proposals, not shipped
interfaces. Selecting miniaudio, Jolt-style allocator patterns, or a fixed voice count
in this document would prematurely freeze a dependency and ABI.

## Non-negotiable boundaries

- Decode, stream, and asset I/O work occur outside the audio callback. The callback
  cannot wait on VFS, take unbounded locks, allocate, log, or call into mutable ECS.
- The simulation thread sends bounded commands or immutable snapshots to the audio
  domain. It does not expose an audio-backend object in ECS component storage.
- The audio domain owns voice/backend-object lifetime. Entity, scene, Play/Stop, and
  asset-reload events must resolve through an explicit cancellation/release policy.
- Persisted data names stable audio asset UUIDs and authored parameters only. Active
  voices, decoder state, backend handles, and playback cursors are runtime-derived.
- Any engine-owned proposed text API uses `cstring` where an immutable C string is
  appropriate. External-library C APIs retain their required native signatures.

## Decisions required before implementation

1. Choose and license-review the backend on all supported platforms, including device
   loss/hot-plug, spatialization, streaming, codecs, and distribution obligations.
2. Define the command/snapshot ownership, queue capacity/overflow policy, audio clock,
   latency target, and shutdown order.
3. Specify authored clip, bus/mixer, listener, attenuation, concurrency, and streaming
   schemas together with scene serializer and cook behavior.
4. Specify focus/background, pause, master volume, error reporting, and device fallback
   behavior for the editor and a game.

## Acceptance gates

- Callback instrumentation demonstrates no allocation, blocking I/O, or engine lock
  acquisition on the real-time path.
- Repeated play/stop, entity destruction, scene reload, Play/Stop, and device changes
  neither leak nor use a stale voice.
- Missing/corrupt assets and queue saturation have defined, observable fallback behavior.
- Spatial, non-spatial, streamed, and mixed voices are covered by automated state tests
  and platform listening/reference tests.
