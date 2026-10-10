# ZEngine — Scripting and Game Logic

**Priority:** P1 for rapid game-logic iteration
**Status:** Design — no game-DLL loader, scripting runtime, or public game API is
implemented.
**Depends on:** an explicit game runtime boundary, stable ECS/scene schemas, system
lifecycle, and platform loading/security policy.

## Direction

The engine needs a deliberate boundary for product game code before it chooses a
language or hot-reload mechanism. Compiled game code, embedded languages, and plugins
have different ABI, tooling, safety, persistence, and reload constraints; none is
currently a shipped ZEngine interface.

The earlier `GameAPI.h`, `ZGame_*` entry points, loader, and reload sequence were
design sketches. They do not declare a supported binary contract. In particular, no
in-process hot reload is safe merely because ECS data is outside the dynamic library:
function pointers, component codecs, registrations, queued work, and live objects must
all have a defined teardown/rebuild protocol.

## Required design decisions

- Choose the first supported model: statically linked game module, native dynamic
  library, hosted language, or a constrained combination. State supported platforms,
  toolchains, debug/release compatibility, and distribution model.
- If native dynamic libraries are supported, define a versioned C-compatible ABI with
  opaque handles, explicit ownership, fixed-width types, and callbacks. C ABI text
  fields retain `const char*`; engine-internal C++ APIs use `cstring` where suitable.
- Define registration timing and the lifetime of systems, components, callbacks,
  background work, render passes, assets, and editor extensions. Do not unload while
  any code/data reference can remain live.
- Require stable schema keys, codecs, migrations, and unknown-data policy before
  plugin/game components become serializable. Process-local component IDs, C++ layouts,
  and raw memory bytes are not persistence formats.
- Define error containment, permission/trust model, observability, debugging, reload
  rollback, and production build behavior.

## Delivery order

1. Publish and test the game/runtime extension boundary without reload.
2. Add one constrained extension point with explicit initialization and teardown.
3. Add reload only after quiescence, callback invalidation, schema compatibility, and
   rollback behavior are proven.
4. Add a hosted language or third-party SDK on top of that stable boundary.

## Acceptance gates

- Invalid, incompatible, or failing game code cannot leave registered callbacks, tasks,
  render work, or component data referring to unloaded code.
- Scene load/save, Play/Stop, editor shutdown, and normal process shutdown obey the
  same extension lifetime contract.
- ABI/version errors and reload failures return structured diagnostics and preserve a
  runnable prior state where rollback is promised.
