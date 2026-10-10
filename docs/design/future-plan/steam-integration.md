# ZEngine — Steam Integration

**Priority:** P2 for a product distributed through Steam
**Status:** Design — no Steamworks integration, Steam manager, build feature, or cloud
adapter is implemented.
**Depends on:** product distribution ownership, save-system contract, networking policy
where applicable, privacy/security review, and Steamworks partner requirements.

## Direction

Steam is a game/product integration, not a mandatory engine subsystem. The old
`SteamManager`, compile flag, Cloud API, achievement, overlay, and initialization
examples were proposed interfaces, not code in this repository. They must not be
treated as a supported offline/no-op contract.

## Required design

- Decide supported Steamworks services per product: bootstrap, ownership, overlay,
  achievements/stats, cloud, networking, workshop, input, DLC, and dedicated-server
  behavior. Do not promise a service merely because Steamworks offers it.
- Put Steamworks types behind a small product adapter. It owns initialization,
  callbacks, thread rules, shutdown, SDK upgrade policy, and errors; generic engine
  systems should not depend on the SDK.
- Define App ID/development environment handling, build/package secrecy, partner
  configuration, user-consent/privacy/telemetry behavior, offline mode, and test
  environment policy.
- Cloud integration must use the versioned game-save format and resolve conflicts,
  quotas, corruption, cancellation, and disconnected writes explicitly.
- Platform networking, if selected, must satisfy the networking threat model and cannot
  bypass authentication, message validation, or back-pressure policy.

## Acceptance gates

- Development, offline, unavailable-client, and production paths have defined behavior
  without exposing credentials or blocking startup/shutdown.
- Callback delivery, overlay focus handling, save conflict/recovery, and SDK failure
  paths are tested against the actual partner test environment.
- A product owner and security/privacy review approve the selected services before they
  become an engine-facing dependency.
