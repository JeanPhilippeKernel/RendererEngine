# ZEngine — Lua Scripting

**Priority:** Next-year plan
**Status:** Design — no Lua runtime, bindings, script component, or file-watcher reload
path is implemented.
**Depends on:** a stable scripting/plugin lifecycle, VFS asset policy, ECS/scene schema,
and a decision to support embedded scripting.

## Direction

Lua is a possible hosted-language layer, not an implemented v2 extension of a shipped
C++ game-DLL system. The previous VM, component, bindings, scheduler, coroutine, and
performance-number sections were proposals rather than API commitments.

## Required design

- Select the Lua version, distribution/licensing policy, allocator/GC limits, error
  reporting, debugger tooling, and main-thread ownership model.
- Expose capability-oriented bindings with validated opaque entity/component values.
  Scripts never retain raw ECS addresses, mutable pointers, or process-local IDs across
  callbacks or scene replacement.
- Define script asset identity, module imports, sandbox permissions, deterministic/replay
  policy, coroutine lifetime, cancellation, and errors at every engine boundary.
- Define authored script-component codecs using stable asset UUIDs and field schemas;
  runtime VM references/coroutines are rebuilt, never serialized.
- Specify reload only at a safe point that invalidates callbacks/tasks and preserves or
  explicitly resets state according to a documented migration policy.

## Acceptance gates

- Syntax/runtime errors, runaway work, cancellation, asset reload, scene replacement,
  and shutdown cannot retain an invalid engine object or stall a frame indefinitely.
- Script component scenes round-trip across a fresh process through declared codecs.
- GCs, coroutine limits, binding calls, and sandbox permissions have stress and
  adversarial tests before designer-facing use.
