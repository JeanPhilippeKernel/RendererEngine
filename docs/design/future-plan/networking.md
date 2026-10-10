# ZEngine — Networking

**Priority:** P2 — only for products that require multiplayer
**Status:** Design — no network module, transport, replication schema, or netcode
runtime exists in the source tree.
**Depends on:** stable game runtime APIs, fixed simulation semantics, component schemas,
asset identity, and an explicit security model.

## Direction

Networking is a product-level architecture choice, not a generic ECS feature to add
piecemeal. The engine must first offer a small transport-independent boundary and a
versioned replication/serialization schema. Gameplay chooses authoritative-server,
rollback/lockstep, or another model according to the game; one runtime must not claim
to implement all models without their incompatible simulation guarantees.

The old document's `INetTransport`, GameNetworkingSockets/ENet/UDP backends,
replication components, RPC APIs, and "GGPO-style" modules were illustrative sketches,
not available APIs or selected dependencies.

## Required decisions

- Threat model, authentication/authorization, encryption/key handling, abuse limits,
  telemetry/privacy, and server deployment ownership.
- Transport capability interface: reliable/unreliable delivery, ordering, fragmentation,
  MTU/back-pressure behavior, reconnects, clocking, and error reporting.
- Stable replicated schemas that are separate from process-local component type IDs,
  reflection offsets, ECS memory layout, and scene-document codecs.
- Authority, input validation, tick rate, prediction/rollback policy, snapshot
  interpolation, interest management, and explicit component eligibility.
- Lifecycle rules for connect/disconnect, level transition, asset/schema version mismatch,
  packet loss/reordering, hot reload (if supported), and shutdown.

## Delivery order

1. Write the game-specific netcode requirements and validate a minimal loopback
   transport under loss, latency, duplication, reordering, and back-pressure.
2. Define versioned message schemas and an authoritative minimal session.
3. Add the chosen prediction/replication model with determinism and adversarial tests.
4. Add matchmaking, platform services, replay, and scale features only after the base
   authority/security contract is proven.

## Acceptance gates

- Malformed, oversized, unauthenticated, and stale messages are rejected without
  corrupting a world or exhausting unbounded resources.
- Version mismatch, disconnect, reconnect, packet loss, and server shutdown have
  deterministic lifecycle tests.
- Published bandwidth/CPU/memory budgets and profiling runs exist for the chosen game
  model; generic promised player counts are not documentation evidence.
