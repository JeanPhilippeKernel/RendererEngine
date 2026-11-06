# Scene Serialization — Authoring Source and Cooked Runtime

**Priority:** P1 — foundation for persistent editor authoring
**Status:** In progress; source documents, binary codec, and source compatibility framework implemented; cook/package and standalone runtime integration remain
**Owner:** jnyfah
**Tracked by:** [#714](https://github.com/JeanPhilippeKernel/RendererEngine/issues/714)–[#719](https://github.com/JeanPhilippeKernel/RendererEngine/issues/719), [#829](https://github.com/JeanPhilippeKernel/RendererEngine/issues/829), and [#830](https://github.com/JeanPhilippeKernel/RendererEngine/issues/830). Their issue descriptions are subordinate to this current schema and source-backed lifecycle contract.
**Depends on:** actor-ecs-architecture.md, VFS stable UUID/asset registry,
component-reflection.md, import-pipeline.md
**Blocks:** shared scene settings, safe undoable lifecycle edits, Play snapshots,
scene cook, and cross-machine scene collaboration

## 1. Goal

One scene source document must load consistently on another machine, retain all
authored data through save/load, and rebuild the runtime ECS/render representation
without serializing runtime handles. YAML is the editable source format. A binary
format is a validated cooked artifact for shipping; it is not an excuse to make
source serialization depend on memory layout.

The old Tetragrama EditorSceneSerializer remains interim compatibility code. Do
not add new authored features to it. New scene data, including grid settings,
belongs in the engine scene-document serializer path.

## 2. Identity model

| Identity | Scope | Serialization rule |
|---|---|---|
| Scene UUID | One authored scene | Required in source and cooked formats. |
| UUIDComponent value | One authored entity | Required, unique, non-null, and persisted. |
| EntityID | One runtime Scene instance | Never persisted as object identity; may be an in-memory loader map key only. |
| ActorHandle | Optional Tier 1 façade | Never persisted. |
| RenderInstanceId / GPU handle | Render lifetime | Never persisted; rebuilt after loading. |
| Asset UUID | Asset registry identity | Required for asset references; source never stores native file paths. |

The currently available SceneSnapshot entity array uses runtime EntityIDs. It is
not the durable wire representation and must not be documented or tested as
cross-process identity. The source snapshot needs UUID entity records plus
versioned component and scene-setting payloads.

## 3. Authoring schema

The schema has a top-level document version and a scene record containing:

~~~text
scene UUID
scene name
schema version and optional compatible feature flags
scene settings
ordered entity records
opaque unknown-component payloads, when preservation policy is selected
~~~

Every entity record contains its UUID and a canonical ordered component map.
Names belong to NameComponent, not a duplicate ad hoc entity-name field.
Parent links encode the parent UUID or null root. Asset references encode UUIDs.

Scene settings are schema-owned values. The first required setting is:

~~~text
SceneGridSettings
  enabled
  origin
  orientation                 normalized canonical quaternion; local Y is normal
  base spacing                finite and greater than zero
  major line cadence          positive integer
  minor/major/U/V colors, fade, and display settings
~~~

The default grid is the world XZ plane: origin zero, normal +Y, U +X, V +Z.
The setting is shared by every user opening the scene. Selection, snapping,
hover, and active gizmo state remain editor-local.

## 4. Component schema registry

ComponentTypeID is a process-local allocation order and cannot be used as a file
format identifier. Each serializable component must register:

- a stable component schema key and schema version;
- YAML and binary codec functions;
- migration functions or compatible-version policy;
- whether its data is authored, runtime-derived, editor-only, or forbidden; and
- reference enumeration/patch hooks for UUID remapping and staged resolution.

Reflection describes inspector display and current-memory fields. It is related
but separate: field offsets and raw byte sizes are not durable component or undo
schema. Inspector and transaction field edits use stable field keys and codec
values.

The registry and all built-in component callbacks must be initialized explicitly
by engine startup before a serializer is offered. Static registration order alone
is not a production lifecycle guarantee.

New engine-facing API names and non-ABI string-view parameters use cstring.
External library and C ABI signatures retain their required native character
types.

## 5. YAML source format

Use yaml-cpp consistently. Earlier JSON and nlohmann::json examples are retired.
YAML should be canonical and human-reviewable:

~~~yaml
scene:
  schema_version: 2
  uuid: 550e8400-e29b-41d4-a716-446655440000
  name: MainLevel
  settings:
    grid:
      enabled: true
      origin: [0.0, 0.0, 0.0]
      orientation: [0.0, 0.0, 0.0, 1.0]
      base_spacing: 1.0
      major_line_every: 10
  entities:
    - uuid: 11111111-1111-1111-1111-111111111111
      components:
        NameComponent:
          value: Player
        ParentComponent:
          parent_uuid: null
        TransformComponent:
          position: [0.0, 1.5, 0.0]
          rotation: [0.0, 0.0, 0.0]
          scale: [1.0, 1.0, 1.0]
~~~

Canonical order is document fields, entity UUID, component schema key, then
component-defined field order. The serializer emits a normalized form on save;
it does not preserve arbitrary source formatting.

All author-controlled data has validation limits: maximum file/entity/component
count, string length, nesting depth, collection length, finite floating-point
values, legal enum ranges, UUID syntax, uniqueness, and component-specific
constraints. Missing assets have an explicit policy: editor source load can use
a visible placeholder and diagnostic; cook/shipping validation fails before
shipping an unresolved reference.

## 6. Loading lifecycle

Loading is transactional. It must not clear or mutate the currently open scene
until the candidate has passed validation and reconstruction.

~~~text
read source through VFS
  -> parse with resource limits
  -> validate document/schema/UUIDs and component payloads
  -> create a candidate Scene and UUID-to-EntityID map
  -> deserialize independent components
  -> resolve parent/entity references from UUIDs
  -> validate hierarchy cycles and component invariants
  -> create or restore actor façades after component data exists
  -> rebuild transforms, physics/editor proxies, and render bindings
  -> publish candidate as the new scene instance and increment its epoch
~~~

No consumer, including the editor renderer, may observe a half-loaded scene.
Failure preserves the previous scene and returns structured diagnostics.

ParentComponent stores runtime EntityID in memory today. Its codec serializes a
parent UUID and uses the second-stage resolver to construct the runtime handle.
RenderInstanceId and all GPU allocations are omitted and rebuilt. Actor OnCreate
ordering must be explicitly compatible with components already deserialized; an
actor cannot be its own parallel persistence format.

## 7. Saving lifecycle

Saving captures an immutable source snapshot on the main thread. Worker I/O only
owns that immutable data; it never walks a mutable ECS scene concurrently.

~~~text
capture source snapshot and canonicalize order
  -> validate complete serializable graph
  -> serialize to a sibling temporary VFS path
  -> flush and close
  -> atomically replace destination
  -> record successful saved checkpoint in EditorSession
~~~

On failure, retain the original file and current dirty checkpoint. File watcher
reloads require conflict handling when local edits are dirty; do not silently
discard one side.

## 8. Unknown data, migrations, and compatibility

The implemented policy is read-only inspection with explicit save rejection.
Unknown component keys and newer component versions remain in their owning
document; `SceneDocument::ReadOnly()` prevents source save and cooked output.
There is no opaque-data re-save mode. Missing migration routes for older known
schemas fail loading instead of pretending that the old fields are current.

Skipping an unknown component and writing the rest of the document is forbidden.

`SceneMigrationRegistry` registers pure document-tree and component-field-tree
transformations with explicit increasing source/target versions. Callbacks use
the existing engine-owned value tree (`SceneYAML::Node`), not YAML text, native
component layouts, or a current codec's restricted `SceneValue` field shape.
Inputs are deep-owned frozen snapshots; outputs must belong to the bounded
destination storage. Duplicate/branching edges, incomplete or overshooting
routes, borrowed outputs, input mutation, duplicate keys, and invalid results
are rejected. Routes contain at most 32 steps; registry capacity is bounded to
256 entries. At most two scratch trees survive a step, each bounded by
`SceneDocumentLimits`, in addition to the destination document's bounded tree.

`SceneCompatibilityService` is the shared source-load/save policy boundary.
`SceneDocument::Parse` and VFS `Read` run document upgrades, then component
upgrades, before the ordinary current-codec and UUID graph validation. Failed
upgrades discard the unpublished candidate and preserve the caller's previous
document. The editor captures the startup registry for its load worker and
rejects stale completions if that registry owner changes. Source emission and
binary encoding both use the compatibility save gate.

Startup intentionally registers no guessed historical conversions. Test-only
versioned fixtures cover the extension point; real migrations must be added
with fixtures for the actual retired schema when versions change. Cooked
binary containers and payload versions remain strict/current-only: migrate
authoring source and recook. They retain their own header/version/table
validation and reference stable schema keys, never runtime type counter values
or pointer-sized memory dumps.

## 9. Binary cook path

The next orchestration milestone is the
[scene cooker review proposal](scene-cooker.md) (#718). It specifies imported
asset closure, incremental keys, and immutable-object/manifest publication;
this section alone is not a cooker implementation contract.

Cook consumes validated YAML source and produces a runtime artifact:

~~~text
YAML source -> staged deserialize/validation -> asset validation
  -> stable-schema binary encode -> package manifest
~~~

The binary format includes magic, version, endian/feature compatibility,
bounded offsets and lengths, stable component keys, component versions, entity
UUID mapping where references require it, scene settings, and integrity checks.
It does not promise zero-parse memcpy loading when component layouts, pointer
sizes, alignment, or plugin availability can differ.

Cook fails on missing production assets, unsupported components, unresolved
references, invalid migrations, or data that cannot meet runtime constraints.

## 10. Interaction with editor history and Play

Entity/subtree/component codecs are reused by delete, duplicate, clipboard,
undo/redo, and Play snapshots. Editor history is session-local and may carry
UUID targets, but it is not persisted in a scene document.

When load, reload, or a same-world Play restoration creates a new scene instance,
EditorSession increments the epoch, clears runtime target caches, and invalidates
commands that cannot safely resolve to that instance. A successful save advances
the history dirty checkpoint; selection and editor preferences do not affect it.

## 11. Required validation

- YAML round-trip across a fresh process preserves scene UUID, entity UUIDs,
  components, hierarchy semantics, assets, and SceneGridSettings—not EntityIDs.
- Binary cook/load validates headers, bounds, stable keys, migrations, and
  malformed/truncated input without partial scene publication.
- Duplicate UUIDs, invalid references, hierarchy cycles, non-finite values,
  oversized input, and malformed components produce structured errors.
- Unknown component preservation or explicit save rejection is verified.
- Interrupted write leaves the old source intact; deterministic save produces a
  stable diff.
- Missing assets show placeholders in editor and fail the cook gate.
- Load/reload/Play Stop rebuild render and derived state before editor exposure.
- Built-in callback registration is tested during engine initialization.

## 12. Delivery checklist

- [x] Versioned SceneDocument source model and UUID entity records
- [x] Stable component/field schema registry and explicit startup registration
- [x] Built-in codecs, UUID reference enumeration, and staged hierarchy resolver
- [x] SceneGridSettings codec and validation
- [x] Immutable save snapshot, canonical YAML emitter, atomic VFS write
- [x] Transactional editor candidate load and renderer/actor reconstruction
- [x] Read-only unknown-data policy and bounded source migration framework
- [x] Stable-schema binary artifact encode/decode
- [x] Source/binary cross-process, malformed-input, migration, and editor lifecycle tests
- [ ] Scene cook asset validation, incremental fingerprints, and package manifests
- [ ] Standalone runtime publication and Play restoration integration
- [ ] Confirm macOS, Windows, and Linux CI before merge
