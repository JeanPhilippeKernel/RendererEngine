# ZEngine — Game Save System

**Priority:** P2
**Status:** Design — no game-save module or save-file format is implemented.
**Depends on:** VFS write/error semantics, scene-document serialization, stable asset
identity, and product security/cloud requirements.

## Boundary

An authored scene document is collaborative source data. A cooked scene is a validated
runtime artifact. A game save is player/session state. They have different ownership,
compatibility, validation, and privacy requirements and must not share an unversioned
memory dump.

The earlier `GameSaveData`, `SaveManager`, checksum, and `ZSav` header APIs were
unimplemented sketches. They do not define a file format or a public API.

## Required design

- Define save slots/profiles, stable format magic/version, bounded lengths, integrity
  validation, migration rules, corruption recovery, atomic replacement, and disk-full
  behavior.
- Define which gameplay data is saveable and provide explicit codecs. ECS entity IDs,
  pointers, render/physics/audio handles, and arbitrary component bytes are never
  durable identity. References use stable gameplay, scene, and asset IDs.
- Decide checkpoint/autosave cadence, threading/snapshot ownership, cancellation,
  encryption/tamper policy, privacy/retention, cloud conflict resolution, and platform
  storage locations.
- Separate an editor's source-file dirty checkpoint and undo history from a player
  save. A save must not mutate a scene document as a side effect.

## Acceptance gates

- A save reloads across a fresh process and supported versions while preserving its
  documented state and repairing references only through declared migrations.
- Truncated, corrupted, oversized, future-version, and interrupted-write files leave
  the last known good slot intact and return actionable diagnostics.
- Concurrent gameplay mutation cannot race a save snapshot; delete/profile/cloud
  conflicts follow defined user-visible policy.
