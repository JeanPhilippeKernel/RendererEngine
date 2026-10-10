# ZEngine Plugin Example — NavMesh

**Status:** Non-authoritative example placeholder — no Plugin SDK or NavMesh plugin
exists in the source tree.
**Depends on:** a shipped, versioned plugin ABI; physics/navigation asset policy; and
editor/serialization extension contracts.

## Purpose

This file must not be read as buildable sample code. Its former CMake target, manifest,
SDK calls, ImGui panel, raw component data, and Recast/Detour integration described an
API that does not exist.

When the plugin system is supported, replace this placeholder with a tested,
version-pinned example that demonstrates only the public SDK. It must show:

- manifest/signature/provenance and dependency declaration;
- explicit lifecycle and callback cancellation;
- navigation asset cook/load/version/error behavior;
- schema-backed persistent component data and unknown-plugin scene behavior;
- ZUI/EditorSession authoring and current RenderGraph registration, with no mutable
  ECS/editor access from render callbacks; and
- build, install, upgrade, rollback, and test commands verified in CI.

Until then, navigation remains a separate product feature decision, not evidence that
plugins or Recast/Detour are integrated.
