# ZEngine — Plugin Distribution

**Priority:** Next-year plan
**Status:** Deferred design — no plugin loader/SDK exists, so a plugin store has no
implementation foundation.
**Depends on:** a stable plugin ABI, package/signature policy, dependency resolver,
sandboxing model, and product account/telemetry decisions.

## Direction

Distribution follows a secure, supported plugin runtime; it cannot define that runtime.
The old store API, catalog schema, endpoints, payment assumptions, and UI flows were
hypothetical and are not a service contract.

## Prerequisites

1. Ship and support a minimal plugin package/manifest and versioned ABI.
2. Define trust roots, signing, verification, revocation, update/rollback, dependency
   resolution, permissions, and offline behavior.
3. Decide whether plugins are native code, hosted code, out-of-process tools, or a
   constrained combination. Native code requires explicit security/support posture.
4. Establish publisher identity, licensing, privacy, moderation, support, legal/tax,
   availability, and incident-response ownership before accepting third-party content.

## Acceptance gates

- Invalid, unsigned, revoked, incompatible, cyclic, and interrupted packages are safely
  rejected or rolled back without corrupting an installed project.
- Install/update/uninstall use atomic recoverable operations, expose provenance and
  permissions, and work with no network connection according to a documented policy.
- A security review and operational owner exist before any catalog endpoint or payment
  integration is exposed.
