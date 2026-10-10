# ZEngine — Python Plugin Host

**Priority:** Next-year plan
**Status:** Design — no Plugin SDK, plugin loader, Python host, or embedded CPython
runtime is implemented.
**Depends on:** a shipped plugin ABI/lifecycle, stable editor and serializer extension
contracts, and a decision to support hosted code.

## Direction

A Python host is a convenience layer above a stable plugin system, not an alternate way
to bypass one. It must expose a deliberately narrow, ownership-safe API; direct raw ECS
memory addresses, arbitrary component layouts, or engine-pointer capsules cannot be a
production scripting contract.

The prior CPython version choice, capsule APIs, ctypes memory access, callback
trampolines, hot reload, and sample package were proposals. None is available today.

## Required design decisions

- Supported Python implementation/version, distribution model, license/security update
  policy, GIL/thread ownership, startup/shutdown, exception reporting, and debugger
  integration.
- Capability-oriented bindings for editor tools and gameplay, with explicit object
  lifetimes, thread rules, quotas, cancellation, and no dangling engine references.
- Schema-backed plugin components and authored fields: stable component/field keys,
  codecs/migrations, reference handling, and unknown-plugin behavior on load/save.
- Isolation and trust policy for untrusted scripts; hosted code must not implicitly gain
  native filesystem, network, process, or arbitrary-pointer access.
- Reload policy that invalidates or migrates Python state only at safe lifecycle
  boundaries. It must not execute stale callbacks after scene/plugin teardown.

## Acceptance gates

- Interpreter failure, exception, cancellation, reload, scene replacement, and shutdown
  cannot retain an invalid engine reference or block a frame indefinitely.
- A plugin component survives fresh-process scene round-trip with declared codec and
  compatibility behavior.
- Threading, GIL, memory, permission, and performance limits have automated stress
  coverage before the host is presented as production-ready.
