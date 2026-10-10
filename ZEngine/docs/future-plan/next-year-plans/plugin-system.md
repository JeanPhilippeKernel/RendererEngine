# ZEngine — Plugin System

**Priority:** Next-year plan
**Status:** Design — no plugin loader, Plugin SDK, manifest, or third-party extension
ABI is implemented.
**Depends on:** stable engine lifecycle, schema/serialization extension points,
RenderGraph/ZUI extension contracts, and a product security model.

## Direction

Plugins are an externally supported compatibility and security commitment. The old SDK
headers, JSON descriptor, package layout, sample callbacks, hosted-language claims, and
render/editor examples were conceptual only. They must not be copied as public API.

## Required design

- Decide which extension classes are supported: native code, hosted code, isolated
  processes, import tools, render passes, gameplay systems, editor panels, or a
  deliberately smaller set.
- Define a versioned C-compatible ABI for any native boundary: opaque handles, explicit
  ownership/lifetime, fixed-width types, allocation rules, callback cancellation, and
  forward/backward compatibility. The C ABI uses native `const char*`; engine-owned
  C++ interfaces should use `cstring` for immutable C strings where appropriate.
- Define manifest/package identity, signing/trust, dependency resolution, permission
  grants, discovery, load order, upgrade/rollback, and unsupported-platform behavior.
- Make persistent plugin data schema-backed: stable component/field keys, codecs,
  migrations, references, and explicit unknown-plugin behavior on scene load/save.
- Define renderer and editor extension boundaries through current RenderGraph and ZUI
  lifecycles. Plugins cannot access mutable ECS/editor state from the render thread.

## Delivery order

1. Ship a minimal internal extension boundary with process lifecycle tests.
2. Publish one versioned SDK extension point and a compatibility test fixture.
3. Add packaging/signing/permissions and a safe loader.
4. Add broader editor/render/import/hosted-language extensions only after their
   persistence and teardown behavior is tested.

## Acceptance gates

- A bad, incompatible, unsigned, cyclic, or failing plugin cannot crash startup or
  leave a live callback/resource after rejection/unload.
- Plugin components round-trip only through registered codecs; a missing plugin follows
  the documented preservation or read-only failure policy.
- Versioning, permissions, reload, scene replacement, and shutdown have integration
  tests on every supported platform.
