# Scene Cooker — Review Proposal

**Status:** v1 core implemented; agent-owned macOS/Linux implementation and
verification complete. Native Windows acceptance remains user-owned and pending.
**Readiness:** The synchronous public cook now connects capture, discovery,
encoding/reuse and publication, with memory/POSIX-disk integration tests.
The independent package/process gates pass locally on macOS and native ARM64
Linux, including direct built-in/custom-schema cross-host package byte checks.
The final native ownership/query-accounting audit and affected regression checks
are complete. The two installed Linux checks interrupted by the verification
environment subsequently pass after the approved Docker restart. The Windows
rooted backend and its enabled native gates previously cross-compiled, but have
not run on Windows. Three-host production acceptance is therefore still pending;
this is not a shipping game pipeline. See section 11.
**Validation ownership:** Windows testing is user-owned as of 2026-11-05; ongoing
agent verification is limited to macOS/Linux. Existing Windows CI gates remain
enabled; their results are still needed for platform acceptance.
**Date / baseline:** 2026-11-04; `develop` at `2d9f0a10` after PRs #869 and #870.
**Tracks:** [#718](https://github.com/JeanPhilippeKernel/RendererEngine/issues/718).
**Depends on:** the implemented [scene serialization foundation](scene-serialization.md).
**Follow-ups:** #829 runtime/Play integration; remaining #719 validation;
texture transcoding, archives, command-line/build integration, and shipping UI.

Section 10 records chronological implementation evidence. Its final acceptance
record supersedes older remaining-work notes; it does not waive Windows gates.

This is the proposed implementation contract for #718. The older
[cook/package sketch](cook-pipeline.md) is longer-horizon context, not an API or
file-format specification to implement verbatim. The API examples in #718 also
predate the merged static scene codec and compatibility APIs.

The [proposed v1 contracts](scene-cooker-contracts.md) resolve the exact manifest,
canonical fingerprints/material output, API/result ownership, adapter callbacks,
input lease, VFS capabilities, resource policy, and diagnostic interfaces.
They take precedence over this page's high-level descriptions. The accompanying
JSON Schema and wire fixtures are design artifacts, not a cooker implementation.

## 1. Outcome and scope

Given an explicit set of saved scenes, an immutable imported-content library,
and a supported target profile, produce a deterministic loose-file **scene
package**: binary scenes, their validated asset closure, and one manifest.
Publishing the manifest makes the complete new package visible at once.

```text
Saved scene YAML       Imported project content + metadata
       |                              |
       v                              |
 Read + migrate                       |
       |                              |
       +----------> Validate dependency closure
                              |
                              +-- Invalid --> Stop; keep old build
                              |
                            Valid
                              |
                Check complete input fingerprints
                              |
                 Reuse verified objects, or encode
                              |
                  Publish immutable data files
                              |
                  Atomically replace manifest
                              |
                     New scene package ready
```

The cooker prepares content; it neither compiles game code nor starts a game.
A later build/package step combines its outputs with the executable, shaders,
and other runtime resources. The first scene package is not, by itself, a
complete runnable game distribution.

Included: a synchronous headless library API, typed reference discovery,
bounded asset inspection/staging, incremental fingerprints, manifest parsing,
safe publication, and tests. No window, GPU device, live ECS scene,
`AssetManager` singleton, or editor session is required.

Excluded: raw FBX/glTF reimport, BC7/ASTC selection, shader cooking, audio/script
cooking, `.pak`/ZIP formats, executable builds, Build Game buttons, a `ZCook`
executable, standalone scene publication, and Play restoration. Unsupported
referenced content fails explicitly rather than being skipped.

The initial root set is the explicit scene set and its declared dependency
closure only. Assets loaded later by an arbitrary name/UUID that does not
appear in that closure are not discovered or implicitly included. Supporting
those loads requires a reviewed explicit additional-root list or asset-label
policy in a follow-up; do not scan/copy the entire library as a hidden fallback.
Known undeclared dynamic dependencies fail when they can be identified; this
milestone cannot certify dependencies hidden in game code. Engine-owned default
materials, fallback textures, shaders, and similar resources belong to the
runtime distribution, not this project-asset manifest. The later game packager
must inventory and verify those resources before claiming a runnable package.

## 2. What exists, and what is missing

| Existing code | Consequence for this design |
|---|---|
| `ECS/SceneDocument.h`, `SceneCompatibility.h` | Reuse source parsing, migrations, validation, and read-only rejection. Do not invent another YAML serializer. |
| `ECS/BinarySceneCodec.h` | Reuse static `Encode`/`Decode`; no new scene wire layout. |
| `Core/VFS/Registry/AssetRegistry.h` | Copy records with `TryReadRecord`; do not retain borrowed records across work. Runtime loaded state is not proof of cook readiness. |
| `Core/VFS/Registry/DependencyGraph.h` | Has copy-out traversal, but production import paths do not populate all asset edges. It cannot prove dependency completeness. |
| `Core/VFS/Meta/MetaFileData.h` | Metadata uses 64-bit rapidhash, not the old document's `LastSourceSha256`. Full settings are on disk, not in the runtime record snapshot. |
| `Importers/AssetCodec.h` | A bounded VFS mesh reader exists. Material decoding is permissive/native-file based; identity extraction alone is not full validation. |
| `Importers/MaterialAssetDocument.h` | The shared strict CPU material reader and canonical emitters are used by the material cook adapter; the legacy material reader is not used for cooking. |
| `Importers/AssetTypes.h` | Meshes contain material UUIDs; materials contain texture UUID/path pairs. Material JSON may also contain native import provenance. |
| `Rendering/Scenes/RenderScene.h` | Sky environment references live in scene settings, outside component reference enumeration. |
| `Core/VFS/VFSFileIO.h` | Reuse the atomic sibling-temp write protocol, extending it for immutable publication rather than duplicating it. |
| `Core/VFS/IVFSContext.h` | Exclusive creation and no-replace rename exist; multi-file transactions, writer leases, and directory-sync guarantees do not. |

`Core/Hash/SHA256` supplies inline streaming state and canonical digest values
using the portable `amosnier/sha-2` dependency pinned to
`565f65009bdd98267361b17d50cddd7c9beb3e6c` (reviewed 0BSD license option).
Known-answer, streaming/padding-boundary, and strict hexadecimal tests accompany
the wrapper. Capture hashing and metadata/material local-content keys use it;
typed schema/recipe/asset/graph/scene fingerprints and the canonical manifest
writer/strict ledger reader, verified object-reuse coordinator, and synchronous
transactional publication session now exist. End-to-end run orchestration and
its complete budget/preflight integration remain implementation work.
This dependency introduces no mandatory system OpenSSL requirement.

## 3. Authoritative inputs and ownership

### Imported content is the boundary

For this milestone the inputs are the project's `.zemesh`, `.zematerial`,
texture images, and environment-map source/artifact metadata. They are the
authoritative **imported library**, not instructions to reopen a developer's
original GLB in `Downloads`.

Native `source_path` / `source_material.path` values are provenance only. They
must not be required on another machine, followed as dependencies, emitted in
runtime manifests, or copied into cooked materials. Missing original import
sources do not invalidate an otherwise complete imported library.

Persisted effective import settings participate in fingerprints. Changing
those settings never silently reapplies an importer during scene cook: inputs
whose format requires regeneration must first be reimported. Current mesh and
material metadata does not record every historical import option. The cooker
cannot reconstruct those options or certify freshness against original raw
sources; that is a future import-receipt/reimport responsibility, not a guessed
migration inside the cooker.

### A stable catalog, not the editor's live cache

Build a private owned `CookAssetCatalog` from configured logical asset roots
and **read-only** sidecars. A headless caller must be able to build this catalog
without previously dragging anything into a viewport. Reuse registry identity
and lookup rules, but do not call scanner/import paths that create sidecars,
mint UUIDs, upload assets, or mutate the project.
Catalog construction and capture run through the selected
[input provider and lease](scene-cooker-contracts.md#6-input-lease-and-capture-contract),
not a caller assertion that a live mutable VFS happens to be stable.

Missing/corrupt sidecars, duplicate UUIDs, embedded/sidecar UUID disagreement,
and ambiguous asset locations fail. Paths must remain within declared input
roots; reject traversal, external native paths, and unsupported path aliases.
Do not silently choose the first matching UUID. An environment map is a
distinct cook asset kind even though the current runtime `AssetType` enum
has no environment-map member.

The owned catalog/captures, scene schemas, and migration callbacks stay immutable
for the call. Live input files are excluded from mutation through successful
capture sealing; afterward the run uses only its owned immutable generation.
#718 does not promise a transactional snapshot of an arbitrarily changing
editor workspace. A future editor/CLI integration must supply a
quiescent or staged immutable library. During capture, detect changed inputs
and fail before publication; size/mtime alone is not an identity check.
Hash, validate, and stage the same captured bytes. No second unchecked open
may substitute different bytes between validation and publication.
For scenes, capture bounded bytes once, fingerprint that capture, and call
`SceneDocument::Parse` with the schemas/migrations; do not hash one VFS read
and then ask `Read` to parse a potentially different file.

The initial supported input mode is an offline, quiescent library or a private
staged copy whose owner excludes importers and other writers. The caller owns
that exclusion from before catalog construction through successful
`CookInputLease::Seal`, after all scene, sidecar, and reachable artifact bytes
have been captured and discovery is complete. `Seal` verifies source/capture
digests and catalog/root identities while exclusion is still held; it must
succeed before any package object is staged/installed, any manifest is
published, or a no-op returns success. No capture/enumeration is allowed after
sealing. Per-file hashing alone cannot
prove that separately captured files belong to one import generation. The
context must explicitly require this input-stability contract; known mutable
sources are rejected, and detecting a change aborts before publication rather
than silently retrying individual files into a mixed generation. Tests must
exercise changes to both artifacts and sidecars during capture.
After sealing, releasing exclusion is cleanup only and cannot newly fail the
cook; later source changes cannot affect owned captures. Never defer the first
verification to lease destruction or any post-commit action.

Disk-backed captures have a separate scratch-storage lifetime. The orchestrator
acquires a scoped scratch-root lease before creating its private run directory
and passes that pinned lease/run to the input provider. Keep the scratch pin
through every captured-file read, file-wrapper release, and owned cleanup
attempt, even if source exclusion/input lease ended after sealing. Use retained
read-only handles or reopen only through the same pinned scratch root; never
return to live mount resolution. Remount/unmount/backend shutdown must not
invalidate captures or redirect cleanup to a replacement tree.

Before editor/CLI integration, specify and review the concrete mechanism that
enforces this ownership: an import-publication exclusion protocol or a staged
snapshot with a verified capture receipt. An advisory flag, timestamp check,
or watcher pause is not sufficient. This milestone does not claim that it can
freeze an arbitrary live workspace.

## 4. Complete, typed dependency discovery

Add a document-wide reference visitor to `SceneDocument`; keep the existing
component-specific visitor usable by editor callers. Enumeration must include
all supported registered components **and** scene-owned settings. Do not let
the cooker access private YAML nodes or hard-code a second scene schema.

Each asset occurrence needs the entity UUID (or scene-settings scope), stable
schema key, field key, referenced UUID, and expected asset kind. Extend the
schema reference description additively so custom components can declare typed
references too. Untyped asset references cannot pass a shipping cook merely
because their UUID happens to exist. Entity references remain the existing
scene validator's responsibility.

Build a private forward graph from semantic references in validated content:

| Content | Edges / policy |
|---|---|
| Mesh component | Mesh UUID. |
| Material component | Material UUID, independently of mesh submaterial references. |
| Sky settings | Every non-nil environment-map UUID, including a retained value in an inactive sky mode. Nil keeps the documented renderer fallback. |
| Mesh artifact | All non-nil submesh material UUIDs. Embedded node hierarchy stays with the mesh artifact, not a runtime hierarchy slot. |
| Material artifact | Every non-nil texture UUID in all five slots. Validate the UUID/path pair against the catalog; do not resolve a mismatched UUID by path. |
| Textures / cooked environment maps | Leaves for the initial adapters; unsupported extra dependencies fail. |

Nil references are accepted only where the owning format explicitly defines a
default: absent texture slots, nil sky environment, and the current mesh's
default-material case. A non-nil UUID that cannot resolve is always an error;
there is no viewport-placeholder fallback in cook.

Deduplicate nodes by `(kind, UUID)` and exact edges by parent identity, stable
slot key, and child identity. Different slots referencing the same child remain
distinct semantic dependencies; reachability need only visit that child once.
Retain occurrence provenance separately so sharing does not erase useful
errors. Use iterative traversal and bounded cycle detection. A cycle is an
error; `CollectCascade` over the live graph is not the discovery algorithm.
Canonical tie-breaking is by kind key, UUID bytes, and stable field key,
never discovery order or runtime handles.

## 5. Asset adapters and target support

Use an explicitly registered adapter table with stable kind keys and recipe
versions. An adapter validates a bounded captured input, enumerates its direct
dependencies, and stages deterministic runtime bytes. No static registration,
`std::function`, runtime rendering callbacks, or opaque passthrough for an
unknown asset type. Extend shared asset-codec readers instead of maintaining
another material parser inside the cooker.

Initial adapters:

- **Mesh:** reuse the shared captured-byte `ReadMeshAsset` decoder (also used by
  `ReadMeshAssetFileVFS`) and its budgets/identity checks. An allocation-free
  layout measurement sizes the arena; it is not a substitute for full payload
  validation. Stage the validated current mesh bytes. Its current v1 format is
  64-bit little-endian, not a newly portable mesh ABI.
- **Material:** reuse the strict bounded captured-byte `MaterialAssetDocument`
  reader, with shared validation for versions, duplicate keys, finite numeric
  values, legal alpha modes, and texture pairs. Serialize canonical current material
  JSON without native provenance. Preserve authored alpha behavior; never infer
  BLEND/MASK from pixels or revive the removed material-source upgrade code.
  Missing/unsupported legacy material contracts require an explicit upstream
  upgrade/reimport, not a cook-time guess.
- **Texture:** validate supported encoded images and bounded decoded dimensions/
  memory on CPU, then stage their encoded bytes unchanged. This first target
  still decodes images at runtime; it does not claim GPU-native texture cooking.
- **Environment map:** resolve the stable source UUID to its recorded `.zenvmap`,
  verify the current header, source-hash relationship, and complete bounded
  payload. Add a VFS reader where current native-file helpers are insufficient.
  No rendering/device initialization is needed. A missing cache is a reimport
  diagnostic, not permission to generate environment lighting on a GPU.

Stage leaf assets before dependents. Rewrite material texture paths to the
package-relative paths of their staged blobs; UUIDs remain authoritative.
Canonical material bytes therefore also depend on the cooked texture paths.
The later runtime loader must mount these paths relative to the package root;
current editor file loaders are not assumed to understand the new package.

First target: versioned `desktop-vulkan-le64` using the existing supported
64-bit little-endian formats and software image decode. Reject other profiles
until their adapters/capabilities are tested. Do not promise universal BC7/ASTC
support or silently use host CPU layout as a target description.

## 6. Incremental keys and determinism

Use SHA-256 for cook fingerprints and object identity. Existing rapidhash is
still the metadata stale-detection hint, and scene CRC32 remains wire-integrity
checking. Neither replaces the full cook key.

```text
Scene input key = SHA256(
  domain/version tag,
  exact captured source bytes hash,
  canonical schema-set fingerprint,
  cooker/codec/migration recipe revision,
  canonical reachable asset graph fingerprint,
  target profile + normalized cook settings
)
```

Hash typed, length-delimited values with a declared byte order. Never hash raw
C++ structs, callback addresses, `ComponentTypeID`, slot handles, timestamps,
unordered-map iteration, absolute checkout paths, or build timestamps.

The schema-set fingerprint uses canonical registered keys/versions and stable
field classifications, not just a count. Hash the full registered set initially
for conservative invalidation. An explicit reviewed recipe revision covers
codec/migration implementation changes that do not alter a public schema
version; it must not be guessed from function pointers.
Exact domains, byte encodings, and array ordering are specified in section 3
of the [v1 contracts](scene-cooker-contracts.md#3-exact-fingerprint-encoding).

Each asset input includes UUID/kind, a content fingerprint, relevant normalized
sidecar settings/importer identity, adapter/format revision, and canonical
direct edges. Binary/image content fingerprints cover the exact captured
bytes. Material fingerprints cover validated semantic fields with native
provenance removed, so moving an import-source path cannot affect the package
key. A raw material digest may still detect changes during capture, but is not
the semantic reuse key. Exclude import timestamps and native provenance.
Reject duplicate/truncated/unsupported semantic metadata instead of relying
on permissive `MetaFileIO::Read` to silently discard settings.

Per-asset reuse keys must include resolved dependency results, not only direct
edge UUIDs. Compute them in dependency-first order:

```text
Asset input key = SHA256(
  asset-key domain/version tag,
  kind + UUID + local content fingerprint,
  normalized semantic metadata + importer/adapter/format/recipe revisions,
  target profile + normalized cook settings,
  sorted direct dependency records(
    stable edge/slot key, child kind + UUID, child input key,
    child output digest + size + package path + format revision
  )
)
```

Leaves have an empty dependency list. A changed texture output changes its
object path and therefore its parent's material key and canonical bytes, even
when the material source and texture UUID are unchanged. Include every child
result the adapter consumes. A scene-wide graph fingerprint does not substitute
for this per-asset invalidation rule. Conservative invalidation may regenerate
identical bytes; content addressing still deduplicates those outputs.

Walk and fingerprint dependencies even when scene YAML is unchanged. An edited
texture must invalidate its material/package inputs without requiring a live
registry event. Per-object reuse is allowed, so that edit need not rewrite an
identical binary scene. Changing source YAML, effective settings, schema/recipe,
dependency content, or target invalidates the corresponding cook result.

Reuse only when the input key, manifest version, target, output byte count,
and full output digest match and all referenced objects exist. A missing object
can be regenerated. An existing corrupt content-addressed object is an explicit
repair error, never a cache hit or an in-place overwrite. `ForceRebuild` bypasses
input-key reuse, not validation, immutable-object protection, or writer locking.
Recovery is described in section 8; it is not an automatic destructive repair.

## 7. Output and manifest

Use loose content-addressed files first; archive offsets are not part of this
milestone. The request supplies a complete scene set. Removing a scene from the
request removes its entry from the next manifest; this is not per-scene upsert.

Require at least one scene. An empty `Sources` span is an invalid request, not
permission to clear a published package; an explicit clear-package operation
would require a separate reviewed contract. Reject duplicate normalized logical
source paths and duplicate scene UUIDs, including different files with the same
scene UUID. Validate request paths and all captured scene identities before
staging any output objects. Never silently deduplicate requests or resolve a
collision by first/last entry wins. These failures preserve the prior package.

```text
<output root>/desktop-vulkan-le64/
  manifest.json                     <-- only mutable publication pointer
  objects/<sha256>.zescenebin
  objects/<sha256>.zemesh
  objects/<sha256>.zematerial
  objects/<sha256>.png               (or another supported image encoding)
  objects/<sha256>.zenvmap
```

The manifest uses the strict versioned
[v1 JSON Schema](scene-cooker-contracts/manifest-v1.schema.json), with exact
field names, ordering, compatibility, and semantic rules in the
[wire contract](scene-cooker-contracts.md#1-manifest-v1). Its records contain:

- Manifest/recipe versions, target profile, and normalized settings fingerprint.
- Scene records sorted by scene UUID: logical source path, source/content input
  key, output object path/digest/size, and sorted direct asset roots.
- Asset records sorted by kind/UUID: stable identity, format/adapter revision,
  input fingerprint, output object path/digest/size, and sorted direct dependencies.

Paths are UTF-8 package-relative paths with `/` separators for objects, and
project-logical paths for source diagnostics. No host-native paths, saved
runtime handles, timestamps, build-run UUIDs, or raw `.meta` contents are emitted.
The manifest is a ledger/lookup map, not a duplicate serialized scene graph.

Logical source/package paths and paths used inside output/scratch leases use
the strict owned `VFSRelativePath` from the
[path contract](scene-cooker-contracts.md#logical-paths-and-slots). Parse their
original length-delimited UTF-8 bytes before any normalization; reject absolute,
drive/UNC, backslash, empty/dot/traversal components, embedded controls, and
excess bytes/components without rewriting them. Do not validate by first using
`VFSPath::Parse`, which adds a leading slash and normalizes away invalid syntax.
Existing context-level request/root/result paths remain `VFSPath`; conversion
to a leased relative identifier is explicit and preserves its declared spelling.

Bound its parser before allocating the full DOM; reject duplicate fields,
duplicate identities, invalid kinds/digests/paths, unresolved manifest edges,
unexpected versions, excess depth/counts/bytes, and object paths outside the
package root. UUID/digest widths and regex boundaries cover the entire value:
trailing LF/CRLF and prohibited embedded controls are not accepted as part of
an identifier or path. The canonical final LF follows the JSON document, not a
string value. Missing manifest means first cook. Syntactically valid JSON with
one unambiguous current-version tag but invalid known-v1 shape/semantics may be
a cache miss with a warning, never a source of cleanup paths or trusted object
locations. Invalid JSON/duplicate keys, missing/ambiguous versions, and unknown
or newer manifest/recipe/adapter formats fail closed. Section 1 of the v1
contract defines the exact compatibility-check order and diagnostics.

### Reader snapshot contract

A package reader opens and captures the bounded manifest once, closes the file,
validates it, and retains an owned immutable lookup snapshot for the entire
load/session. All scene and asset UUID resolutions use that snapshot. Do not
reread `manifest.json` for individual dependencies or mix a scene from one
snapshot with asset mappings from another. A new manifest becomes visible to
an existing consumer only through an explicit new snapshot/load transaction.
Immutable old objects remain available; no garbage collection runs in #718.

Concurrent publication must preserve reader consistency on every supported
backend. Windows ordinary opens retain `FILE_SHARE_READ`; the explicit read-only
`AllowReplace` mode adds `FILE_SHARE_DELETE`, without permitting in-place writers.
Both the ordinary disk backend and retained-root implementation supply this mode.
Test a reader holding the old manifest open during publication, as well as
readers capturing it just before/after replacement. Each successful capture
must see one complete old or new manifest, never partial or mixed bytes.

### Deployment follows the manifest, not the object directory

The output root may retain objects from earlier cooks and interrupted attempts;
it is not a directory tree to recursively copy into a game distribution. The
future packager must pin one validated manifest and copy that manifest plus
exactly its referenced scene/asset object set into fresh deployment staging,
verifying each object's digest/size. Do not include unreferenced objects,
temporary files, lease metadata, or unrelated files. Publish the staged package
only when its referenced closure is complete. Engine-owned runtime resources
are a separate explicit input to that later packaging step.

Removing a scene/asset from the active manifest must remove it from the next
distribution even if its old object remains in the cook cache. This rule does
not authorize deletion from the shared cook root or invalidate pinned readers.
Implementing the game packager remains outside #718.

## 8. Publication and failure semantics

First acquire the scoped
[publication-root lease](scene-cooker-contracts.md#7-shared-vfs-publication-capabilities),
which atomically pins the backend/mount/root and verifies required capabilities.
Every output operation uses its rooted operations, not live mount resolution;
its path inputs are strict `VFSRelativePath` values, not normalized `VFSPath`.
Retain the lease through writer-lock removal and owned-file cleanup. Remount/unmount
or backend shutdown cannot invalidate or reroute the pinned root. Unsupported
pinning fails closed; a backend identity alone is not a lifetime guarantee.

One cooperating writer per target output root. Acquire a separate writer lock
through the pinned root's `CreateDirExclusive` before reading/updating its
manifest; concurrent attempts return a structured busy diagnostic. No
check-then-create lock. Keep lock/run
metadata outside deterministic outputs. A process-crash lock requires explicit
operator recovery after confirming no writer is active; do not steal it on a
timeout or delete an arbitrary pre-existing directory.

1. Validate request paths, target, inputs, and budgets. Input, output, and scratch
   roots are physically disjoint; reject aliases/symlinks that defeat that
   separation. Acquire the scratch-root lease before creating the private run
   directory and before input acquisition/capture; unsupported pinning fails
   before any scratch files are created.
2. Capture/validate scenes, typed dependency closure, and fingerprints; then
   successfully seal the input lease while exclusion is held. No objects are
   staged/installed and no no-op succeeds before sealing. Existing published
   files remain untouched. Refuse mutable or unsupported input sources.
3. Stage each missing object to an exclusively created unique sibling temporary
   file; check full write, flush, close, and digest/size of the staged bytes.
4. Publish that immutable object with `RenameNoReplace`. If a matching name
   already exists, verify its bytes/digest; never blindly replace it.
5. Finish and validate the complete manifest, verifying every object it refers
   to. Prepare the owned caller result and all storage needed for success and
   bounded post-commit warnings before proceeding. If nothing changed and the
   existing package was verified, leave it alone and return the prepared result.
6. Write/flush/close a unique sibling manifest temporary, then atomically replace
   `manifest.json`. **This rename is the commit point.**
7. Publish the prepared caller result through a non-allocating, non-failing
   transfer. Clean only owned temporary files and remove the owned writer lock
   through the still-pinned root, then release the output-root lease. Input-lease
   release performs no new validation. Release all captured-file wrappers and
   finish owned scratch-run cleanup attempts through the scratch lease before
   releasing its pin; do not derive scratch lifetime from input sealing.
   Cleanup trouble after commit is a bounded best-effort warning, not a false
   failed-cook result claiming the old manifest is still authoritative. No
   required validation, callback, or result allocation remains after commit.

Before step 6, any failure preserves the old manifest and every valid object it
references. New unreferenced immutable objects are harmless orphans. Do not
delete them on the assumption that no other manifest may refer to them;
garbage collection is a separate reviewed operation.

VFS must expose/verify the required atomic-replace, no-replace, exclusive-create,
and same-backend guarantees for the pinned destination. Support the tested disk
and memory implementations; unsupported backends fail closed, never copy/delete as
a rename fallback. Extend the shared atomic-write helper with explicit immutable
publication mode rather than writing a second protocol. Disk `Close()` must
surface native close failures; its current always-OK result is not sufficient
to test that guarantee honestly.

This provides atomic visibility and process-interruption safety, **not a claim
of survival across sudden power loss**. The current VFS has no parent-directory
sync contract. Power-loss durability needs an explicit directory-sync extension
and platform tests before it can be advertised. File `Flush()` alone is not
multi-file durability, and watcher import-publication scopes are not locks.

### Operator recovery

Diagnostics identify the exact output root, failure stage, and lock or object
path, without treating untrusted manifest paths as repair targets. There is no
automatic lock stealing or corrupt-object deletion, and no repair CLI is
claimed by this milestone.

- **Stale lease:** stop/quiesce all potential writers to that output root and
  confirm the former owner has exited. With exclusive maintenance access,
  inspect the recognized lease metadata and explicitly remove only that lease;
  leave the manifest and objects intact, then retry. If ownership or exclusion
  cannot be established, use a new output root instead.
- **Corrupt object:** preserve the old root for diagnosis and rebuild from the
  authoritative inputs into a new, empty output root. Verify the entire new
  package before explicitly switching consumers to it. `ForceRebuild` does not
  overwrite the corrupt object. In-place quarantine/repair and garbage
  collection require a separate reviewed maintenance protocol.
- **Interrupted write or disk full:** restore capacity, resolve any stale lease
  as above, and retry. Ignore unreferenced objects; do not recursively delete
  temporary-looking paths. Cleanup removes only files whose ownership is known.
  If the prior package was already corrupt, failure preservation does not make
  it valid; report that distinction rather than claiming a usable fallback.

## 9. Proposed API shape, limits, and diagnostics

Place orchestration under `ZEngine::Cook`, separate from ECS codec ownership.
The [public API contract](scene-cooker-contracts.md#4-public-orchestration-api-and-result-ownership)
defines the context, inline result, and synchronous signature. Names are new,
not existing APIs. The selected request is:

```cpp
struct SceneCookRequest
{
    std::span<const Core::VFS::VFSPath> Sources = {};
    Core::VFS::VFSPath OutputRoot              = {};
    CookTarget Target                         = CookTarget::Invalid;
    bool ForceRebuild                         = false;
};
```

`SceneCooker::Cook` consumes the request plus an explicit context containing
the leased input provider, output/scratch VFS services, schemas, optional
migrations, adapter registry, checked allocator, and limits. The recipe revision
is an explicit reviewed engine constant, initially 1. Use `VFSResult<void>` and
the bounded typed diagnostics contract, not pointers to instances of the static
`BinarySceneCodec`/`SceneCompatibilityService`.

The raw output/scratch VFS pointers are used to acquire root leases during
setup, not for subsequent live-resolved file operations. `CookCaptureContext`
borrows the orchestrator-owned `VFSScratchRootLease` and a validated private-run
`VFSRelativePath`, rather than an unpinned scratch context/root pathname. Output
and scratch leases share pin infrastructure, with capability requirements
appropriate to each; scratch does not require publication-only rename/read modes.
The run must acquire input `Rooted` authority before preparing capture storage:
check input/output, input/scratch, and scratch/output physical separation while
input exclusion and all pins are held, then exclusively create the private run
directory and transition the input lease to `Active` with `PrepareCapture`.
Root acquisition alone does not discover the catalog or write scratch files.
After all captures and leases are released, explicit provider `ReleaseStorage`
frees retained run buffers before the checked allocator and scratch pin die;
the caller-owned provider may outlive those per-call services. Its fixed-state
size, including its frozen library owner and configured root bindings, is reported
relative to the run's covered allocator chain for a single run charge, not hidden
in routed bytes. Pre-owned binding storage includes upstream headers, size-class/
alignment slack and native page rounding. A fully covered pool does not pay for
its child bindings twice; separate allocator state and retained pools do count.
Retained VFS root operations/backend pools/mapping tables remain separate
shared-domain charges.
Target-directory setup also stays rooted: create `<target>` through the retained
output-parent pin after domain preflight, then `AcquireChildRoot` to obtain the
same-access target authority. Child acquisition creates no directory and performs
no live context lookup. Each child retains its own bounded mount pin and physical
anchor independently of its parent, including after context/provider teardown.
Budgeted context acquisition and borrowed input-root reservations now share a
per-call VFS memory policy. It counts each root state, but each retained backend
pool/provider and mount table only once across input/output/scratch/children.
Owned-memory snapshot/write buffers use that precharged fixed pool; they do not
silently bypass the budget or get charged again per open. Unknown backend memory
bounds fail closed. Its engine hash index is inline for common runs and grows
geometrically through checked reusable workspaces for larger domain sets.

Sources are borrowed only during the synchronous call. The caller's result owns
inline path/digest/count fields, not a manifest DOM or arena-backed strings;
no work remains queued after return. Failure preserves the prior result and
releases all run-owned allocations. Prepare the fixed result before the commit
rename; assigning it afterward must not throw or allocate. A verified no-op
succeeds with `Published == false` and the existing canonical manifest digest.

The first synchronous API is explicitly **non-cancellable** and has no progress
callback contract. Callers must not interpret an external cancellation request
as rollback of a running cook. Process termination instead follows section 8's
interruption/recovery rules. A future cooperative cancellation API must define
bounded safe checkpoints and return cancellation only before the manifest
commit; once committed, the result is success even if cancellation arrives.
Review callback lifetime/reentrancy and cancellation tests before adding it.

The catalog index retains bounded UUID/kind/path entries; load full metadata
only for reachable assets. Keep source documents and decoded assets one at a
time, using owned temporary storage where needed rather than retaining every
scene/mesh/image in memory. Count that temporary disk use against input/output
budgets too.

The [resource contract](scene-cooker-contracts.md#8-resource-policy-and-diagnostics)
defines exact fields, hard caps, and accounting. Initial limits include:

- Existing `SceneDocumentLimits` per scene; at most 256 requested scenes.
- At most 65,536 catalog entries, 8,192 reachable asset identities, and 65,536
  dependency edges and reference occurrences before deduplication.
- At most 8 MiB manifest bytes and 64 KiB per sidecar, with explicit parser
  depth/string/collection limits; material input at most 1 MiB.
- At most 512 MiB encoded input and 512 MiB decoded output per asset, 8 GiB
  total captured input, and 16 GiB total staged output. `MaxWorkingBytes` must
  be explicitly positive; its zero default is invalid, not an implicit 768 MiB
  allocation. Fitting per-asset limits does not imply fitting that aggregate cap.
- Hash/copy via a fixed 64 KiB stream buffer; process assets sequentially first.
  No retained decoded texture/mesh array for the entire package. Image adapters
  preflight dimensions/overflow and enforce decoder allocation limits as well.

### Working-memory budget: 768 MiB is an example, not a default

The earlier 768 MiB estimate is an example ceiling for one cook, **not a minimum RAM
requirement, an upfront allocation, a measured peak, or a game-runtime budget**.
It provisionally leaves room for one large decoded asset within the 512 MiB
per-asset cap plus the remaining live cook state. The remaining 256 MiB is not
a measured overhead allowance or a guarantee that every such asset fits.
No benchmark currently justifies 768 MiB as the correct default; confirm or
revise any later convenience preset using representative/adversarial fixtures.
For v1, the caller must explicitly supply its ceiling rather than inherit an
unmeasured default. Inputs exceeding it fail safely before commit.

Encoded file size is not decoded memory use. For example, an 8192 x 8192 RGBA8
image needs 256 MiB for pixels alone (`8192 * 8192 * 4`), irrespective of how
small its PNG is. Decoder scratch, captured encoded bytes, the catalog/graph,
parsers, and output/result buffers are additional live allocations. Keeping
512 MiB of encoded bytes and 512 MiB of decoded bytes simultaneously would
already exceed 768 MiB before those overheads; it is not an allowed execution.

Account all simultaneous cook-owned allocations, including general-heap and
third-party parser/decoder allocations, alignment/capacity overhead, captured
buffers, catalog/graph state, diagnostics, and prepared manifest/result storage.
Use budget-aware allocators/hooks or proven bounded implementations; an adapter
whose allocation behavior cannot satisfy the bound is unsupported. Check sizes
and arithmetic overflow before allocation, propagate allocation failures, and
fail before commit without corrupting prior results or published files.

Allocate on demand, release per-asset scratch promptly, and stream immutable
captures through owned temporary disk storage when needed. Do not retain a
whole package in RAM or reserve the full ceiling solely because it is available.
The ceiling and per-asset limits are configurable; a smaller budget may reject
large assets cleanly, never silently exceed the limit or reduce asset fidelity.
Temporary disk storage counts against its own existing budgets, not RAM.

Report tracked peak live bytes by category and measure process peak RSS
separately on supported platforms. This is not a total-process RSS guarantee:
caller/editor baseline memory, shared libraries, and OS file cache differ from
cook-owned allocations. Tests must verify accounting and failure behavior;
changing only this non-semantic resource ceiling must not change the cooked
bytes of an input that successfully fits both configurations.
Resource ceilings are execution policy, not semantic cook settings; exclude
them and memory telemetry from fingerprints and the deterministic manifest.

Use typed cook diagnostics, not a long string squeezed into the current
192-byte scene message: code, stage, source logical path, scene/entity UUID,
schema/field key, dependency UUID/kind, and underlying VFS error. Report the
dependency chain for transitive failures. Bridge scene-codec messages into this
bounded sink without changing their meaning. Cap entries and retain a dropped
count; never report success merely because the diagnostic buffer filled up.

## 10. Validation and delivery slices

Test categories below require multiple cases, not one smoke test each. Each
pre-commit failure test verifies that prior manifest bytes, referenced objects,
and caller result remain unchanged.
After commit, assert the new package/result remain authoritative and cleanup
failures produce success with bounded warnings instead of claiming rollback.

1. **References and catalog:** all component/settings references, custom typed
   references, fresh-process discovery, identity disagreement, duplicate UUIDs,
   missing sidecars, wrong kinds, nil/default policy, and no viewport dependency.
   Reject empty scene batches, duplicate normalized source paths, and different
   scene files with the same scene UUID before staging any output objects.
   Inject source/sidecar/catalog changes immediately before sealing: assert
   failure before object staging, manifest replacement, or successful no-op.
   After sealing and exclusion release, change originals and verify output
   still uses the captured generation. Reject capture or repeated seal attempts
   after sealing/failure; lease destruction must not revalidate inputs.
   Test the strict relative parser on raw spellings: reject absolute/drive/UNC
   paths, backslashes, repeated separators, dot/traversal components, controls,
   invalid UTF-8, and overflow without normalization. Accept valid Unicode and
   exact 255-byte/32-component limits; reject 256 bytes/33 components. Keep
   existing context-level `VFSPath` behavior unchanged.
2. **Adapters and closure:** mesh -> material -> texture plus sky fixtures;
   malformed versions/payloads, duplicate JSON fields, non-finite values,
   bad texture pairs, corrupt images/envmaps, cycles, shared nodes, and budgets.
3. **Incremental/deterministic:** exact repeat equality; changed leaf asset,
   effective import settings, schema/recipe, target, source, and removed scene;
   permuted catalog/type-registration order; moved checkout/Unicode paths;
   missing/corrupt cached objects; explicit forced rebuild.
   In particular, change only a texture's bytes while preserving its UUID and
   material source: assert the material key/path rewrite changes and no stale
   texture path survives. Verify unrelated objects remain reusable.
   Use one checked-in golden package specification on macOS/Windows/Linux:
   identical captured input bytes (including YAML line endings), schemas,
   recipe, target, and semantic settings must produce identical manifest bytes
   and object paths/digests/sizes across hosts. Verify fixture input hashes so
   checkout transformations cannot masquerade as a cooker determinism failure.
   Per-host repeatability or matching decoded semantics alone is insufficient.
4. **Publication:** fault-inject open, partial write, flush, close, object rename,
   manifest write/rename, cleanup, lock contention, unsupported backends, and
   malicious manifest paths. Test a failure after new objects exist but before
   manifest replacement. Assert no in-place update of a referenced old object.
   Include disk exhaustion, real process termination/restart around the commit
   point, stale-lease recovery, and rebuilding a corrupt package into a new root.
   Post-commit cleanup trouble must report committed success with a warning.
   Inject allocation failures during result preparation before commit and prove
   result handoff after the rename does not allocate or fail.
   Race root acquisition and capability checks with remount/unmount/shutdown;
   attempts must be rejected/deferred or acquisition fails before mutation.
   Retain the backend/root through temp writes, renames, and lock cleanup;
   verify no operation is rerouted and no dangling backend remains. Reject
   cross-root rename/path escapes and a backend that reports flags without
   implementing real pin ownership.
   Apply the same remount/shutdown races to scratch storage during capture,
   post-seal encoding, and cleanup. Destroy the input lease after sealing while
   retaining captures, then read them successfully from the original pinned
   scratch root. Cover retained handles and pinned reopens. Verify all wrappers
   and cleanup attempts finish before scratch-pin release, unsupported pinning
   creates no run files, and failed cleanup never targets a replacement mount.
5. **Integration:** memory and real-disk fixtures on macOS/Windows/Linux; separate
   process reads only the published manifest/objects, checks all hashes, decodes
   binary scenes, and verifies UUID/component/settings semantics. No YAML fallback
   or original native import sources are available to that reader.
   Run publication concurrently with readers, including held-open manifest
   handles on Windows and a retained old snapshot resolving dependencies after
   a newer manifest is published. Assert each reader uses one generation only.
   Manifest validation includes trailing LF/CRLF and embedded-control regressions
   for identifiers and paths, independently of object digest/path agreement.
6. **Resource accounting:** valid near-limit assets, encoded/decoded expansion,
   aggregate live memory, parser/decoder scratch, and injected allocation failure
   across arenas and heap hooks. Confirm a lower configured ceiling rejects
   safely, temporary-disk use is bounded, and successful output is identical
   under different sufficient budgets. Record measured peaks before selecting
   the default working-memory ceiling.

Implement in these reviewable slices: shared reference/reader prerequisites;
headless catalog and asset adapters; manifest/fingerprint engine; transactional
publication; independent-process/cross-platform coverage. Each slice includes
its tests. Do not mark #718 complete until they work together end-to-end.

Use a small checked-in deterministic fixture for CI. An optional read-only
SampleProject smoke cook writes to a separate temporary output root; it does
not rewrite project metadata or upgrade sample assets to make the test pass.
The test-only `SceneCookProcess` helper supports
`smoke-offline <project.json> <existing-empty-workspace> <explicit-working-bytes>`.
It requires an offline project and rejects physically overlapping or occupied
workspaces. Three fresh input owners verify publication, recaptured no-op and
forced rebuild, then independently check manifest/object integrity and decode
the scene objects. The process fixtures cover these checks with plain/Unicode
paths, a complete two-scene batch, unchanged inputs and insufficient budgets.
Configuration parsing and harness setup are not cooker-budget proof. This is
not a shipping CLI, live-editor snapshot protocol or runtime-rendering gate.
Implementation is in progress and includes focused engine tests for the shared
VFS, allocation, decoder, and hashing foundations. These do not replace the
end-to-end cooker/package-reader and cross-platform acceptance gates below.

The concrete frozen-input provider now builds its strict read-only catalog
through pinned roots, owns captures independently of its input lease, and
rechecks discovery identities and capture digests at seal. Its tests cover
allocation failures, working/input limits, environment artifact mapping,
physical aliases, and capture lifetime after native facade teardown. The shared
strict material document reader and canonical material/projection emitters use
inline storage with streaming JSON output. These components now feed the public
run described below. Complete run-wide budgeting and cross-platform/process
acceptance remain unfinished.

The material adapter and fixed four-kind registry now exist. Material inspection
retains only compact dependencies/original paths in reusable scratch storage;
encoding rereads immutable captures and emits resolved package paths without
consulting the input lease. Shared discovery resolution rejects original
UUID/path disagreement before sealing. Tests cover failure rollback and
post-seal encoding after the original context is shut down.

The mesh adapter now reuses the CPU mesh reader through a borrowed byte-source
interface. The shared reader's layout pass computes exact decoded workspace
capacity without allocating; full decoding still validates geometry, hierarchy,
UTF-8 names, maps, stream offsets/sizes, and exact payload consumption. Native
and VFS wrappers publish outputs only after checked close succeeds. The adapter
retains only canonical material dependency slots, omits nil fallback edges, and
reuses decoded arena storage as its passthrough buffer after destroying decoded
containers. Shared adapter support handles metadata, diagnostics and captured
byte streaming/digest verification. Tests cover exact/insufficient arena sizes,
lexical slot order, allocation/read/write faults and headless repeated reuse.

The texture adapter now validates supported encoded images through the shared
CPU implementation, directly over immutable captured-byte callbacks. Format
selection uses content rather than filename spelling; bounded dimensions and
native RGBA storage (including 16-bit PNG) are checked before full decode.
Every decoder allocation/reallocation uses the checked cook route, with sticky
allocation/read/skip failures. Pixels are released before allocating the
at-most-64-KiB passthrough stream. Leaf inspection retains no allocation, and
encoding verifies the captured SHA-256 without a full encoded-file copy. Tests
cover all seven declared formats (first frame only for GIF), repeated reuse,
truncated PNG/JPEG/BMP/TGA input, allocation/provider/sink failures, and a large
BMP that cannot fit an encoded copy alongside decoded pixels in its pool.
Decoder acceptance is the supported CPU decoder's content contract, not a new
strict container/CRC parser. Principal common/PNG/zlib/GIF fixed state now uses
the checked shared working-budget route; complete helper/compiler-frame
accounting remains open.

The environment adapter now validates the exact six sidecar settings, original
artifact-path spelling, source receipt hash, and complete current `.zenvmap`
payload. Its shared CPU reader supports borrowed captures and checked-close VFS
reads, enforces current artifact/importer versions, zero reserved fields, exact
size, square faces/mip policy, bounded dimensions/bytes, positive finite exposure,
and finite RGBA32F values. It never allocates a decoded cubemap or initializes a
renderer. Source receipt hashing is shared with `MetaFileIO::ComputeHash`,
preserving the existing chained-rapidhash 4096-byte boundary contract regardless
of provider fragmentation; SHA-256 independently verifies the captured source.
One at-most-64-KiB workspace is reused for source hashing, payload validation,
and artifact passthrough. Leaf inspection retains no allocation. Tests include
payloads larger than that scratch pool, short/failed reads, settings/hash/digest
disagreement, truncation/nonfinite data, allocation refusal, and checked close.
The run-owned dependency graph and captured-asset discovery now exist.
`CookDependencyGraph` uses one checked, geometrically grown block for compact
records/text, its typed-identity hash index, and finalization workspace. Numeric
indexes survive growth; borrowed pointers do not. Document-wide root collection
copies provenance, counts every enumerated occurrence before filtering entity
references/nil fallbacks, and rolls back logical records on failure. Distinct
slots remain edges even when they share a child. Iterative leaf-first Kahn
ordering uses a canonical min-heap; scene roots/closures are deduplicated and
canonical. Finalization allocates nothing; closure uses caller-owned workspace.
Cycle diagnostics identify an actual closed cycle, not its acyclic dependents.

`CookAssetDiscovery` visits each unique reachable identity once, captures its
artifact/sidecar and optional environment source, calls the registered shared
adapter, and resolves every original UUID/path pair against the active frozen
catalog before committing edges. Lazy blocks hold 32 inline capture owners
without relocation or per-asset owner/control-block allocations; no parsed or
decoded asset document is retained. Strict metadata can be reparsed from these
owned captures after sealing, independently of the input lease. Discovery does
not itself seal, encode, stage, or publish. Its first capture-cleanup failure is
retained for publication-time warning policy, without hidden retries. Tests
cover shared assets, unreachable invalid payloads, post-seal encoding after
source replacement/context teardown,
allocation failures, long chains/cycles, and occurrence-limit rollback.

The shared libyaml allocation boundary now routes through a scoped checked
allocator, including scanner reallocations and separate string duplication.
The build wraps the pinned API source without modifying fetched dependencies;
the standalone yaml target owns the lean bridge. Image decoding and YAML reuse
one allocation-route interface and cook backend. The scene tree can now borrow
precharged writable storage without creating its own OS arena. Focused tests
inject every parser/scanner/document/emitter allocation failure and exercise
rollback/reuse, nested/thread-local ownership, and independent yaml-only linkage.
`SceneDocument::Parse` and `BinarySceneCodec::Decode` now accept that checked
route. The immutable header and initial tree share one retained block of at most
32 KiB plus header. Larger trees add geometrically grown, stable routed blocks;
the decoded cap is no longer allocated up front. All capacity, alignment and
block headers remain budgeted, and moves never relocate borrowed tree text.
Settings expose a borrowed `SceneDocumentSettingsView`, whose name
points into NUL-terminated owned tree text. Editor capture inputs may retain the
convenience owning settings type. Moves carry the original allocator/deleter,
and failed parsing/decoding preserves the caller's previous document.
Compatibility upgrades use two inline tree owners, each initially small, and
reuse their stable backing blocks per migration chain. Small chains, including
the 32-step fixture, use exactly two initial blocks; larger trees grow only as
their observed peak needs increase. Rewinding preserves the charge for retained
blocks and requires no new allocation for a previously reached footprint.
Tests inject every routed allocation failure through parsing and document plus
component upgrades, check release/move/name lifetimes and multi-block source/
binary round trips, and prove identical binary bytes under different sufficient
decoded-storage budgets. Principal YAML parser/event/tree fixed objects now
use the same working ledger before libyaml initialization; full parser/helper
stack accounting remains open.

`CookSceneCodec` now verifies captured scene bytes/digests, parses through the
checked document/YAML route, and refuses unsupported read-only documents. Its
temporary contiguous source buffer is exact-sized and released before return;
roots can be copied into the graph without retaining parsed scenes. The codec
owns the persistent allocation route and must outlive returned documents.
Structured scene diagnostics distinguish malformed data, unsupported versions,
configured limits, and allocator failure, including rolled-back tree failures
and diagnostic-entry overflow. Binary measurement shares the encoding layout/
validation algorithm and allows one exact-sized output workspace, not a file-cap
reservation. The scene bridge reparses captures for encoding, checks the
discovered UUID, computes the binary SHA-256, and writes only to a supplied
staging sink; caller results remain unchanged on failure. It has no publication
authority and the caller still must seal before encoding. Tests exercise short/
invalid reads, source digest disagreement, format/decoded caps, native allocation
failure, every allocation refusal through parsing/migration/encoding, independent
binary decoding, and scene plus asset encoding after a real frozen-input seal,
source replacement, and source-context teardown.

The exact v1 fingerprint functions now stream schema, fixed settings, recipe,
asset, graph and scene inputs directly into the typed SHA-256 encoder. Schema
iteration uses the registry's canonical order; field sorting reuses caller-owned
indexes sized to the largest observed field table, without mutating registration
order or imposing a new 64-field cap. Asset keys read direct graph edges and
node-indexed child results without building another dependency array. They
include every distinct slot, child key, output digest/size/path and format.
Scene keys reuse the existing iterative transitive-closure workspace, excluding
unrelated scene assets. Content-addressed object path construction/validation is
now shared between asset adapters and scene/package descriptors. These helpers
validate descriptor spelling, not physical object bytes; publication/reuse must
still verify those bytes. Independent typed-byte goldens and invalidation tests
cover registration/discovery order, runtime/callback identity exclusion,
locations/provenance, working budgets, schema/recipe/settings changes, source
digests, shared children, slot removal and per-scene closure isolation. A real
post-seal scene/asset fixture now also computes its recipe and input keys from
the captured scene and generated material bytes.

The manifest writer now streams exact canonical v1 bytes from the finalized
graph and node-/scene-indexed results. It uses caller-owned sorting workspace,
validates descriptors and measures the byte cap before any sink write, and
hashes successful staging output without allocating a JSON buffer. The ledger
reader first validates complete JSON syntax/Unicode and decoded-key uniqueness,
including unknown nested objects, using one exact temporary key index rather
than a DOM. Compatibility checks precede rebuildable v1 shape errors. Parsed
records and dependency text occupy one exact retained allocator-routed block;
temporary graph validation reuses the shared dependency indexing/finalization
algorithms. Global asset UUID uniqueness, reachable closure, canonical array
order, logical paths, slot spellings and shared-object size consistency are
checked. Hard failures preserve the previous reader owner; a rebuild decision
warns and clears records so none of its paths can authorize cleanup. Tests use
the reviewed golden/negative fixture files, every allocation/sink refusal,
input teardown, all supported image format descriptors, and a real post-seal
scene/material encode-to-manifest/read integration. A valid ledger still does
not prove any physical object bytes or authorize reuse/publication.

The synchronous incremental-reuse coordinator now computes current keys in
leaf-first order and compares previous identities, formats, dependency slots,
child keys and child object descriptors. Scene checks use only their own closure;
schema changes invalidate scenes without invalidating otherwise reusable assets.
One exact allocator-routed block holds graph-indexed results/state, reusable
closure workspace and compact open-addressed verified-object indexing. A borrowed
stream buffer (at most 64 KiB) verifies exact file sizes, EOF and SHA-256, with
checked close on every opened handle. Missing files are rebuildable misses;
existing corrupt files and read/stat/close failures are hard errors, never repair
permission. Verified shared files are opened once per locked run; misses/errors
are not cached. Forced encoding still requires physical verification before an
installed result becomes ready. Results cannot be replaced once ready. The
caller must seal inputs, hold the cooperating writer lock and immutable rooted
object authority throughout the run; this helper has no publication authority.
VFS wrapper/snapshot allocations remain separately run-accounted, not an implied
zero-allocation VFS guarantee. Tests include allocation refusal, hash collisions,
shared objects, inconsistent previous ledgers, scene isolation, byte corruption,
native errors, symlinks/special files and real post-seal scene/material reuse
through a retained root after source replacement/context shutdown. Checked packed
table layout arithmetic is shared by the graph, ledger and reuse owners.

The run-owned `CookSceneCaptureSet` now maps the entire requested scene list before
capturing a scene, sorts a compact index to reject duplicate logical paths, and
retains stable capture owners in one exact allocator-routed allocation. Request
order remains graph-index order. Each scene is parsed only long enough to copy
its identity and typed reference roots into the shared graph; no parsed document
is retained. Duplicate UUIDs reuse the graph's conflict checks. Invalid/empty/
over-cap requests and duplicate paths fail without capturing scenes; partial
capture/parse failures expose no complete result set and require discarding the
partial graph. Reset reports owned-capture cleanup failures without retrying
against later filesystem state. Tests cover multiple scenes, path/UUID conflicts,
source/context teardown, single-table retention, and every capture-set allocation
refusal through the real frozen provider and scene reader.

The synchronous `CookPublication` session now acquires the exclusive writer directory
through a retained output root, requires already-sealed inputs, and strictly
loads the previous ledger before preparing reuse state. Objects and the manifest
stream through the shared VFS atomic-file owner: exclusive sibling creation,
bounded complete writes, checked flush/close, verification of the actual staged
SHA-256/size, and atomic no-replace installation or replacing manifest commit.
Existing collisions are verified, never overwritten; corrupt objects fail even
on forced rebuild. One borrowed stream buffers small producer writes and is
reused for physical verification and exact canonical-manifest comparison. No
additional publisher-owned cook allocation is made after initialization;
producer codecs and VFS wrappers retain their separately charged allocation
policies. A verified
canonical no-op writes no output file and needs no staging bytes; noncanonical
valid ledgers are republished canonically. New installed objects and manifest
staging share the run's staging cap; duplicate staging releases its charge only
after successful owned cleanup. Rename is the sole manifest commit, with the
fixed result fully prepared beforehand and warning-only owned cleanup afterward.
Memory/POSIX-disk tests cover short I/O, every publication I/O boundary, allocation
refusals, immutable corruption/missing objects, contention, failed cleanup,
held-open old manifests/snapshots, and no postcommit producer/write activity.
Real capture/discovery/scene/material codecs also publish a canonical package
after sealing, source replacement and source-context teardown, then prove a
second independently captured verified no-op.

Root-first input acquisition now holds exclusion without allocating capture
storage, discovering the catalog, or writing scratch. Real memory/native tests
check physical domain separation before private-run creation; the real
scene/material publication and independently captured no-op fixture use the same
sequence, including retained pins after source-context shutdown. Explicit
provider storage release rejects live leases/captures and returns routed buffers
before a caller-owned provider is destroyed. Fixed provider/owner/configured-binding
state has a separate reported charge; these are prerequisites, not the complete
run-wide ledger.
The shared same-access child-root API now pins existing descendant directories
from retained disk/owned-memory anchors, with independent bounded mount pins.
Tests cover parent release, context/provider lifetime, path replacement, symlinks,
access preservation, and pin exhaustion. Real cooker integration places both
publication and independent captured no-op beneath the anchored target directory.
Those real fixtures now also apply the checked VFS memory policy to every
input/output/scratch root. It includes fixed root/file-slot state, shared
provider/pool and mount-table capacities, and memory-arena tracker pages.
Reference-counted process-local domain identities avoid duplicate pool charges;
independent contexts still pay for their distinct mapping tables. The common
index requires no allocation, and larger sets use the engine hash map with
transactional checked growth/compaction and prompt old-workspace release.
Tests cover budget/host refusal at growth, conflicting capacities, overflow,
root/child acquisition rollback, borrowed-root capture failure, and lifetime
after parent/context/provider teardown. Fixed parser/decoder/hash call-stack
accounting remains unfinished.

`SceneCooker::Cook` now implements the selected public synchronous entry point.
It validates the whole request/service/limit configuration before acquiring
input exclusion, charges fixed run/provider/owner/result/diagnostic state, and
uses one checked allocator and VFS policy for the complete call. Physical-domain
preflight precedes all scratch/output directory creation. The private scratch
directory reuses the shared VFS exclusive temporary-name protocol; target child
authority is acquired from the retained output parent. Capture/discovery and the
fallible input seal precede publication initialization. Assets encode leaf-first,
with child results mapped into one reusable geometrically grown dependency table;
ready cached objects never invoke their encoder. The same 32 inline sorting words
serve schema hashing and manifest sorting; larger workspace is checked and reused.
No orchestrator heap/control block, per-edge allocation, or new per-call atomic
is introduced. Dead dependency-table backing is released before replacement,
without relocation/copy or simultaneously retaining old/new tables.

Cleanup drains publication/captures/input exclusion/provider buffers before
removing only the owned, nonrecursive scratch run and releasing root pins. It
runs on every return path. The fixed caller result changes only on success;
postcommit cleanup errors are bounded warnings. Memory/POSIX-disk public-call
tests cover first publication and independently captured no-op, real texture to
material path rewriting, complete multi-scene/shared-asset batches, canonical
request-order/budget independence, every routed allocation refusal, duplicate
scene paths/UUIDs, overlapping roots, writer contention, forced encoder failure,
corrupt immutable objects, unowned scratch preservation and provider-cleanup
warnings. They also decode the produced binary scene with the shared codec.

`SceneCookProcess` now supplies a separate headless producer/consumer gate. Its
reviewed package golden covers one Unicode-named scene, mesh, canonical material,
BMP texture and environment-map artifact. Checked-in scene/material source
snapshots and fixed SHA-256/size expectations anchor every generated source and
published object; expectations never auto-update after a failed test. A separate
producer with different actual component-type IDs and a moved checkout must emit
the same manifest. Independently recaptured no-op/forced runs preserve those
bytes. A texture-only byte change invalidates the texture/material/mesh/scene
keys, rewrites the material's staged path, retains passthrough mesh bytes and
leaves the unrelated sky reusable.

Fresh consumers mount only a package with read-only authority after source
trees are removed. They verify every object's size/SHA-256, decode scenes and all
four asset kinds, and check UUIDs, names, transforms, mesh geometry/material
edges, texture paths and sky settings. Negative consumers reject missing and
same-length byte-corrupted texture objects. A concurrent process holds the old
manifest open before publication, first reads that handle after replacement,
and resolves all old dependencies from the same output directory. Abrupt child
exits immediately before/after the actual manifest rename preserve the expected
old/new package. Retry refuses the stale writer directory; explicit new-root
rebuild preserves interrupted output and reproduces the complete new package.
These checks run on plain and Unicode native roots through CTest and the
installed-artifact script. The locally installed macOS artifacts pass without
source-checkout arguments; macOS/Linux/Windows CI is configured to invoke that same
script. Windows process CTests and shared memory/disk cooker tests are now enabled.
macOS installation re-signs the Vulkan loader after its install-time rpath edits,
as well as the existing helper signing; fresh installed artifacts need no manual
dependency-signature repair.

Local validation on 2026-11-05 after routed scene-storage growth: 2,430 engine
tests ran (2,425 passed, five existing skips), all 1,265 editor tests passed,
and all five scene/YAML/cooker process CTests passed. Freshly installed scene and
cooker scripts also passed their plain/Unicode gates without source-checkout
arguments. The optional smoke now passes its isolated project fixtures at the
explicit 16 MiB working budget, including recaptured no-op, forced rebuild,
two-scene batches and safe insufficient-budget refusal. The real SampleProject
smoke remains unrun while the editor owns that project; fixture evidence is not
proof of its actual imported-asset closure.

The Windows retained-root implementation uses counted single-component
`NtCreateFile` opens relative to directory handles, rejects reparse points before
truncation, and keeps all traversal on one volume. Rename uses one handle-relative
`FileRenameInfo` operation without copy/delete fallback; successful rename is the
commit point. Native 128-bit identities are shared with ordinary disk files.
Enumeration reuses the caller's aligned buffer, opens each returned entry without
duplicating its parent or copying its UTF-16 name, and uses independent directory
offsets. Physical overlap walks parent identities rather than comparing native
path prefixes. The initial profile requires local NTFS/ReFS with file-ID support
and a successful bounded-query probe; other filesystem profiles fail closed.
One root-state allocation contains 32 inline wrappers and retains its provider.
Windows-specific tests cover junction rejection and retargeting, ordinary versus
replace-compatible sharing, append offsets, invalid-handle errors and native
Unicode/128-bit identity. The implementation and nine affected test/helper
translation units cross-compile for x86_64 Windows with real Windows/dependency
headers. This is compile evidence only: native NTFS/ReFS execution, ancestry/query
assumptions and filesystem-specific rename behavior still require Windows runs.

Backing allocator accounting now preflights both a per-block occupied-memory bound
and the complete retained allocator chain's fixed state/idle pools. Unknown bounds
fail before root acquisition or output/scratch mutation. Native page backing
includes mapping headers/alignment/page rounding without a CRT heap or allocation
map. Nested budget/scratch wrappers count a retained outer pool once, not every
occupied child block again. TLSF sizing is checked against actual occupied blocks,
including an intentionally unsplittable tail. Stream sizing distinguishes reusable
pool capacity from additional ledger headroom; checked allocation remains the
authority for pool exhaustion/fragmentation. The test-only project smoke uses this
native backing and reports mapping peaks/allocation/free counts. Its setup/config
parsing is still outside the synchronous cook's complete memory proof.

The default-scene-cap regression now also parses through real native page backing
under an explicit 1 MiB working ceiling, without eagerly allocating its 128 MiB
decoded cap. This is not a 128 KiB physical-memory claim: page rounding for numerous
small parser blocks counts, unlike the synthetic requested-byte unit allocator.
Reusable scratch remains the intended small-allocation amortization mechanism.

Local validation on 2026-11-05 after the backing-footprint/retained-pool changes:
2,447 engine tests ran (2,442 passed, five existing skips), all 1,265 editor
tests passed, and all five scene/YAML/cooker process CTests passed. The focused
cooker/TLSF set passed all 503 tests. Fresh installed scene and cooker scripts
passed their plain/Unicode gates without source-checkout arguments. Native-backed
nested-pool tests verify idle-capacity refusal at one byte below the complete
fixed/pool charge, no child-block double charge, no additional upstream mappings
for 256 allocate/free cycles, and complete release. Public memory/disk tests
refuse unknown retained/block contracts and unaffordable idle backing before
acquiring input roots or mutating output/scratch. The affected Windows allocator,
orchestrator and test translation units cross-compile; no native Windows run or
cross-host golden comparison has been established by this validation.

Pre-owned frozen root bindings now use allocator-chain-aware accounting rather
than just their payload byte count. The query includes separate backing state,
idle pool capacity and full native/block overhead, while shared/nested pools
already covered by the run add no child-block charge. The nearest common ancestor
is found in linear time with constant storage and cycle detection before recursive
profile queries; no second registry or allocation is introduced. Native-backed
tests cover shared, sibling, nested and independent allocator chains, unknown/
underreported bounds, stable retained roots after refusal and repeated pure queries.
Public memory/disk tests verify that separate native bindings add exactly their
measured backing footprint without changing package bytes, and that an oversized
external pool fails during setup before exclusion/output/scratch mutation.

Local validation on 2026-11-05 after pre-owned binding accounting: 2,457 engine
tests ran (2,452 passed, five existing skips), all 1,265 editor tests passed, and
all five scene/YAML/cooker process CTests passed. Fresh installed scene/cooker
scripts passed plain/Unicode gates without source-checkout arguments. Affected
allocator, owner/provider, orchestrator, process-helper and regression-test
translation units cross-compile for Windows; this remains compile evidence,
not native Windows or cross-host execution. The live SampleProject editor was
left untouched; its offline smoke remains outstanding.

Schema/migration registry backing now has an allocation-free native-arena
accounting query. It charges registry objects and root/sub-arena owner state,
complete writable committed pages across each arena family, and page-rounded
tracker storage. Shared roots are counted once even when the registries use
different child views; idle pages remain charged after Clear/reuse or child
teardown. Virtual address reservation is separate from live working bytes.
Unknown borrowed/routed backing fails closed during setup, before input-root
acquisition or output/scratch mutation. The optional migration registry must be
initialized. Memory/disk public-call regressions verify early refusal and that
an independent migration arena adds exactly its measured retained charge without
changing canonical package bytes or the verified no-op outcome. Callback-owned
contexts are deliberately not inferred from `void*`.

Local validation on 2026-11-05 after registry-backing accounting: 2,474 engine
tests ran (2,469 passed, five existing skips), all 1,265 editor tests passed,
and all five scene/YAML/cooker process CTests passed. The final focused
allocator/growing-arena/registry/public-cooker set passed all 98 tests. Fresh
installed scene/cooker scripts passed plain/Unicode gates without source-checkout
arguments. The changed native allocator, registry query, orchestrator, tests and
both cooker process helpers cross-compile for Windows; this is not a native
Windows run. Boundary tests cover root/child views in both registry directions,
shared and independent backing, repeated pure queries, discontiguous commitment,
Clear/reuse/move/teardown, and unknown borrowed/routed backing. The live
SampleProject editor remains untouched and its offline smoke is still pending.

Callback contexts now require one explicit per-call `ICookCallbackStorage` owner.
The cooker checks every schema codec/reference, migration-entry and adapter root,
then queries the complete graph's uncovered footprint once before input work.
Missing/unrecognized roots and unknown footprint contracts fail closed; all-null
built-ins need no owner. The concrete `CookArenaCallbackStorage` borrows one
native arena family, includes full retained pages/tracker/owner state, and
deduplicates registry-shared backing and arena-resident provider storage. Coverage
queries allocate nothing and use linear time/constant storage without another
hash registry. A custom external/composed owner must provide complete retained
graph bounds and proven deduplication; merely reporting a pointer-bearing root's
`sizeof` or an allocator's aggregate statistics is insufficient. Callback graph
contents and backing stay stable throughout the synchronous call. Broader
service/VFS/allocator alias acceptance and call-stack accounting are still open.

Local validation on 2026-11-05 after callback-context ownership accounting:
2,490 engine tests ran (2,485 passed, five existing skips), all 1,265 editor
tests passed, the full cooker/TLSF subset passed all 542 tests, and all five
scene/YAML/cooker process CTests passed. Fresh installed scene/cooker scripts
passed their plain/Unicode gates without source-checkout arguments. The changed
allocator, compatibility visitor, registry query, callback owner, orchestrator,
regression tests and both cooker process helpers cross-compile for Windows;
native Windows execution and cross-host equality remain unverified. Unit coverage
includes each callback-root category, one footprint query for a shared graph,
distinct roots in one native family, committed-range holes/bounds, registry-family
and arena-resident-provider deduplication, pure repeated native queries and
unsupported ownership. Public memory/disk tests prove early missing/unknown/
unaffordable context refusal with no adapter execution, and an exact native
footprint increase without changed package bytes or the verified no-op outcome.

Native service accounting now uses one shared three-view calculator for schema,
migration and declared callback arena families. Registry objects and native
root/view/provider objects already resident in any of those charged families add
no duplicate inline state, including cross-family containment. Native snapshots
identify the actual live root owner, updated on moves while preserving backing
identity for existing child/sibling views. An optional callback `StorageArena()`
declaration enables cross-family containment; its complete backing must actually
be included by the paired callback footprint. Both queries must succeed and be
charged before input work, as the public cooker enforces. This fixes native
service-object double charges without a new hash registry or allocation; broader
working-allocator/VFS/non-native service aliases remain separate acceptance work.

Local validation on 2026-11-05 after native service-object containment: 2,497
engine tests ran (2,492 passed, five existing skips), all 1,265 editor tests and
all five scene/YAML/cooker process CTests passed. The focused allocator/registry/
callback/public-cooker set passed all 121 tests and the full cooker/TLSF set passed
all 548 tests. Fresh installed scene/cooker scripts passed plain/Unicode gates
without source-checkout arguments. Changed allocator, shared service calculator,
callback owner, orchestrator, regression tests and both cooker process helpers
cross-compile for Windows, not native execution evidence. Tests cover live child
views across root moves, arena-resident registry/view objects, root owners stored
inside another native family, callback-family cross-containment and pure repeated
queries. Public memory/disk tests relocate actual schema/adapter/provider objects
into native callback storage and verify the exact combined peak-memory delta,
unchanged package bytes and a canonical verified no-op. The live SampleProject
editor remains untouched; its offline smoke is still pending.

Standard VFS retained operational objects now have measurable physical backing.
Disk roots/providers, owned-memory roots/storage and retained mount tables use
one header-free native mapping per fixed final object, rather than charging only
`sizeof` for unknown CRT-backed allocations. Their exclusive/shared profiles
include complete writable-page rounding. The common `NativePages` primitives
also serve `CookPageAllocator`, removing its duplicate mapping/release backend.
There is no new allocation map, per-object header or per-file allocation;
existing inline file slots and memory-backend pools remain the high-frequency
storage route. Standard retained VFS mappings are independent of caller registry
arenas. The subsequent caller-facade accounting below covers native facade state
and arena families; fixed stack and arbitrary custom service/allocator aliases
still need their separate acceptance audit.

Local validation on 2026-11-05 after native VFS object backing: 2,501 engine tests
ran (2,496 passed, five existing skips), all 1,265 editor tests and all five
scene/YAML/cooker process CTests passed. The native-page/cooker/TLSF/root-lifetime
subset passed all 578 tests; the focused native-release/page-allocator/VFS-budget
set passed all 21 tests. Fresh installed scene/cooker scripts passed plain/Unicode
gates without source-checkout arguments. Native tests verify checked rounding,
exact writable capacity, invalid-argument refusal, default object initialization,
128 adjusted-base virtual deletions and actual mapping removal. macOS removal is
checked with Mach VM regions, including a live-mapping control; a residency query
is not used as an unmapped-range oracle there. Changed native-page, cooker-page,
VFS mapping/root/storage and regression/process-helper sources cross-compile for
Windows; the affected native-page/page-allocator/POSIX-root/mapping/storage sources
and native-page tests also cross-compile for Linux. These are compile checks, not
native Windows/Linux runs or cross-host golden equality. The live SampleProject
editor remains untouched and its offline smoke is still pending.

Caller value accounting now includes the request, context and complete borrowed
source-path span, in addition to result/diagnostics. The allocation-free
`QueryCookCallerMemory` uses five inline address ranges, checked arithmetic and a
range union. Paths borrowed from request/context fields are counted once; complete
value ranges in already charged native registry/migration/callback families add
no separate charge. All caller/registry/callback/VFS profiles are checked and
charged before root acquisition. No source-path copy or allocation is introduced.
This covers caller value ranges, not arbitrary excess heap capacity/metadata or
the ownership of every context service pointer. Broader custom service/allocator
aliases and complete call-stack accounting remain open.

Local validation on 2026-11-05 after caller-value accounting: 2,511 engine tests
ran (2,506 passed, five existing skips), all 1,265 editor tests and all five
scene/YAML/cooker process CTests passed. All 60 focused caller/public-cooker tests
passed. Repeated pure queries, request/context source-path subobject aliases,
caller values in registry/callback/migration families and unrelated native
storage are covered. Public memory/disk tests verify the exact peak-charge
reduction for arena-resident caller values, unchanged verified no-op package
bytes and a one-byte-short caller charge failing before input acquisition,
adapter execution or output mutation. Fresh installed scene/cooker scripts passed
plain/Unicode process gates. The changed caller/registry/orchestrator and both
regression-test translation units cross-compile for Windows and Linux; this is
not native execution evidence. The live SampleProject editor remains untouched;
its offline smoke remains pending.

Output/scratch caller-facade storage now participates in the native-service
accounting. `IVFSContext::NativeStorage` declares the complete fixed object range
and native arena family; `QueryCookVFSServiceMemory` charges its retained native
pages/tracker and root/view owner state before input-root acquisition. The shared
calculator has five fixed slots (schema, migration, callback, output, scratch),
not a heap-backed index. Shared families and complete facade object ranges are
counted once, including service owners/caller values stored within a facade.
The standard context reports its real import-bookkeeping arena and refuses active
imports or attached watcher/cache/scanner/registry/coordinator/listener graphs.
Unknown derived contexts cannot silently inherit a `sizeof(base)` profile; they
must explicitly declare complete storage. Unknown/borrowed/malformed/unaffordable
storage fails before input acquisition, adapter execution or output mutation.
Operational VFS root/provider/pool/mapping domains remain independently counted
by the existing VFS policy. This is not a bound for arbitrary hidden heaps or
custom allocator/operational-domain aliases.

Local validation on 2026-11-05 after native VFS caller-facade accounting: 2,527
engine tests ran (2,522 passed, five existing skips), all 1,265 editor tests,
all 95 focused native-service/caller/public-cooker tests and all five process
CTests passed. Tests cover pure repeated queries, shared/independent families,
registry/callback/VFS cross-containment, arena-resident facades, nested caller
values/arena owners, overlapping composite facade objects and invalid/conflicting
custom declarations. A real native watcher is refused until its thread and
external state are detached. Public memory/disk cooks prove the exact facade
inline-charge reduction and unchanged verified no-op package bytes. Fresh
installed scene/cooker scripts passed plain/Unicode gates. Changed context,
service calculator/query, orchestrator, regression tests and both cooker process
helper translation units cross-compile for Windows and Linux using real headers;
this is not native execution or cross-host golden equality. The live SampleProject
editor is still untouched and its offline smoke remains pending.

Custom-schema package acceptance now includes a real application-defined
`GameSurface` component: authored texture UUID/weight, typed texture dependency,
and a runtime cache omitted from both source and cooked payloads. Independent
producers register it before/after built-ins with different actual component
IDs. Fixed reviewed source/manifest digests anchor both schema versions; literal
20-byte UUID/float vectors independently anchor its codec. An identity source
migration from v1 to v2 changes the schema key, scene input key and scene object,
but preserves authoring source and every asset record. Missing migration
preserves the old manifest, object inventory and every caller-result field,
with no leaked scratch/lock files. Independent read-only consumers decode v1/v2
packages after source removal; missing/non-current schemas fail explicitly.

The binary scene decoder now distinguishes unsupported schemas/versions from
malformed tables without an allocation or additional table pass. It finishes
canonical table validation before returning `UnsupportedVersion`; malformed
keys, reserved identity keys, bounds/order and trailing bytes remain
`InvalidData`, even when another schema is unavailable. Refusing-route tests
prove these failures occur before document allocation and preserve prior output.
Custom codec/migration tests cover literal wire bytes, truncation/trailing data,
invalid values, short storage, typed reference/remap semantics, owned migration
clones, derived-field rejection and default runtime-cache reconstruction.

Local validation on 2026-11-05 after custom-schema acceptance: 2,536 engine tests
ran (2,531 passed, five existing skips), all 1,265 editor tests and all five
process CTests passed. All 120 focused schema/ownership/public-cooker tests
passed. Fresh installed scene/cooker scripts passed plain/Unicode gates. The
binary codec, regression tests, custom callback fixture and process helper
cross-compile for Windows and Linux with real dependency headers. This does not
establish native Windows/Linux execution or cross-host equality. The live
SampleProject remains untouched and its offline smoke is still pending.

Native working-allocator fixed state now participates in the same service-range
coverage proof. Page/scratch/budget allocator objects resident in schema,
migration, callback or VFS facade storage do not add another fixed-state charge.
`UncoveredMemoryRequirements` composes native parent profiles without allocation;
the ordinary profile remains the full physical state so a nested consumer cannot
inherit a previous ledger's credit. Pool bytes, idle capacity, allocation
containment and per-block footprints remain unchanged. Opaque/custom allocators
receive no inferred discount. The existing five-slot service calculator supplies
the proof; its inline state is charged, every service profile must succeed and
be charged before input work, and no coverage pointer is retained. A cycle check
precedes recursive allocator profiles without an index or arbitrary depth cap.

Local validation on 2026-11-05 after native allocator-state coverage: 2,547 engine
tests ran (2,542 passed, five existing skips), all 1,265 editor tests, all 91
focused allocator/public-cooker tests and all five process CTests passed. Native
tests cover repeated pure queries in independent service families, embedded VFS
allocator state, nested page/scratch/budget wrappers, unchanged physical profiles,
fully charged idle pools, exact/one-byte-short budgets, opaque owners, malformed
coverage claims and allocator cycles. Public memory/disk cooks prove the exact
resident allocator-state reduction and unchanged verified no-op package bytes.
A one-byte-short service charge still refuses before input acquisition, backing
allocation, adapters or output mutation, even with a covered allocator object.
Fresh installed scene/cooker scripts passed plain/Unicode gates. Changed allocator,
service/calculator/orchestrator and regression/process translation units
cross-compile for Windows and Linux; this is not native execution evidence.

Native frozen input-provider/owner state now uses that same coverage proof.
Complete source/owner objects in schema, callback or VFS native service families,
including embedded objects in a declared complete VFS facade, add no duplicate
fixed-state charge. The source's owner allocator applies coverage consistently
to both single-parent chains and their nearest common ancestor, with linear work
and constant auxiliary storage. Both physical and uncovered ancestor aggregates
are validated: a service discount cannot hide an inconsistent physical profile.
Root-binding blocks retain their full mapping/header/page cost unless contained
in an already charged allocator pool; independent/sibling pools retain their
complete backing. Opaque providers keep their ordinary profile, partial object
coverage earns no discount, and no coverage pointer or extra index is retained.

Local validation on 2026-11-05 after frozen input-state coverage: 2,557 engine
tests ran (2,552 passed, five existing skips), all 1,265 editor tests and all five
process CTests passed. Repeated native queries preserve allocation counts, arena
cursors and committed sizes. Public memory/disk cooks verify exact source-only
and combined source/owner peak reductions with byte-identical no-op manifests.
A one-byte-short independent binding-mapping charge refuses before input
acquisition, backing allocations, adapters or output mutation; the same provider
then succeeds at a sufficient budget. Fresh installed scene/cooker scripts passed
plain/Unicode gates. Changed allocator/provider/orchestrator and regression/process
translation units cross-compile for Windows and Linux; native execution and
cross-host golden equality remain unverified. The live SampleProject remains
untouched and its offline smoke remains pending.

Manifest stack-state work now removes duplicate fixed storage before charging
it. Syntax probing, measurement and typed construction reuse one 4097-byte
decode buffer. Measurement saves owned recipe/count values and restarts the
same parser for construction; syntax checking borrows the complete buffer and
never retains it. Principal typed/syntax parser objects, the temporary validation
graph and their charge tokens use scoped `sizeof` charges through the existing
ledger without any new allocation. `ICookAllocator::TryCharge` fails unsupported
by default; native page/budget ledgers support it, and scratch views delegate
external storage to their parent rather than discounting idle pool capacity.
Resource refusal preserves the previous manifest, including unsupported charge
contracts during validation. Synthetic slab fixtures now provide explicit ledger
headroom outside unchanged slab capacities; exact peak comparisons use matching
previous-manifest/no-op phases.

Compiler stack-usage output from the current macOS arm64 Debug flags measured
the manifest reader entry frame falling from 9,696 to 5,504 bytes and its syntax
entry frame from 4,960 to 976 bytes (8,176 fewer bytes across these two nested
frames). This is measured compiler evidence for this build, not an all-path or
cross-platform bound. The shared GIF decoder's roughly 35 KiB principal object
is now charged using its actual type before decode; the remaining decoder
helper/compiler-frame accounting is still unfinished.

Local validation on 2026-11-05 after manifest fixed-state accounting: 2,563 engine
tests ran (2,558 passed, five existing skips), all 1,265 editor tests, all 190
focused manifest/allocator/reuse/publication/scene/public-cooker tests and all
five process CTests passed. New tests cover complete/short decode buffers,
4096/4097-byte strings with range guards, one-byte-short refusal before parser
allocation, every principal fixed-charge seam failing with budget/allocation/
unsupported errors while preserving the previous owner, and exact external-state
charges outside fully retained pools without native allocations. Changed native
allocator/manifest/orchestrator and regression/process translation units
cross-compile for Windows and Linux using real dependency/generated headers;
fresh installed scene/cooker scripts passed plain/Unicode gates. This is not
native Windows/Linux execution or cross-host golden equality. The live SampleProject
is untouched and its offline smoke remains pending.

Shared fixed-state accounting now uses an optional, allocation-free core
working-budget hook on codec allocation routes. Ordinary editor routes can stay
budgetless; the final native cook route always supplies its real ledger. Nested
reservations coalesce into one charge with checked growth/overflow, non-LIFO
release and cleanup even after sticky failure. No registry or new allocation is
introduced. YAML reserves the actual principal parser/event/tree/token sizes
before libyaml initialization. The image reader reserves common source/prefix/
context/result/scope/token state before reading, PNG state before header work,
and zlib/GIF state before decode. GIF's separately routed header object has
already been freed before the large inline decode reservation, avoiding a
sequential-state double charge. Refusal preserves outputs and drains all state.

The environment adapter now separately charges principal source-receipt/SHA-256
state, including the receipt's fixed 4096-byte partial chunk. Shared mesh/texture/
environment passthrough similarly reserves principal SHA-256/result/token state
before its first capture read or sink write, through the actual checked
allocator. Neither path allocates for this accounting, discounts unrelated idle
slab capacity, changes receipt boundaries/fingerprints nor retains a second
stream. Exact/one-byte-short and unsupported-contract tests check no-I/O refusal,
result preservation and charge cleanup, including late read/write/digest failure.
Synthetic allocator wrappers explicitly forward fixed-state requests; fixture
ledgers have headroom outside their unchanged slabs rather than silently waiving
the external storage cost. These gates cover the named objects, not every
recursive/helper frame or compiler spill slot.

Capture/recheck paths now reserve their principal temporary SHA-256, pending
capture/result, EOF-probe and token state before source metadata/read I/O,
capture allocations or scratch-file creation. The common hash loop borrows the
charged caller-owned nonmovable hash object; no second state or allocation is
introduced. Scene read/encode additionally reserve principal temporary workspace,
document-owner, hash/result and token state. Nested encode/read/YAML phases
coalesce in one actual ledger outside retained slab capacity, and all drain even
after sticky refusal. Shared allocation-free preflight precedes any fixed-budget
query, and Encode uses the already-validated read path. Unknown allocation-route
contracts report a backend resource error, not an asset/scene-version error.
New tests cover no-source/no-scratch I/O on capture/refusal, preserved old values,
sticky resource errors, invalid arguments before budget queries, exact nested
phase boundaries and an observed principal encode peak succeeding exactly while
one byte below refuses with no sink write or extra native allocation. Successful
binary bytes remain identical. This is still named principal-state accounting,
not complete helper/compiler/hash-internal frame coverage.

Local validation on 2026-11-05 after shared codec/capture/streaming-hash fixed-state
accounting: 2,583 engine tests ran (2,578 passed, five existing skips), all 1,265
editor tests and all five process CTests passed. The engine suite includes
fixed-budget boundary/refusal and drain regressions for YAML, PNG/GIF, environment
receipt state, shared passthrough, capture/seal rechecks and scene read/encode,
without new native decoder/stream/hash
allocations. Fresh installed scene/cooker scripts passed plain/Unicode gates.
Changed shared YAML/image/cook and regression/process translation units
cross-compile for Windows and Linux using real dependency/generated headers;
this is not native execution or cross-host golden equality. The live SampleProject
remains untouched and its offline smoke remains pending.

Physical object verification now requires the checked allocator at every entry
point, including private staged objects and manifest staging. Shared byte
verification borrows one principal SHA-256/digest state, reserved along with its
charge token before Open/stat/read and retained through checked-close, without
allocation or a nested hash reservation. Invalid arguments fail before budget
queries; unknown/refused fixed contracts are resource/backend failures, not cache
misses, ready results or scene/asset version errors. Reuse and publication route
these charges through their initialized allocator. Refusal poisons publication
before commit while preserving the prior manifest and caller result. Tests cover
exact/one-byte-short limits at all three entry points, active reservation at
Open/checked-close, no-I/O preflight/refusal, reservation drain, and object/manifest
staging refusals on both memory and disk backends. This is principal-state
accounting, not a complete helper/compiler/hash-internal stack bound.

Local validation on 2026-11-05 after physical-verification fixed-state accounting:
2,593 engine tests ran (2,588 passed, five existing skips), all 1,265 editor tests
and all five process CTests passed. The full engine suite ran with native macOS
permissions for Metal, filesystem events and special-file checks; the sandboxed
attempt could not satisfy those OS checks. Fresh installed scene/cooker scripts
passed their plain/Unicode gates. All eight affected verifier/reuse/publication/
regression/process translation units cross-compile for Windows and Linux with
real dependency/generated headers. Native Windows validation, complete fixed
stack/storage accounting and full cross-host acceptance remain outstanding.

Linux x86_64 execution under Docker Desktop's emulation built successfully with
Clang 18.1.3/C++20 and passed all 1,265 editor tests plus the YAML and two scene
process gates. It did not pass cooker acceptance: a direct syscall probe reports
`ENOSYS` for `openat2`, including with no resolution flags, on both container-local
and mounted storage. Rooted disk operations correctly fail `Unsupported` rather
than weaken no-symlink/no-mount-crossing guarantees. The same static probe runs
successfully on native ARM64 Linux with every required resolution flag. Native
Linux package execution and comparison still need their own complete test runs;
the emulated failure is not a reason to skip security checks or claim acceptance.

Publication now separately reserves principal previous-manifest read, object
staging/hash, exact comparison, prepared result and manifest staging objects
before their file I/O. The staging sink borrows an optional hash pointer instead
of embedding an unconditional SHA-256 object: manifest bytes are already hashed
by their canonical writer, so no second staging hash is constructed. A canonical
no-op holds only result/comparison phase reservations, never manifest staging
state. Object staging remains charged through its producer, separately charged
verification, cleanup and destruction. Public workspace/producer preflight still
precedes fixed-budget queries. New memory/disk regressions cover all five named
phase refusals without new cook allocations or phase I/O, exact/one-byte-short
nested object verification, canonical no-op limits with zero staging budget,
reservation drain, sticky refusal and preservation of the old manifest/result.
Complete helper/manifest-writer/compiler/hash-internal frames are not covered by
these principal-object charges.

Local validation on 2026-11-05 after publication principal-state accounting:
2,601 engine tests ran (2,596 passed, five existing skips), all 1,265 editor tests
and all five process CTests passed on macOS. Fresh installed scene/cooker scripts
passed plain/Unicode gates. The modified publication and regression translation
units cross-compile for Windows and Linux. Subsequent native ARM64 Linux
execution and manifest-writer work are recorded below; complete fixed storage/
stack accounting, native Windows and full cross-host acceptance remain unfinished.

The canonical manifest writer now requires the run's checked allocator and
reserves principal stream/writer/hash/pending-result/token state before
measurement/emission. One stream and JSON writer serve both passes; an unused
measurement hash is no longer constructed. The one emission hash is initialized
only after measurement succeeds. No allocation is added. Validation still
precedes fixed-budget queries and no sink writes occur on reservation refusal.
The reservation drains on measurement or sink failure as well as success, and
the caller result stays unchanged on failure. Both publication comparison and
staging route the actual allocator; unsupported contracts report
`UnsupportedBackend`, and all failures poison the run before commit. A no-op
holds result/comparison/writer reservations, never manifest staging state.
Tests exercise exact/one-byte-short budgets, reservation lifetime during every
sink-write failure, resource-error propagation and preservation of the prior
memory/disk package and result. Validation/fingerprint/JSON/hash helper and
compiler-generated frames still need separate bounds.

Local validation on 2026-11-05 after manifest-writer principal-state accounting:
2,605 engine tests ran on macOS (2,600 passed, five existing skips); all 1,265
editor tests and all five process gates passed. A native ARM64 Linux Debug full
build and execution use Ubuntu 24.04, Clang 18.1.3/C++20, libstdc++13 and CMake
4.1.3 in Docker Desktop's ARM64 Linux VM, with no x86 syscall emulation. Required
`openat2` resolver flags work in that native environment. All 617 cooker cases,
1,265 editor tests and five process gates pass there. The full Linux engine suite
has two grid-label GPU pixel-comparison failures with software Vulkan/lavapipe (218 versus
219.6105 at a tolerance of 1.5), also observed before this writer change; 2,589
tests pass and seven skip out of 2,598. No cooker code or test expectation was
relaxed for these failures. This is not a completely green Linux engine suite,
native Windows evidence or the CI LLVM22/x64 profile.

Fresh installed scene/cook gates pass plain/Unicode paths on both hosts. Retained
built-in and custom-schema manifests and each of their five referenced objects
match macOS/native ARM64 Linux byte-for-byte, with sizes and SHA-256 verified.
The manifest digests are respectively
`0db1ca584106b8a31bd9c0a1bc414203bc66a775c0e80b75518f09c8a4c1c7c7`
and `53fb0471850d4b9209860e07aa1fcc9caa46a9958a2715db14dce154ca789d56`.
Native Windows/full three-host equality, complete working-storage accounting,
and remaining acceptance work are still required; subsequent native exhaustion
evidence is recorded below.

The intermittent Linux input-capture failure is now localized to the test host's
Docker bind-mount `fakeowner` filesystem (magic `0x6a656a63`), not manifest writing.
A traced frozen capture observes the same texture path/size/mtime with inode
18442 during discovery and 18444 on read-only open. A standalone C/syscall probe,
with no engine or preload code, reproduces inode 20569 -> 20570 after atomic
replacement in a copied fixture, while bytes, size and mtime remain equal. The
same copied-fixture probe passes 128 repetitions on VM-native overlayfs, and 64
complete traced cooker capture/rewrite runs pass there too. The production
identity check correctly fails closed; repeated success on this bind mount is
not a stable-identity backend guarantee. Use a filesystem providing stable native
identities for offline frozen input. No source-stability check was bypassed,
retried in production, or weakened. A deterministic memory-backend regression
also verifies that identical bytes/size/mtime cannot hide identity changes or
replace an existing capture/receipt, without new allocation or scratch growth.

Native Linux disk-exhaustion acceptance now uses
`SceneCookDiskFullProcess.cmake`, installed beside the independent-process
helper and configured as a separate Linux CI step. It uses a dedicated 2 MiB
tmpfs; the helper validates the actual opened fill file's tmpfs type and <=16 MiB
capacity before filling anything. Ordinary filesystems and oversized tmpfs are
refused before import mutation. A noncopyable test owner writes a fixed 4 KiB
buffer until the kernel reports `ENOSPC`, immediately before the selected real
staging write, without injecting a VFS failure or changing production behavior.
Four cases cover object staging and manifest staging after new objects are
installed, through plain/Unicode paths. They preserve every caller-result field,
the old manifest and its physical objects; independent processes verify the old
package, then normal retry publishes/consumes the changed package. Temporary
staging, scratch, fill files and writer locks drain. Already installed immutable
objects may remain unreferenced after failed manifest staging, as specified by
the publication contract. This proves these native tmpfs write-exhaustion paths,
not block-device writeback/flush exhaustion, power-loss durability or another OS.

Local validation on 2026-11-05 after identity/exhaustion acceptance: macOS runs
2,606 engine tests (2,601 passed, five skips); all 618 cooker cases pass on macOS
and native ARM64 Linux. All 1,265 editor tests and five process CTests pass on
both hosts. Linux runs 2,599 engine tests (2,590 passed, seven skips, the same two
software-Vulkan grid-label pixel failures). Fresh installed scene/cook scripts
pass plain/Unicode paths on macOS and VM-native Linux overlayfs; all four real
ENOSPC cases pass from installed Linux artifacts on the dedicated tmpfs. Safety
checks reject ordinary storage and a 32 MiB tmpfs without retaining a fill file.
The affected identity/publication/VFS/helper test translation units cross-compile
for Windows and Linux. Workflow YAML and shell syntax validate locally; the new
CI step has not been executed in CI. The live SampleProject editor is untouched.

Complete fixed helper/callback/parser/decoder/hash call-stack and remaining
caller-owned service storage accounting, native Windows validation, cross-host
golden equality and the remaining acceptance requirements are unfinished. The macOS
process evidence is not a cross-platform or production-readiness claim.

Fingerprint principal-state accounting now requires the actual run allocator
for schema/recipe/asset/graph/scene and metadata/material helpers, without adding
heap allocation. One recipe reservation covers its pending result and schema
hash/callback; graph-plus-scene hashing resets and reuses one hash instead of
constructing two. Integer encoding scratch is part of the charged fingerprint
type. Metadata/material include their principal canonical writer/hash/sorting
state, and environment inspection reserves its semantic hash/pending digest.
The fixed v1 settings digest is static and independently checked against a hash
of the canonical settings bytes, eliminating repeated runtime SHA work entirely.
Adapter/reuse callers propagate reservation failures as resource errors rather
than metadata/version errors or cache misses. Regression tests prove exact
principal budgets, one-byte-short refusal, no new allocations, prior-output
preservation and drained charges. Existing nested publication/no-op/rollback
tests retain their exact-budget and I/O assertions with the new key phase.

Local validation on 2026-11-05 after fingerprint accounting: the serial macOS
full run executes 2,614 engine tests (2,609 pass, five skips), including all 626
cooker cases. An earlier concurrent validation run had one importer failure
(`SeparateImporterInstancesRemainIndependent`, glTF directory rejected); 20
isolated repetitions and the serial full run pass without changing that test.
Native ARM64 Linux executes 2,607 engine tests (2,598 pass, seven skips and the
same two software-Vulkan grid-label pixel failures); all 626 cooker cases pass.
All 1,265 editor cases and five process CTests pass on both hosts. Installed
scene/cook scripts pass plain/Unicode paths on both hosts, and all four real
Linux tmpfs ENOSPC cases pass with the rebuilt installed helper. Built-in and
custom-schema packages compare byte-for-byte between macOS and Linux, including
their manifests and all objects; manifest digests remain `0db1ca584106b8a31bd9c0a1bc414203bc66a775c0e80b75518f09c8a4c1c7c7`
and `53fb0471850d4b9209860e07aa1fcc9caa46a9958a2715db14dce154ca789d56`.
All 18 affected production/test translation units cross-compile for Windows and
Linux; four initial Windows compiler-cache `NoSpaceLeft` failures pass on serial
retry. The macOS installation refresh exits zero but logs external-framework
`install_name_tool` header-padding errors; successful installed-helper execution
is not proof of a clean framework installation. That installation diagnostic
still needs resolution/audit. This is a refreshed installation, not a new-prefix
install proof. Native Windows/full three-host equality, complete helper/parser/
decoder/hash/compiler-frame and remaining caller/service storage accounting,
near-limit acceptance and offline SampleProject smoke remain open. The live
SampleProject editor remains untouched.

Material parsing now uses caller-owned `MaterialAssetReader` storage: one owned
document, one parser and one shared 37-byte token buffer with the existing field
limits. Its borrowed result lasts until the next read/destruction; failures
expose no partial result, and overlapping input is rejected. Expected identity
may safely borrow the previous result. The cooker constructs the complete reader
in its existing single scratch allocation alongside observed input bytes, not a
second full document on the call stack. The convenience strong-output wrapper
remains available but is no longer used by the cooker. Inspection and encoding
reserve their principal dependency/digest and resolved-path/output/writer state
without adding allocations. Exact/one-byte-short and injected refusal tests
preserve prior outputs, sink state and the reusable backing-allocation count.

The canonical JSON writer now owns one 64-byte inline buffer for escapes,
integers, floats, UUIDs and chunked lowercase hex; manifest/material emitters
share those primitives instead of duplicating formatting buffers. Same-writer
sink reentry fails before mutation, with sticky `InvalidPath`, even when the sink
ignores the nested error; callbacks may use independent writers. Parser reset
reuses its existing inline stack without constructing another reader. Strict
material path validation now uses a shared validation-only `VFSRelativePath`
entry point, avoiding construction/copy of a full path. Owning parse and
validation-only calls use the same UTF-8/spelling/length/component algorithm.

Individual compiler-frame measurements use Apple Clang, arm64 C++20, explicit
Xcode SDK, `-O0 -g0 -fstack-usage`, not the complete CMake build profile. The old
material convenience parser's frame is 6,816 bytes; the cooker-used owned-reader
`Read` frame is 1,152 bytes. The remaining convenience wrapper is 6,224 bytes,
but the cooker does not call it. Adapter `ReadMaterial` drops from 496 to 368
bytes, JSON `Float32` from 272 to 208 bytes, and material `ValidTexture` from
400 to 96 bytes after validation-only paths. These are measured individual
frames, not complete call-stack bounds or a full memory-budget proof.

Local validation on 2026-11-05 after reader/JSON/path-workspace changes: macOS
runs 2,626 engine tests (2,621 pass, five existing skips), including all 628
cooker cases. The final native run selects
`VK_DRIVER_FILES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json` explicitly;
an earlier sandboxed run lacked Metal/FSEvents/filesystem access, and an earlier
native auto-discovery run loaded a stale missing kosmickrisp driver, causing
unrelated validation failures. Neither run was counted as a passing profile or
used to weaken tests. Native ARM64 Linux (Clang 18.1.3/libstdc++ 13, Debug) runs
2,619 engine tests (2,610 pass, seven skips and the same two software-Vulkan
grid-label pixel failures); all 628 cooker cases pass. This profile is not the
LLVM 22/x64 CI profile. All 1,265 editor tests and five process CTests pass on
both hosts. Refreshed installed scene/cook scripts pass plain/Unicode paths on
both hosts; all four real Linux 2 MiB tmpfs ENOSPC recovery cases pass. Complete
built-in/custom-schema package trees, not only manifests, compare byte-for-byte
between hosts, retaining manifest digests
`0db1ca584106b8a31bd9c0a1bc414203bc66a775c0e80b75518f09c8a4c1c7c7` and
`53fb0471850d4b9209860e07aa1fcc9caa46a9958a2715db14dce154ca789d56`.
The reader/writer and validation-only translation units cross-compile for
Windows/Linux; the Windows validation-only check uses GNU-target release
assertions, not native MSVC execution. The macOS refresh installs only the test
subdirectory into the existing prefix, not a fresh complete dependency install;
the prior external-framework header-padding diagnostic remains open. Complete
helper/compiler/library-stack and caller/service memory accounting, native
Windows/three-host package equality, near-limit acceptance, offline SampleProject
smoke and the final requirement audit remain unfinished. The live SampleProject
editor stays untouched.

Manifest syntax preflight now walks containers iteratively, with 16 default-
initialized semantic frames inside its existing charged owner. Its second pass
uses `JSONReader::Reset`, not another parser temporary. Root/asset compatibility
roles, strict unknown-nested-object duplicate checking and the depth-16 limit
remain unchanged; the existing one key-index allocation is unchanged. Four new
tests cover every allowed depth/overflow, mixed/empty siblings, compatibility
scope, deep duplicate/malformed input, prior-ledger preservation and exact/
one-byte-short budgets. Under the explicit standalone Apple Clang arm64 C++20
`-O0 -g0 -fstack-usage` profile, the old recursive value frame was 688 bytes;
the new nonrecursive value dispatcher is 544 bytes and controller is 160 bytes.
Parser restart drops from 288 to 160 bytes. These measurements do not bound the
complete helper/library/sort/compiler stack.

Validation on 2026-11-05 after iterative syntax traversal: macOS runs 2,630
engine tests (2,625 pass, five existing skips); native ARM64 Linux runs 2,623
(2,614 pass, seven skips and the same two software-Vulkan grid-label failures).
All 632 cooker cases, 1,265 editor tests and five process CTests pass on both
hosts, using the same explicit MoltenVK and native Linux profiles above.
Refreshed installed plain/Unicode scene/cook protocols and all four Linux
tmpfs ENOSPC cases pass. Complete built-in/custom-schema package trees compare
byte-for-byte across macOS/Linux, with unchanged reviewed manifest digests.
Both affected translation units cross-compile for Windows/Linux; the test
cross-check explicitly includes the generated golden-fixture header directory.
Native Windows/full three-host equality, complete memory and near-limit audits,
the prior complete macOS installation diagnostics, offline SampleProject smoke
and final requirements audit remain open. No live SampleProject operation was
performed.

The separate `SceneCookResourceProcess` now exercises actual texture resource
boundaries without making every unit-test shard allocate large images. Generated
immutable TGA ranges and a byte-verifying streaming sink keep fixture/output
storage constant-sized. An RLE 16384x8192 RGBA8 image fully decodes exactly
536,870,912 bytes from 5,242,898 encoded bytes. Inspection and exact passthrough
pass; repeating encoding at the observed allocation/reservation peak produces
the same bytes. One byte less rejects before any pixel mapping, preserves the
prior inspection and emits nothing. Lower decoded/dimension caps and over-cap
headers likewise reject without decoder allocation. An uncompressed 16384x8191
image streams all 536,805,394 bytes, within 64 KiB of both 512 MiB resource caps;
one-byte-short encoded/decoded limits reject before output. Additional ordinary
tests accept horizontal/vertical 16,384-pixel PNG/BMP/TGA strips and reject 16,385.

This acceptance work exposed a speculative allocation in format recognition:
stb's JPEG type probe allocates a complete decoder merely to recognize SOI,
including while identifying TGA or unsupported bytes. Shared CPU recognition
now matches that marker and its permitted 0xff fill without allocating; full
header/decode validation is unchanged. Regression tests cover filled JPEG
markers, TGA with/without its maximum-length ID, zero-allocation TGA dimension
rejection and unsupported-byte recognition. The resource process is an installed,
serialized CTest with a 600-second timeout and explicit artifact/CI wiring on
all three hosts. Filtered or zero-repeat invocations cannot claim gate success.

Validation on 2026-11-05 after resource acceptance: macOS runs 2,633 engine tests
(2,628 pass, five existing skips); native ARM64 Linux runs 2,626 (2,617 pass,
seven skips and the same two software-Vulkan grid-label failures). All 633 cooker
cases, 1,265 editor tests, three large-resource cases and the existing five
process protocols pass on both hosts. The observed decoded-cap allocator/
principal-reservation peaks are 536,888,100 bytes on macOS and 536,875,812 on
Linux, including their different native mapping granularity. These are not
complete compiler-stack/RSS measurements or whole-cook budget bounds, and do
not select a working-memory default. Refreshed installed resource/scene/cook
helpers pass on both hosts; all four real Linux tmpfs ENOSPC cases still pass.
Fresh built-in/custom-schema package trees compare byte-for-byte across hosts
with unchanged reviewed manifest digests. All four affected translation units
cross-compile for Windows/Linux, not native Windows execution evidence. Workflow
YAML parses locally; the new CI gate has not run in CI. The macOS installation
refresh remains test-only into an existing prefix. Broader near-limit codec/
mesh/environment/scene and aggregate temporary-storage acceptance, complete
memory accounting, native Windows/three-host equality, clean macOS installation,
offline SampleProject smoke and the final requirements audit remain open. The
live SampleProject editor remains untouched.

The resource process now shares one bounded generated-capture/sink fixture and
adds environment/mesh acceptance rather than retaining second large fixture
buffers. The current environment format's actual maximum is a 1024x1024,
six-face RGBA32f cube: 100,663,296 payload bytes (96 MiB), not the generic 512 MiB
asset ceiling. Inspection and exact byte passthrough each use one reusable stream
allocation. Exact observed working budgets pass; one-byte-short budgets refuse
without output. Encoded/decoded/dimension caps reject before payload traversal,
and a NaN in the final scalar or a final-payload read failure preserves prior
inspection and drains all charges.

A valid v1 mesh with the full 8,388,608 vertices uses 536,870,897 decoded workspace
bytes (15 bytes below 512 MiB) and 536,870,436 encoded bytes. Inspection and
encoding each make one native workspace allocation; passthrough reuses that
same arena after destroying the decoded containers. Exact decoded and observed
working limits pass with byte-identical output. One-byte-short decoded or mapping
budgets refuse before mapping. The shared reader now exposes its actual named
fixed-storage bound (including the 16 KiB inline reader buffer, pending decoded
objects and arena helpers). The adapter reserves it and its bridge/limits state
before measurement/decoding and holds it across the workspace allocation. A
budget one byte below the observed full-operation peak therefore now rejects
before mapping too, preserving inspection and emitting nothing. No allocation
is added, measurement/decode reservations are not summed as concurrent, and
unsupported reservation contracts are backend errors, not mesh-version errors.
Over-count headers, encoded caps, a final invalid index and
late read failures preserve the previous inspection and drain native mappings.

On 2026-11-05 all eight resource cases (including the three texture cases) pass
from built and refreshed installed executables on macOS arm64 and native ARM64
Linux. All six macOS process CTests and the five installed Linux process protocols
pass, as do all 635 cooker regression tests on both hosts. Observed
environment allocator/principal-reservation peaks are 86,632 bytes
on macOS and 74,344 on Linux; corresponding mesh peaks after the reader
reservation are 536,904,976 and 536,892,688 bytes. macOS runs 2,635 engine tests
(2,630 pass, five existing skips); Linux runs 2,628 (2,619 pass, seven skips and
the same two software-Vulkan grid-label failures). All 1,265 editor cases pass
on both hosts. Additional failure injection covers reservation refusals before
reads/allocations and drainage on success, read faults and allocation failure;
repeated decoding retains the existing single backing allocation. Fresh complete
built-in/custom package trees still compare byte-for-byte with unchanged manifest
digests; all four installed Linux tmpfs ENOSPC gates pass again.

These remain partial modeled ownership/reservation figures, not complete
compiler/helper stack, RSS or whole-cook budget proof. A standalone Apple Clang
arm64 `-O0 -fstack-usage` audit measures 17,440/17,488-byte layout/decode frames,
560-byte shared validation and adapter-read frames, and 1,904/1,392-byte inspect/
encode frames. Those individual figures are not a complete call-chain bound;
recursive sort, container/UTF-8 helpers and compiler spills still require audit.
The install refresh remains test-only,
not a clean full installation. Remaining per-format/scene and aggregate resource/
temporary-storage acceptance, complete memory accounting, clean macOS installation,
offline SampleProject smoke and the final requirements audit stay open. Windows
testing is left to the user; the live editor remains untouched.

Subsequent clean macOS installation acceptance on 2026-11-05 closes that
installation gap for the tested Debug arm64 profile. A full `cmake --install`
into an initially empty prefix exposed inherited absolute dependency rpaths
that exceeded the FreeType/Vulkan frameworks' Mach-O header capacity, followed
by stale signatures after other installed targets' load-command rewrites.
The system-only dependency targets now have empty install rpaths; Obelisk uses
`@loader_path/../lib`, as the tests already do. A shared Apple-only install hook
signs and strictly verifies each affected real installed target after its
upstream install rule. Fetched sources and Windows/Linux behavior are unchanged.
This is ad-hoc local/CI signing, not distribution signing or notarization.

The fresh full installation succeeds without the prior framework errors. The
entire prefix was then moved, not copied, to a path containing spaces and
Japanese characters, leaving its original location absent. From `/private/tmp`,
with all DYLD library/framework/fallback paths and `LD_LIBRARY_PATH` unset,
2,630 of 2,635 installed engine tests pass (five existing skips), all 1,265
installed editor tests and all eight resource cases pass, and installed
scene/cook plain/Unicode process protocols and `YAMLAllocationProcess` pass.
The engine tests use the explicit MoltenVK driver profile above. Installed
Obelisk `--help`, glslang `--version` and spirv-cross `--revision` also run;
no editor/project is launched. All 15 installed Mach-O files strictly verify
after relocation and have no checkout-linked dependency paths; the inspected
engine/editor/resource executables and Obelisk have only their loader-relative
library rpath. An isolated signing fixture additionally passes a Unicode/space
`DESTDIR`/prefix and fails installation for missing/symlink targets, injected
signing-launch failure and injected strict-verification failure.

The macOS installed-artifact CI tests no longer set `DYLD_LIBRARY_PATH`, so
they gate relative installed linking rather than override it. Workflow YAML
parses locally; CI itself has not run. The shared CMake changes regenerate/build
as a no-op in the native ARM64 Linux fixture. This is a new empty-prefix full
installation from the existing Debug build, not a clean-source rebuild or
Release-profile proof. Complete memory accounting, remaining per-format/scene
and aggregate resource/temporary-storage acceptance, offline SampleProject
smoke and the final requirements audit remain open. Windows testing stays with
the user, with its existing CI wiring retained. The live SampleProject remains
untouched.

Mesh inspection now emits its material slots directly in decimal lexical wire
order (`0`, `1`, `10`, ...), rather than constructing numeric order and applying
a comparison sort. The iterative prefix walk visits every submesh once,
including children of nil-material parents, in O(submeshes) time and constant
scalar storage. Original slot indices and encoded mesh bytes do not change;
no table, allocation or recursive sort frame is introduced. The existing
temporary edge/text block and compact inspection allocation remain; this is
not a claim that the whole adapter is allocation-free.

On 2026-11-05 sixteen new decimal-boundary regressions through the format's full
8,192 submeshes compare dense/sparse/alternating-identity results with an
independent string-sort oracle. They also prove byte-exact passthrough, unchanged
allocation counts, drained mappings/charges and one-edge-short refusal before
output without mutating prior inspection. All 651 cooker tests pass on both
macOS arm64 and native ARM64 Linux. Full engine runs are 2,651 on macOS (2,646
pass, five existing skips) and 2,644 on Linux (2,635 pass, seven skips and the same
two software-Vulkan grid-label failures); both pass all 1,265 editor tests and
six process CTests. Refreshed installed plain/Unicode protocols and all eight
resource cases pass on both hosts, as do the four installed Linux tmpfs ENOSPC
gates. The macOS refresh uses the relocated prefix without library overrides;
all five refreshed test signatures strictly verify. Fresh complete built-in/
custom package trees compare byte-for-byte with unchanged reviewed manifest
digests. A standalone Apple Clang arm64 `-O0 -g0 -fstack-usage` sample measures
32 bytes for the new index helper and contains no mesh sort instantiations.
That is not complete call-chain/working-memory proof: other sorts, helper/
compiler stacks, ownership and remaining resource/aggregate limits remain open.
Windows testing remains user-owned; the live SampleProject editor is untouched.

The subsequent hash/helper audit fixes object-only SHA-256 reservations. The
shared `SHA256::FixedWorkingBytes()` includes the stream and the pinned sha-2
implementation's simultaneously live `ah[8]`/`w[16]` compression arrays (96
additional bytes); `CookFingerprint` composes that bound once. Capture/recheck,
adapter receipt/passthrough, metadata/material/recipe keys, scene codecs,
manifest writing, publication and object verification now reserve it before
use. Exact-budget/refusal gates include the new bound and preserve previous
outputs. No new allocation or retained hash state is added. Redundant reset
clears are removed without changing member default initialization or treating
reset as secure erasure. Reuse regressions check 200 known answers after
finalized/unfinished prior streams at ten compression/padding boundaries.

Metadata fingerprinting also replaces its general-purpose sort and subsequent
duplicate scan with one bounded insertion pass over the same 32-byte index
array. Caller settings remain untouched, equal keys fail before publishing a
digest, and no recursive sort helper or second buffer remains. This intentionally
small-table algorithm has at most 496 comparisons/shifts at the 32-setting hard
cap, not a quadratic strategy for large catalog/graph tables. Eight boundary
cases exercise five orderings, zero allocations, exact and one-byte-short
budgets; separate tests check all 720 six-key permutations and all 496 duplicate
position pairs at full capacity. Existing UTF-8/escaping and digest goldens pass.

Validation on 2026-11-05 after these changes: all 662 cooker tests pass on
macOS arm64/native ARM64 Linux. Full engine runs are 2,663 on macOS (2,658 pass,
five existing skips) and 2,656 on Linux (2,647 pass, seven skips and the same two
software-Vulkan grid-label failures); both hosts pass all 1,265 editor tests and
six process CTests, including all eight large resource cases. The macOS engine
run explicitly selects the baseline MoltenVK driver; an initial unselected run
encountered an unrelated broken Vulkan driver registration. Environment modeled
reservation/allocation peaks now include the extra 96 bytes: 86,728/74,440 bytes
(macOS/Linux). Mesh and texture modeled peaks are unchanged. The prior hash-only
installed refresh also passed all eight resource cases on both hosts and all
four Linux tmpfs ENOSPC gates; the final metadata refresh passes 27 installed
metadata/hash tests and the plain/Unicode cook protocols on both. The relocated
macOS installation runs outside the checkout without library-path overrides,
and all five refreshed helper signatures strictly verify. Fresh complete
built-in/custom package trees are byte-identical across both hosts, with the
same reviewed manifest digests.

Standalone Apple Clang arm64 `-O0 -g0 -fstack-usage` samples show reset shrinking
from 240 to 48 bytes and metadata fingerprinting from 816 to 800 bytes, with no
metadata sort instantiations. These are individual compiler frames, not complete
call-chain, RSS or whole-run working-memory proof. The 96-byte query covers the
named upstream arrays, not all scalar/compiler/helper storage. Other sorts,
helper/parser/decoder call chains, remaining caller/service/VFS ownership,
per-format/scene and aggregate resource/temporary-storage limits, offline
SampleProject smoke and the final acceptance audit remain open. Windows stays
user-owned and the running SampleProject remains untouched.

Native-service setup now reuses the run's existing five-view snapshot for
registry/VFS/caller charges instead of independently rebuilding it through each
standalone wrapper. Public standalone query contracts are unchanged; callback
owners still perform their own complete memory-contract query and may inspect
native storage independently. Caller-value unioning retains only five
address/size pairs (80 rather than 160 bytes on arm64), insertion-ordered in the
same engine array with at most ten shifts. No allocation, retained state or
modeled service charge is added. Tests cover all 120 address permutations,
128 repeated cached projections without arena growth, and shared/distinct facade
queries in public memory/disk cooks with unchanged package/no-op behavior.

Two material acceptance cases now exercise the actual 1 MiB encoded hard cap
and 4096-byte decoded name. Fixed-size generated captures provide independent
input/output oracles without full fixture copies; canonical output is 25,200
bytes. Each successful inspect/encode uses one native workspace. Exact working
budgets pass and one-byte-short budgets fail; decoded/encoded limits, a
4097-byte name, 997-byte reads, late syntax corruption and final-read failure
preserve prior inspection, emit no output and release all workspace. Modeled
inspect/encode peaks are 1,066,032/1,066,740 bytes on macOS and
1,057,840/1,058,548 bytes on Linux; these are not RSS or complete call-stack bounds.

Validation on 2026-11-05 after this batch: 666 cook-related engine tests pass on
both hosts (659 in `tests/Cook`, plus seven environment-cooking tests). Full engine
runs are 2,667 on macOS (2,662 pass, five existing skips; baseline MoltenVK) and
2,660 on native ARM64 Linux (2,651 pass, seven skips and the same two software-
Vulkan grid-label failures). Both pass all 1,265 editor tests and six process
CTests, now including ten resource cases. Refreshed installations pass 22 focused
service/caller/integration tests, plain/Unicode cook protocols and all ten
resource cases. The relocated macOS helpers run outside the checkout without
library-path overrides; all five signatures strictly verify. Four installed
Linux tmpfs ENOSPC cases pass. Fresh complete built-in/custom package trees are
byte-identical across hosts, with unchanged reviewed manifest digests.

Standalone Apple Clang arm64 `-O0 -g0 -fstack-usage` samples show the old public
caller query's 992-byte frame replaced by a 576-byte cached projection frame and
a separate 576-byte standalone wrapper frame, with no sort instantiations in
that translation unit. The hot cook bypasses that wrapper, but standalone
call-chain savings are not established by comparing individual frames. Complete
helper/parser/decoder/callback/compiler and remaining caller/service/VFS memory
accounting, other-format/scene and aggregate resource/temporary-storage tests,
offline SampleProject smoke and the final requirements audit remain open.
Windows remains user-owned; the live SampleProject editor is untouched.

Scene/batch and aggregate acceptance on 2026-11-05 adds three memory/disk
integration cases (six tests). A real 256-scene request shares one material and
publishes 257 referenced objects; every scene binary is independently hashed,
decoded and checked against its discovered UUID. Every manifest scene root and
all object byte counts are verified. A 255-scene configured cap and a 257-source
request refuse before run allocations and preserve the prior result/package. Reversed request order,
forced encoding and a doubled sufficient working budget retain identical bytes.
The test keeps the same 4 MiB reclaimable working slab; its larger memory-backend
fixture capacity and 128 MiB starting budget are test policy, not engine presets.

An eight-scene/material/texture fixture counts all 19,970 unique captured bytes,
including both sidecars, and 10,597 newly installed object/manifest bytes. Exact
capture/staging caps succeed; one-byte-short caps preserve every result field and
the old manifest, drain scratch/allocator storage and leave no temporary files or
writer lock. Exact staging is checked in a separate cold root, not made cheaper
by a failed run's orphan objects. Recovery in the failed root verifies those
immutable objects and publishes with only manifest-sized retained staging space.
A separate forced-rebuild fixture searches and rechecks a one-byte working-budget
boundary rather than assuming a high-budget peak is the minimum. Measured bytes:

| Fixture | macOS memory | macOS disk | Linux memory | Linux disk |
| --- | ---: | ---: | ---: | ---: |
| 256-scene modeled peak | 40,779,524 | 7,323,396 | 40,709,748 | 7,323,252 |
| Eight-scene minimum sufficient working budget | 40,678,340 | 7,222,212 | 40,608,564 | 7,222,068 |

Two additional serialized resource cases exercise the actual 32 MiB scene-input
cap using bounded generated whitespace, not a second full fixture copy. Read and
encode pass at their measured exact budgets and refuse one byte less; encoding
still produces the same independently decoded 463-byte binary as the small
unpadded source. Lower/over-limit inputs refuse before reads/allocations; late
997-byte read failure, receipt mismatch and independently rehashed malformed YAML
preserve prior documents/results, emit no sink bytes and drain all routed storage.
Read/encode modeled peaks are 33,966,856/33,967,464 bytes on macOS and
33,733,384/33,733,992 bytes on Linux. Fixture preparation/emitter/verification
storage is outside these direct-codec measurements.

Both hosts pass all 672 cook-related engine tests, six process CTests (all twelve
resource cases actually executed), 1,265 editor tests, six new installed batch
tests, all twelve installed resource cases and installed plain/Unicode cook
protocols. Full engine runs are 2,673 on macOS (2,668 pass, five existing skips)
and 2,666 on Linux (2,657 pass, seven skips and the same two GPU grid-label
failures). Five refreshed macOS helper signatures strictly verify; four installed
Linux bounded-tmpfs ENOSPC cases pass. Fresh complete built-in/custom package
trees remain byte-identical across hosts with unchanged reviewed manifest digests.

This closes the named scene-count/source-byte and configured aggregate boundary
gates, not all resource acceptance. The padded scene has small semantic content;
maximum entity/node/decoded-tree density, broader format/combined-large-resource
and temporary-storage stress, complete helper/compiler/callback memory accounting,
remaining caller/service/VFS ownership, offline SampleProject smoke and the final
requirements audit remain open. These are modeled ledger peaks, not RSS or
complete call-chain bounds. Windows stays user-owned and the live editor remains
untouched. No production allocation or API/format behavior changes in this batch.

Sorting-state accounting on 2026-11-05 replaces the schema-field and four catalog
index `std::sort` calls with shared internal iterative heapsort. Its borrowed,
default-initialized state holds one moved value and fixed loop indexes: no
recursion, dynamic allocation, record copies or table-sized helper stack, with
O(n log n) worst-case comparisons. The schema callback owns this state inside its
already reserved context, preserving one reservation for schema/recipe hashing.
Multi-entry catalogs share one reservation across all four index sorts and refuse
before touching indexes when it fails; an exact-budget retry remains possible.
Empty/single-entry catalogs do not reserve unused sort state. Records remain
stationary and lookup/merge complexity and canonical bytes are unchanged.

Five new tests cover all permutations through eight elements, large tables through
8,193 elements (sorted/reversed/equal/duplicate/shuffled, both comparator orders,
range guards and an explicit n-log-n comparison bound), move-only value ownership
and repeated state reuse, catalog reservation failures/exact-budget retries, and
empty/single-entry fast paths. The 257-field schema test additionally checks
reversed field registration produces the same digest without moving registry
fields. Schema/recipe exact-budget/refusal tests still use one reservation and no
allocations, now including the sorting state.

Both hosts pass all 677 cook-related engine tests, 1,265 editor tests, six process
CTests (all twelve resource cases executed), 19 relocated/installed focused tests
and installed plain/Unicode cook protocols. Full engine runs are 2,678 on macOS
(2,673 pass, five existing skips) and 2,671 on Linux (2,662 pass, seven skips and
the same two GPU grid-label failures). Five refreshed macOS helper signatures
strictly verify. Fresh complete built-in/custom package trees are byte-identical
between hosts and retain the reviewed manifest digests. This removes recursive
sorting from these five call sites only: graph/ready-queue, manifest, frozen-root/
file and scene-capture sorting, complete helper/compiler/callback call chains,
and remaining caller/service/VFS ownership accounting are not thereby validated.
Semantic-density/combined-large-resource/temporary-storage stress, offline
SampleProject smoke and the final acceptance audit remain open. Windows remains
user-owned; the live editor and SampleProject are untouched. No new runtime
allocation or wire-format/API-signature changes are introduced.

Dependency-graph sorting-state accounting on 2026-11-05 extends the same borrowed
state to global-UUID, canonical-node and occurrence sorting and the topological
ready queue. One scoped reservation covers that principal state before any
finalization workspace mutation; reservation refusal leaves discovery retryable.
Empty graphs do not request unused sorting storage, and invalid state/uninspected
nodes refuse before querying the budget. The initial ready frontier is collected
once and heapified bottom-up in O(V), not built using V logarithmic pushes.
Subsequent pushes/pops are O(log V), with no allocation or recursion. Sorting,
heap construction and queue removal share one iterative hole-filling sift rather
than duplicate implementations. Canonical ordering and graph table ownership are
unchanged.

Six additional tests cover exhaustive queue permutations through seven elements,
8,193-element interleaved/duplicate/adversarial min/max queues with per-operation
comparison bounds, linear heap-construction comparisons, move-only queue ownership,
graph exact-budget/refusal/retry and pre-budget validation. Existing scene-root
and graph fixtures retain their original slabs and now provide separate fixed-
storage ledger headroom; this is test policy, not an engine memory preset. The
manifest reader's reservation-failure test discovers every fixed-charge seam
from a successful read instead of assuming three, covering the new graph charge
and preserving the previous manifest and retained storage on every refusal.

Rechecked modeled batch budgets include the new graph reservation:

| Fixture | macOS memory | macOS disk | Linux memory | Linux disk |
| --- | ---: | ---: | ---: | ---: |
| 256-scene modeled peak | 40,779,572 | 7,323,444 | 40,709,796 | 7,323,300 |
| Eight-scene minimum sufficient working budget | 40,678,388 | 7,222,260 | 40,608,612 | 7,222,116 |

Both hosts pass all 683 cook-related engine tests, 1,265 editor tests, six process
CTests (all twelve resource cases), 72 refreshed installed focused tests and
installed plain/Unicode cook protocols. Full engine runs are 2,684 on macOS
(2,679 pass, five existing skips) and 2,677 on Linux (2,668 pass, seven skips and
the same two GPU grid-label failures). Five macOS helper signatures strictly
verify; all four installed Linux bounded-tmpfs ENOSPC cases pass. Fresh complete
built-in/custom package trees remain byte-identical across hosts with unchanged
reviewed manifest digests. These are principal-state ledger measurements, not
RSS or complete compiler/helper/callback call-chain bounds. Manifest, frozen-root/
file and scene-capture sorts, diagnostic temporaries/Describe and other helper/
parser/decoder/callback/compiler frames, remaining caller/service/VFS ownership,
semantic-density/combined-large-resource/temporary-storage stress, offline
SampleProject smoke and final acceptance remain open. The live editor is still
running and untouched; Windows remains user-owned. No runtime allocations or
wire-format/API-signature changes are added.

Remaining cooker sorting-state accounting on 2026-11-05 converts manifest JSON
key uniqueness, descriptor-size conflicts, scene UUID ordering, frozen physical/
logical file ordering, move-only root bindings and scene-capture path indexes to
the same iterative constant-storage helper. Reader, writer and publication retain
one shared descriptor-conflict implementation and include its borrowed state in
their existing principal reservations. Syntax state includes its typed key-sort
state in `sizeof(SyntaxPass)`. Writer structural validation still performs no
budget query; sorting and conflicting-descriptor checks now occur only after its
single reservation succeeds. Budget refusal cannot modify the caller's workspace,
write sink bytes or replace the output. Conflict errors still precede all writes.

Frozen-file discovery reserves one shared record-sort state before enumeration;
refusal leaves the file set empty and allocates nothing. Root initialization
reserves before binding allocation/pin acquisition, and multi-scene capture
reserves before its owner/index table. Root and scene-capture state is released
after sorting, before later validation/codec phases; known singleton root/capture
requests do not reserve unused sorting state. The empty VFS base-lease constructor
is explicitly non-throwing, matching its null/empty members rather than weakening
the helper's type requirement. No `std::sort`/standard heap calls remain in the
cooker directory; this does not audit sorting reached through external codecs
or caller callbacks.

Four new tests cover root pre-allocation refusal/exact-peak retry with valid moved
pins, the singleton-root fast path, file-set exact payload-plus-state refusal/
retry and capture refusal before owner allocation. Existing writer tests now
check unchanged workspace on reservation failure and charged conflict checking
with unchanged result/sink. Allocation-failure test wrappers forward fixed-state
reservations so their intended allocation seams remain covered. Successful runs
retain their existing allocation counts; no runtime allocation was introduced.

Both hosts pass 687 cook-related engine tests, 1,265 editor tests, six process
CTests (all twelve resource cases), 169 refreshed installed focused tests and
installed plain/Unicode cook protocols. Full engine runs are 2,688 on macOS
(2,683 pass, five existing skips) and 2,681 on Linux (2,672 pass, seven skips and
the same two GPU grid-label failures). Five macOS helper signatures strictly
verify; four installed Linux bounded-tmpfs ENOSPC cases pass. Fresh complete
built-in/custom package trees remain byte-identical across hosts with unchanged
reviewed manifest digests. Updated modeled batch measurements are:

| Fixture | macOS memory | macOS disk | Linux memory | Linux disk |
| --- | ---: | ---: | ---: | ---: |
| 256-scene modeled peak | 40,779,612 | 7,323,484 | 40,709,836 | 7,323,340 |
| Eight-scene minimum sufficient working budget | 40,678,428 | 7,222,300 | 40,608,652 | 7,222,156 |

Adjacent one-byte-short budgets still refuse. These are principal-state ledger
measurements, not RSS or complete compiler/helper/callback call-chain bounds.
Diagnostic temporaries/Describe, other helper/parser/decoder/callback/compiler
frames, remaining caller/service/VFS ownership, maximum scene semantic density,
combined-large-resource/temporary-storage stress, offline SampleProject smoke
and final acceptance remain open. Windows stays user-owned; the live editor and
SampleProject remain untouched. Public cook signatures and wire formats are
unchanged.

Diagnostic construction on 2026-11-05 removes the remaining full-record locals
from cooker error/warning helpers. `CookDiagnostics::Emplace` reserves the next
fixed caller-owned slot, normalizes warning severity and updates saturating
error/drop counters using the same retained-slot path as copied `Add`. It
constructs the record directly in that slot and returns a synchronous borrowed
pointer for provenance/message filling. A full buffer returns null without
constructing another record, but errors remain visible. It requires no new
allocation or failure-time memory reservation. Retained code/severity must match
the finalized counters. Clearing reconstructs retained entries in place instead
of assigning a large default temporary; copied `Add` still supports aliasing.

Adapter/dependency/catalog/input-lease/capture/scene-codec/manifest/publication/
incremental-reuse/top-level-run helpers and graph discovery/finalization now use
that route. Full graph buffers skip unretained cycle provenance construction
without changing the corrupted result or error visibility. `Describe` separately
validates graph keys and the complete explanatory text, then enriches the
caller's record without a second diagnostic or key-copy scratch buffers. It
preserves every output byte on failure, accepts text aliased to old output
fields, resets stale fields/tails and retains existing bounded-chain behavior.
Seven new tests across this diagnostic work cover aliasing, UTF-8 boundaries,
transactional failures, direct slot/reset behavior, mixed copied/emplaced and
self-aliased copies, normalized warnings, saturation/overflow, and full-buffer
cycle/global-UUID conflicts without payload allocation.

Standalone Apple Clang arm64 samples using the existing Debug response arguments
with `-O0 -g0 -fstack-usage` measure `Describe` shrinking from 1,712 to 272 bytes,
graph finalization from 3,584 to 672, diagnostic clearing from 1,568 to 128,
adapter failure from 1,584 to 112, manifest diagnostics from 1,584 to 96, and
top-level run failure from 1,600 to 96. Emplacement itself measures 160 bytes.
These are individual compiler frames, not complete call-chain, RSS or whole-run
memory bounds. No storage member, working-budget preset, cook signature or wire
format changes; existing modeled resource-budget measurements remain unchanged.

Final rebuilt verification passes 694 cook-named engine tests on each host,
1,265 editor tests, six process CTests (all twelve resource cases), 68 refreshed
installed focused tests and installed plain/Unicode cook protocols. Full engine
runs are 2,695 on macOS (2,690 pass, five existing skips) and 2,688 on Linux
(2,679 pass, seven skips and the same two GPU grid-label failures). Five installed
macOS helper signatures strictly verify; all four installed Linux 2 MiB tmpfs
ENOSPC recovery cases pass. Fresh complete built-in/custom package trees remain
byte-identical across hosts with the unchanged reviewed manifest digests.
Remaining helper/parser/decoder/callback/compiler call-chain and caller/service/
VFS ownership accounting, maximum scene semantic density, combined-large-resource/
temporary-storage stress, offline SampleProject smoke and final requirement audit
remain open. Windows stays user-owned; no Windows testing/cross-compilation or
operation against the live editor/SampleProject was performed.

Native callback profiling on 2026-11-05 now reuses the run's existing five-service
memory snapshot instead of constructing a second snapshot and requerying VFS
facades. The common root-coverage walk remains mandatory. Only the exact final
native callback-owner type receives that projection; custom owners, including
ones declaring a native arena, still receive their complete footprint query once
with the covered allocator and preserve all refusal categories. Standalone
profiling keeps its independent snapshot when needed. No storage member,
allocation, ownership hook, budget preset or output format was added.

Three new tests cover the shared/standalone native footprint, no-growth/no-
allocation repeat projections, one-query facade reuse, native foreign-root
rejection and mandatory custom-owner contract/failure propagation. Existing
multi-registry coverage repeats the shared query 128 times. The public cooker
integration now covers native/custom callback owners with shared/distinct VFS
facades on both backends: each facade is profiled once and package bytes agree.
Final rebuilt runs pass 697 cook-named engine tests per host, 1,265 editor tests,
six process gates (all twelve resource cases), 29 installed focused tests and
installed plain/Unicode protocols. Full engine totals are 2,698 on macOS (2,693
pass, five existing skips) and 2,691 on Linux (2,682 pass, seven skips and the same
two GPU grid-label failures). Five installed macOS helper signatures strictly
verify; four installed Linux 2 MiB tmpfs ENOSPC cases pass. Fresh complete
built-in/custom packages are byte-identical across hosts with unchanged reviewed
digests and modeled resource-budget boundaries. This closes duplicate native
callback profiling, not complete helper/compiler/callback stack/RSS or ownership
accounting. The remaining semantic-density/combined-resource/temporary-storage
stress, offline SampleProject smoke and final requirements audit stay open.
Windows stays user-owned; no Windows test/cross-compile or live editor/project
operation was performed.

Scene entity-density acceptance on 2026-11-05 now supplements the whitespace-
padded 32 MiB source test with 65,536 actual entities. A constant-storage capture
borrows the bounded settings prefix and synthesizes descending UUID rows for
arbitrary read ranges; neither a large fixture string nor a second binary copy
is retained. The sink checks the complete canonical ascending entity table,
empty component ranges, wire header/CRC, scene UUID, exact length and output
digest. This is wire inspection, not a second independent scene decoder or a
public-cooker publication test.

The generated input is 4,458,536 bytes; encoding produces 1,573,327 bytes. Read/
encode pass at their exact measured principal working budgets and refuse one
byte less without sink writes or output replacement. Peaks are 72,076,040 /
72,076,648 bytes on macOS and 71,719,688 / 71,720,296 on native ARM64 Linux.
Both paths also accept the exact 327,855-input-node boundary. One fewer node,
lower entity/collection/decoded-memory ceilings and a 65,537th entity report
structured budget failures, preserve an independently owned previous document
and drain tentative storage. The existing 96 MiB fixture ceiling is unchanged.
All six process gates, including all fifteen resource cases, pass on both hosts.
Refreshed installed helpers also pass all fifteen cases: macOS from the moved
Unicode installation without library-path overrides, Linux with no source or
dependency mount. All five installed macOS helper signatures strictly verify.
Only tests changed; these measurements do not establish whole-stack/RSS bounds,
the default 1,048,576-node maximum, component/value density, combined-large-
resource publication or temporary-storage stress.

The actual offline SampleProject smoke also passed on 2026-11-05 after both the
previous editor PID and a fresh unsandboxed Obelisk process query were absent.
The installed helper mounted the project read-only and used a separate empty
temporary workspace. Initial publication, fresh-capture no-op and forced rebuild
all verified the same two-scene/two-object package, 1,971 referenced bytes and
manifest SHA-256 `30b78ef8967a55975f720f00a53ca57013d2b5ae38a3b0204557e737e491b3c6`.
The reported cook peak is 1,679,192 principal bytes; native mappings peak at
737,440 bytes, with 10,488 allocations and matching frees across the harness.
The sorted before/after SHA-256 inventories match for all 118 project files.
These configured scenes reference zero assets: this closes the actual configured
SampleProject smoke, not representative mesh/material/texture/sky or runtime
rendering acceptance. No editor was launched/stopped and no project files were
modified. Complete memory/ownership accounting, broader semantic-density and
combined-resource/temporary-storage acceptance, the final requirement audit and
user-owned Windows results remain outstanding.

Component/scalar-density acceptance on 2026-11-05 removes temporary key-array
reservation and sorting for YAML maps with zero, one or two fields. Two-field
maps compare the two stored key texts directly; larger maps retain their existing
sort. A new regression fills a borrowed arena completely before checking all
three tiny-map sizes and duplicate text in distinct key nodes. These checks need
no scratch bytes and preserve the existing malformed-map diagnostic behavior.
The generated source reader now copies a borrowed row slice directly into the
requested output and patches only intersecting UUID digits, without a temporary
row copy. Bounded component-row preparation retains 32 KiB, not a large scene
string; shared budget/refusal helpers avoid duplicating each density test.

Four new resource cases exercise 512 entities with 64 components each and
127-byte built-in name values, and 2,048 entities with actual 4,096-byte scalar
values through a registered typed string codec. Component-cap coverage uses
synthetic wire-only schema aliases of the real name codec, not candidate ECS
instantiation. Both entity and component source orders are descending; in-place
wire inspection checks all ascending entity/component rows, version/range/key
fields and complete string payloads. Exact working peaks succeed; one byte less
refuses without sink writes or output replacement. A 65th component, lower
component/scalar limits, a 4,097-byte scalar and a semantically invalid 128-byte
name preserve the previous independently owned document, distinguish budget
from invalid-scene diagnostics and drain tentative allocations.

The component source/binary sizes are 6,588,968 / 5,321,167 bytes. Principal read/
encode peaks are 74,205,960 / 74,206,568 bytes on macOS and 73,849,608 / 73,850,216
on Linux. The scalar source/binary sizes are 8,673,320 / 8,509,903 bytes, with peaks
42,369,912 / 42,370,520 on macOS and 42,238,840 / 42,239,448 on Linux. Reported
binary SHA-256 values agree across hosts: component
`bddbd138977395c08c53e3530567967ce47a9a40baf7351c7bb559c3de8b8ed6`, scalar
`f63efd13bd8759e07cb46057d7afcecdce466fec84b340aa58b1f412b573a6ef`.
The 96 MiB fixture ceiling and production limits/formats are unchanged.

Final rebuilt verification passes all six process gates, nineteen resource
cases, 1,265 editor tests and 74 installed YAML/scene-document/capture/binary-codec
tests on each host. Both refreshed installed resource helpers pass all nineteen
cases, installed plain/Unicode package protocols pass, five macOS signatures
strictly verify, and four Linux dedicated 2 MiB tmpfs ENOSPC cases pass. Full
engine totals are 2,699 on macOS (2,694 pass, five existing skips) and 2,692 on
Linux (2,683 pass, seven skips and the same two GPU grid-label failures).
These are principal-budget and codec/wire results, not whole-stack/RSS proof,
maximum node/decoded-tree/field density or dense/combined-resource publication
acceptance. Complete memory ownership/accounting, those remaining density/stress
gates and the final requirements audit remain open. Windows stays user-owned;
no Windows checks or SampleProject/editor operation was performed in this batch.

Node/field-density acceptance on 2026-11-05 adds four resource cases using a
registered typed 64-field numeric component and bounded repeated/final row
patterns. The 5,229,045-byte source contains exactly 1,048,576 input nodes
(7,542 full-field entities and one 26-field entity). The routed YAML parser
accepts this exact default node cap, and rolls back node/link storage and the
destination on a one-lower cap or one actual extra input node. Parser principal
peaks, including source storage, are 139,970,800 bytes on macOS and 139,602,160 on
native ARM64 Linux. These are parser results, not successful full-scene cooking
of that fixture: canonicalization exceeds the independent 128 MiB decoded-tree
cap. Both public codec paths refuse safely with structured budget diagnostics,
without replacing a prior document/result or writing the sink. The cooker hard
cap is not raised; the limits are conjunctive, not a promise that every maximum
can be combined into an accepted document.

A separate 2,048-entity scene with 64 actual numeric fields per component passes
read/encode at exact principal working peaks, and refuses one byte less. Source
and binary sizes are 1,421,352 / 645,583 bytes; peaks are 135,807,864 / 135,808,472
on macOS and 135,660,408 / 135,661,016 on Linux. Complete wire inspection checks
canonical entity order, schema/version/ranges and all numeric values. Binary
SHA-256 agrees across hosts:
`1b7ec394d184ca46665864fde9b05a126552e84dc0622ab1e7fb22dcaca8f234`.
A 65th field is rejected without prior-state changes or sink writes. Candidate
ECS population is not exercised. The new stress fixture explicitly uses 192 MiB
of native backing; existing 96 MiB fixtures and production policies are unchanged.

All six process gates, all 23 source/installed resource cases and 74 installed
focused YAML/document/capture/binary-codec tests pass per host. Five installed
macOS signatures strictly verify, and source/snapshot files match. This batch
changes tests only; full engine/editor baselines remain those recorded above,
not new full-suite runs. Dense/combined-resource publication, temporary-storage
stress, complete memory ownership/call-chain accounting and the final acceptance
audit remain open. No whole-stack/RSS claim, Windows execution/cross-compilation
or SampleProject/editor operation is implied.

Combined-publication acceptance on 2026-11-05 adds a serialized real-disk stress
gate and installs its script for CI on all three platforms. Bounded generators
produce 65,536 descending-UUID entities, a real 4096-square 32-bit BMP (64 MiB
pixels), a mesh with 27 MiB of vertex/index streams, a 512-square environment
artifact (24 MiB pixels), and their material/metadata edges. The mesh wire oracle
is shared with the adapter-limit fixture instead of duplicated; preparation
retains bounded periods/row buffers, not large asset/scene strings. This is a
selected mixed-resource profile, not simultaneous acceptance of every hard cap
or representative artist-authored rendering coverage.

The public cooker publishes one scene/four assets/five objects with 122,160,938
referenced bytes, then passes fresh recapture/no-op and forced rebuilding. Each
run hashes every physical object and independently binary-decodes the published
scene, including its UUID and all 65,536 entities. Source inventories remain
unchanged, scratch is empty, and no temporary files/writer lock remain. Fresh
attempts under a 100 MiB aggregate capture cap and a 63 MiB staging cap preserve
every old result field and the published manifest/object contents. The staging
cap is below the largest object: existing immutable duplicates release their
temporary charge, so the sum of already-installed object bytes is not a valid
oracle for temporary-space refusal. These are logical-byte-limit checks; the
separate four native Linux 2 MiB tmpfs tests still provide real ENOSPC recovery
evidence, not physical disk-full proof for the mixed profile or other OSes.

Source and installed macOS/native ARM64 Linux runs agree on manifest SHA-256
`7073b566e630c32c4a681406799ae52c6bb36470af7b1fb92ded4ea7c9cfdefe`.
Principal cook peaks are 73,211,328 / 72,731,952 bytes; backing allocator peaks
are 72,384,864 / 71,922,016 and native mapping peaks 72,384,672 / 71,921,824.
The explicit 256 MiB stress budget is test policy, not a convenience preset.
All 6,427,996 native allocations/frees across each complete harness match: the
whole pipeline is not allocation-free.

This test exposed disk captures being reopened for stb's 128-byte image reads.
A two-second sample of the original first cook localized virtually all sampled
time to that path; the obsolete test was stopped after 196 seconds, not counted
as completed acceptance. Shared image validation now uses an inline 16 KiB
engine-array read-ahead buffer, enabled only after dimension/native pixel-storage
checks. Large reads bypass it. It adds no heap allocation and is included in
principal fixed accounting. Two regressions verify at most 72 source calls for
a generated 1 MiB BMP, exactly one pixel allocation, cheap unbuffered header
refusal, and late read-error preservation/complete release. Actual 512 MiB
texture principal peaks are now 536,904,508 / 536,892,220 bytes, including this
fixed buffer; previous historical measurements above predate it.

Rebuilt full-engine results are 2,701 / 2,696 pass / five skips on macOS and
2,694 / 2,685 pass / seven skips / the same two GPU grid-label failures on Linux.
Both hosts pass 1,265 editor tests, all seven process gates, all 23 source and
installed resource cases, the installed stress protocol, 92 installed focused
image/scene tests and installed plain/Unicode package protocols. Five installed
macOS signatures strictly verify; four installed native Linux ENOSPC cases pass.
This closes the selected dense/mixed publication and temporary-byte-limit
profile, not complete helper/parser/decoder/callback/compiler call-chain and
caller/service/VFS ownership accounting, whole-stack/RSS proof, or the final
requirement-by-requirement acceptance audit. Windows execution stays user-owned;
only its CI wiring changed. SampleProject and editor lifecycle are untouched.

ECS canonical-ordering accounting on 2026-11-05 removes the three remaining
`std::sort` calls in scene record decoding and large-map key validation. The
existing cooker heap implementation now lives in `Core::Containers::HeapSort`;
cooker sites import that same implementation, without duplicate algorithms,
function-forwarding layers or an ECS-to-Cook dependency. Its borrowed,
default-initialized principal state is constant-size, allocation-free and
iterative, with O(n log n) worst-case comparisons. Component/entity sorting
reserves its state and charge token before mutation. Large-map validation also
reserves its engine-array facade and arena checkpoint before obtaining scratch;
the key payload still uses the existing charged arena and insertion order is
unchanged. Maps of up to two keys retain the zero-reservation fast path.

`SceneYAML::Storage` reuses its existing route pointer, including for growing
borrowed storage, to expose the actual fixed-state budget. An unrelated nested
YAML scope cannot waive or redirect that owner's charge; native/unrouted storage
can still use the scoped parser budget. No member, heap block or retained
ownership index was added. Four additional regressions cover exact/one-byte-short
large-map budgets, duplicate text and original order, zero-budget tiny maps,
scratch failure draining, native/routed budget selection, and component/entity
sort refusal during both source and binary decoding. Refusal preserves the
prior document and releases candidate allocations; retry succeeds. The existing
exhaustive permutation, move-only, range-guard and comparison-bound tests now
exercise the shared Core algorithm.

Both hosts pass 116 focused scene/sorting tests, 1,265 editor tests, all seven
process gates (including all 23 resource cases), refreshed installed focused /
resource / plain-Unicode protocols, and installed mixed-resource stress. Full
engine results are 2,705 run / 2,700 pass / five existing skips on macOS, and
2,698 run / 2,689 pass / seven skips / the same two GPU grid-label failures on
native ARM64 Linux. The 64-field scene's exact read/encode principal peaks are
135,807,944 / 135,808,552 bytes on macOS and 135,660,488 / 135,661,096 on Linux;
its binary digest is unchanged. Mixed-publication manifest digest and measured
peaks are unchanged from the preceding batch. Four installed Linux 2 MiB tmpfs
ENOSPC cases pass and five installed macOS signatures strictly verify. Formatting
and diff checks pass. These tests do not close complete helper/compiler/callback
memory accounting or the final requirement audit. The next identified duplicate
principal storage is the simultaneous 64-field buffers in document decoding
and graph validation. Windows remains user-owned; no editor or SampleProject
operation was performed.

Principal codec scratch sharing on 2026-11-05 removes the duplicate 64-field
buffers across binary decoding, document decoding and graph validation. One
default-initialized engine `SceneFieldScratch` is borrowed synchronously through
those phases; capture also reuses one buffer across component callbacks and
document validation. Binary encoding preparation borrows the encode buffer
instead of constructing another. No heap allocation, document member, retained
borrow, public signature or wire-format change was added. The internal owned
decode entry reserves the field storage, one active writer and charge token;
the borrowed entry requires that already charged storage. Binary measure/encode
also charge their encoding layout, and both reference-enumeration paths now
charge their field state before callbacks. Graph validation charges its array
facades, checkpoint, temporary node and reference context before arena scratch;
array payloads remain in the existing charged tree arena.

Four new regressions observe identical field-buffer addresses across the full
capture/source/binary decode phases and encoding preparation/emission, count one
field reservation/release, preserve prior documents on field/graph refusal,
check exact/one-byte-short binary layout budgets before output-arena mutation,
and refuse before invoking reference visitors. The existing reusable test route
supplies the bounded instrumentation without another helper implementation.

Standalone Apple Clang arm64 samples using the current common include/define
flags plus `-O0 -g0 -fstack-usage` measure graph validation shrinking from 3,536
to 1,472 bytes and encoding preparation from 2,608 to 544. The new owned decode
entry is 2,240 bytes plus a 2,112-byte borrowed body, versus the old 4,160-byte
entry; including graph validation, these selected simultaneous frames total
5,824 versus 7,696 bytes. Binary decode's selected decode/body/graph frames total
7,744 versus 11,792 bytes. These sums exclude other live helpers/callbacks and
compiler/library frames, so they are not complete call-stack or RSS bounds.
Previously uncharged principal storage is now counted even when these sampled
frames shrink; modeled working peaks need not decrease. Complete memory
ownership/call-chain accounting and the final contract audit remain open.

Both hosts pass the 113 focused scene/cooker tests, 1,265 editor tests, all seven
source process gates (23 resource cases), refreshed installed focused/resource
suites, installed plain/Unicode package protocols and installed mixed-resource
publication. Full engine totals are 2,709 run / 2,704 pass / five existing skips
on macOS and 2,702 run / 2,693 pass / seven skips / the same two GPU grid-label
failures on native ARM64 Linux. The 64-field scene's exact read/encode principal
peaks are now 135,810,168 / 135,810,776 bytes on macOS and 135,662,712 /
135,663,320 on Linux, with unchanged binary digest. Mixed-publication digest,
peaks and all 6,427,996 balanced native allocation/free calls are unchanged.
Five macOS strict signatures and four installed Linux 2 MiB tmpfs ENOSPC cases
pass. Changed sources match the Linux snapshot; formatting/diff checks pass.
No Windows execution/cross-compilation or editor/SampleProject operation occurred.

Bounded scene-tree traversal on 2026-11-05 replaces recursive captured-node
counting and the separate recursive migration uniqueness pass. One charged,
default-initialized engine array holds at most 32 forward-iterator parent
frames. Map keys retain their extra node count; scalar/null leaves need no
parent frame. Collection depth, item count, scalar bytes, embedded NUL and node
limits retain their checks, with explicit counter/initial-depth overflow
protection. Migration output now requests uniqueness in this same traversal;
large-map uniqueness still uses its existing bounded arena sorting scratch.
Each child link is advanced once, rather than repeatedly using linear
`Node[index]` lookup. Traversal itself adds no allocation or retained borrow.

YAML storage cycle checks likewise use a charged 33-link inline cursor stack
instead of recursive helper frames. The depth-zero-through-32 and expanded
node-visit limits remain conservative, including shared-child DAGs. Self-links
fail immediately; scalar, null and empty collection children take a zero-scratch,
zero-reservation fast path. Budget refusal fails closed before installing a
link or replacing an existing field and preserves `LimitExceeded`. The actual
storage owner supplies the budget even inside an unrelated YAML scope.

Eight new regressions cover exact collection/node/depth boundaries, initial
depth and count overflow, scalar/NUL classification, nested duplicate maps,
65,536-child traversal, exact/one-byte-short reservations with no allocation or
field mutation on refusal, actual-owner routing, the leaf fast path, expanded
DAG visit bounds, and a cycle in the last wide-collection child. Existing
migration cases continue to reject invalid, duplicate and over-limit outputs.

Standalone Apple Clang arm64 `-O0 -g0 -fstack-usage` samples with the current
common CMake include/define flags measure captured-node traversal at 2,432 bytes
once, versus the old 416-byte recursive frame, and cycle detection at 368 bytes
once, versus its old 144-byte recursive frame. Other helpers, callbacks and
compiler/library frames remain live; these individual samples are not a
whole-call-stack/RSS bound. Owned tree cloning still has recursive frames and
is the next identified traversal to address. Full helper/caller/service/VFS
accounting and the final contract audit remain open.

Validation for this traversal batch passes all 136 focused source/installed
scene, migration and cooker tests, 1,265 editor unit tests, seven source process
gates (23 resource cases), 23 installed resource cases, installed plain/Unicode
package protocols and installed mixed-resource publication on each host. The
full macOS engine suite reports 2,717 run / 2,712 pass / five existing skips;
native ARM64 Linux reports 2,710 run / 2,701 pass / seven skips and only the same
two GPU grid-label failures. The initial sandboxed macOS run lacked Metal,
filesystem and watcher capabilities; the host-access rerun above passes.
Five installed macOS strict signatures and four installed Linux 2 MiB tmpfs
ENOSPC cases pass. Dense-scene digests and principal peaks are unchanged;
mixed-publication manifest/peaks and all 6,427,996 balanced allocation/free pairs
are unchanged. Changed code matches the Linux snapshot and formatting/diff
checks pass. No Windows execution/cross-compilation or editor/SampleProject
operation was performed. These gates do not close the accounting/audit gaps above.

Owned tree cloning on 2026-11-05 now uses a charged, default-initialized engine
array of at most 32 parent cursors instead of recursive `CloneNode` calls.
Source links are copied before destination growth, so same-owner clones remain
safe when record/link arrays move. Every key and scalar is still owned by the
destination, including repeated source aliases; presentation, ordering,
collection/node/depth limits and duplicate-key rejection remain intact. Each
new child is attached through the existing checked append API while it is still
empty, then filled iteratively. This avoids rescanning completed subtrees
without an unchecked link API or per-node traversal allocation. Destination
tree payload allocations remain necessary and charged; the entire clone is not
claimed to be allocation-free.

Scalar/null/empty-collection clones bypass the parent scratch. Separate small
public/private entry helpers keep those fast paths out of the large traversal
frame in the sampled debug profile; cycle checks now have the same entry/body
separation. Migration application reserves its two optional tree owners,
checkpoints, current/next nodes, counters, pending result and token before the
first snapshot allocation. The reservation survives snapshot reuse, callbacks,
validation and final owned cloning, and drains after both temporary owners.
Its explicit scratch allocation route supplies the budget, or native callers
use the existing scoped YAML budget. No public signature or wire format changes.

Five new regressions cover one exact clone reservation and one-byte-short
refusal before mutation, actual-owner routing, deep alternating map/sequence
clones, leaf/depth/node limits, same-owner 65,536-child record/link growth,
owned scalar copies, and zero-reservation leaf/empty cases. Public compatibility
service tests measure the complete principal peak, reject one byte less,
reserve migration state before any snapshot allocation/callback, drain both
temporary owners, and preserve the previous `SceneDocument` on refusal.

Standalone Apple Clang arm64 samples with current common include/define flags
and `-O0 -g0 -fstack-usage` measure the clone entry at 160 bytes and its single
iterative body at 2,528, versus the old 112-byte entry plus a 656-byte recursive
frame per node depth. Cycle checks have a 112-byte fast entry and 368-byte
iterative body; migration application is 1,664 bytes. These selected frames
exclude other simultaneous helper/callback/compiler/library state and are not
a whole-call-stack/RSS bound. Complete memory ownership/call-chain accounting
and the final requirement audit remain open. The temporary migration diagnostic
formatting buffer identified at this checkpoint is addressed below.

This cloning batch passes 141 focused source/installed scene/cooker tests,
1,265 editor unit tests, seven source process gates (23 resource cases),
23 installed resource cases, installed plain/Unicode package protocols and
installed mixed-resource publication on each host. Full engine totals are
2,722 run / 2,717 pass / five existing skips on macOS and 2,715 run /
2,706 pass / seven skips / only the same two GPU grid-label failures on native
ARM64 Linux. Dense-scene digests/principal peaks and mixed-publication
manifest/peaks/all 6,427,996 balanced allocation/free pairs are unchanged.
Five macOS strict signatures and four installed Linux 2 MiB tmpfs ENOSPC
checks pass. Changed code matches the Linux snapshot; formatting/diff checks
pass. No Windows execution/cross-compilation or editor/SampleProject operation
occurred. These results do not close the remaining accounting/acceptance audit.

Scene diagnostic construction on 2026-11-05 now writes migration errors directly
into retained caller storage instead of formatting into a second 192-byte stack
buffer and copying it afterward. `SceneDiagnostics::Add` and the new `Emplace`
share one retention path, preserving severity/category/error/drop behavior.
`Emplace` accepts an optional bounded initial message, safely moves overlapping
input before clearing its unused bytes, and creates no temporary entry. Migration
formatting moves the retained key before writing its prefix and formats only the
suffix; this preserves a key borrowed from the same reused diagnostic slot and
avoids overlapping `snprintf` input/output. Null diagnostics or full storage do
not format discarded text. Entry layout, message capacity, migration API and
wire bytes are unchanged.

Cook diagnostic key assignment also now snapshots only the schema key before
moving the field key in place, reducing its fixed scratch from two 128-byte
buffers to one. Both inputs are validated before any mutation, including crossed,
partial and shared-source aliases; unused output bytes are still cleared. Five
new tests cover retained-slot reuse, initial-message overlap, truncation,
overflow/category visibility, null diagnostics, aliased registration keys,
full-buffer migration refusal without replacing the previous scene, and crossed
key assignment. All 156 focused tests pass on macOS and native ARM64 Linux.

Standalone Apple Clang arm64 samples with the same common CMake include/define
flags and `-O0 -g0 -fstack-usage` measure migration rejection at 192 bytes versus
336 previously, and cook key assignment at 352 versus 496. The scene `Emplace`
and retention helpers measure 64 and 32 bytes respectively. These individual
frames are not a complete call-chain/RSS bound; the removed message/key scratch
does not imply that all diagnostic/helper/compiler storage is accounted for.
Next inspect manifest dependency-reader scratch, then complete the remaining
helper/callback and caller/service/VFS ownership audit and final #718 acceptance
matrix. Windows execution remains user-owned.

Validation for this diagnostic batch passes all 156 focused source/installed
tests, 1,265 editor unit tests and seven source process gates on each host,
including 23 resource cases and mixed-resource publication stress. Full engine
results are 2,727 run / 2,722 pass / five existing skips on macOS and 2,720 run /
2,711 pass / seven skips / only the same two GPU grid-label failures on native
ARM64 Linux, confirmed from XML. Installed plain/Unicode producer-consumer
protocols, five strict macOS signatures and four Linux 2 MiB tmpfs ENOSPC cases
pass. Changed code matches the Linux snapshot and formatting/diff checks pass.
Installed resource/mixed-stress suites were not rerun in this batch; their last
full installed results remain the cloning batch above. No Windows execution,
editor launch or SampleProject lifecycle operation occurred. These results do
not close the remaining memory-accounting or final acceptance audit.

Typed manifest row accounting on 2026-11-05 now reserves non-empty scene,
asset, root and dependency row state before construction, using the actual
manifest allocator. Empty arrays use small separate entries and do not reserve
or enter their row bodies. Field-name tables are static read-only storage.
Dependencies need only one 128-byte inline key buffer: compare the decoded key
with the previous key before overwriting that buffer, then defer equal-key
identity ordering until the complete record is available. This preserves any
property order, removes one key buffer and the previous-key copy, and retains
no borrowed input text. No row heap allocation is added.

Manifest preparation separately reserves its counts/recipe snapshots, checked
layout/offsets, pending owner and token before constructing them. That charge
outlives second-pass parsing, graph validation and result handoff; temporary
owners drain before reservations release. Row/preparation `OutOfMemory` and
`Unsupported` refusals now propagate as resource errors, like budget refusal,
instead of turning into cache-miss rebuilds that clear the previous manifest.

Five regressions cover every full-ledger fixed reservation with three injected
failure categories, scoped exact/one-byte-short reservation pressure, aggregate
modeled peak/peak-minus-one, long/short valid mesh slots, owned text after input
teardown, escaped scene paths, varying property order and identity-ordered equal
slots. Scoped pressure tests isolate a reservation; they are not whole-run/RSS
proof. The separate aggregate test covers the requested-byte ledger peak.
All 185 focused source/installed tests, 1,265 editor unit tests and the full
macOS engine suite pass (2,732 run / 2,727 pass / five existing skips), as do
installed plain/Unicode package protocols and five strict signatures.

The manifest-row batch's seven macOS source process gates subsequently passed,
including all 23 resource cases and mixed-resource publication. The mixed
manifest remains `7073b566e630c32c4a681406799ae52c6bb36470af7b1fb92ded4ea7c9cfdefe`,
with 73,211,328 modeled principal bytes and 6,427,996 balanced allocation/free
pairs. Dense-field encode digest/peak remain unchanged. These are modeled
ledger/resource checks, not complete memory/RSS proof or current Linux results.

Current standalone Apple Clang arm64 `-O0 -g0 -fstack-usage` samples using common
CMake include/defines measure empty row entries at 48 bytes. The dependency row
body is 480 bytes versus the old combined dependency method's 560; its field
callback is 240 bytes versus 144. Public Read is 5,680 versus 5,504. Checks,
tokens, helpers and compiler frames can increase non-empty call-chain storage;
these selected samples do not establish a total stack/RSS reduction or bound.

Native Linux verification of this batch is pending: final linking reported
`No space left on device`, and subsequent Docker status/three-second health
checks stopped responding. The current sources match its snapshot, but no
current Linux test pass is claimed. Only this run's three stuck CLI clients
were cancelled; Docker and the editor were not restarted. Windows remains
user-owned. Path/digest helper temporaries, complete helper/callback and
caller/service/VFS ownership accounting, and the final #718 audit remain open.

Manifest path-helper hardening on 2026-11-05 adds shared
`VFSRelativePath::ParseInto`, validating the complete spelling before writing
directly into an existing owned path. The value-returning API delegates to it;
the manifest reader no longer constructs/copies a second owned path. An
overlap-safe move precedes clearing the unused tail, so self/suffix views are
valid and invalid input preserves every destination byte. There is no new heap
allocation, unchecked construction or normalization path. Logical-path refusal
still discards only the private row before manifest handoff.

Path/digest helpers reserve their named view/result/token state before use;
digest accounting includes both the returned parse result and the decoder's
local digest. Reservations route through the actual allocator, with resource
failures preserving the previous manifest. Existing full-ledger seam tests
enumerate these additional reservations in both typed passes, including all
three failure categories and exact/one-byte-short pressure. Three new shared
path tests cover owned Unicode text, long-to-short reuse/tail clearing,
self/suffix aliases, malformed/overlong input and unchanged failure output.
All 568 focused source/installed cooker/JSON/path tests pass, along with 1,265
editor unit tests and the full macOS engine suite (2,735 run / 2,730 pass /
five existing skips). Installed plain/Unicode independent-process package
protocols and five strict signatures pass. XML inspection confirms no engine
or editor failures; formatting/diff checks pass and changed sources match the
Linux snapshot. All seven source process gates pass, including 23 resource
cases and mixed-resource publication. Mixed manifest, 73,211,328 modeled
principal bytes, 6,427,996 balanced allocation/free pairs and the dense-field
encode digest/peak are unchanged. Installed resource/mixed-stress tests were
not rerun in this batch. Current Linux verification remains pending: a bounded Docker health check still times
out, and neither daemon nor editor was restarted.
Sampled arm64 `-O0 -g0 -fstack-usage` frames are path helper 192 bytes versus
416 previously, shared ParseInto 128 bytes, and digest helper 224 versus 160
after adding its charge. These are local samples, not complete call-chain/RSS
bounds. Remaining helper/callback/caller/service/VFS accounting and final #718
acceptance are not closed by this change. Windows remains user-owned.

Orchestration path preparation on 2026-11-05 now retains only a default-initialized
strict target and manifest path in charged fixed-run storage. Scalar byte/component
preflight preserves early invalid-path rejection without constructing owned paths
before ledger setup. It matches the existing Append raw-byte checks, including
the root separator and current context-path component limit; it does not change
shared context-path normalization. Preparation reserves the two returned paths,
Append/Parse principal text/component tables and owned values before use. Its
temporary paths/charge drain before provider queries and the actual cook, without
adding a heap allocation, global scratch, retained target-directory result or
second strict target. The strict target uses the shared ParseInto API.

Three parameterized regressions (six memory/disk cases) cover invalid target and
manifest byte/component bounds even under an insufficient budget, valid root/
Unicode/exact-byte/component boundaries, and exact preparation-reservation versus
one-byte-short refusal before provider queries/root acquisition. They verify
unchanged prior results, allocation counts and outstanding backing. This isolates
the preparation seam; it is not a minimum whole-run budget test. All 80 focused
source/installed public-cooker/resource tests pass, as do 1,265 editor unit tests,
the full macOS engine suite (2,741 run / 2,736 pass / five skips), installed
plain/Unicode package protocols and five strict signatures. XML has zero failures;
source/snapshot equality and formatting/diff checks pass. All seven source process
gates pass, including 23 resource cases and mixed-resource publication. Selected
arm64 debug frames are public Cook 28,016 bytes and
PrepareRunPaths 1,072; these are not full call-chain/RSS bounds. Remaining helper/
callback/ownership accounting and the final #718 audit stay open. Windows remains
user-owned. Docker's normal restart timed out; a graceful macOS Quit stopped its
VM, normal termination of remaining Desktop/backend processes completed shutdown,
and Desktop subsequently started with a responding daemon. No VM force-kill was
used. Current Linux verification results follow. No editor/SampleProject
lifecycle action occurred.

After Docker recovery the synchronized native ARM64 Linux snapshot rebuilds all
six test/helper targets successfully. Its full engine suite runs 2,734 tests:
2,725 pass, seven skip, and XML identifies exactly the same two software-Vulkan
GPU grid-label failures; no cooker failure is present. All 1,265 editor tests,
648 installed cooker/JSON/path tests, installed plain/Unicode package protocols
and four installed dedicated-2-MiB-tmpfs ENOSPC recovery cases pass. Fresh installed
macOS/Linux producers also yield byte-identical manifests and all five referenced
objects for both built-in and custom-schema packages. All seven source process
gates, 23 installed resource cases and installed mixed-publication stress pass
on each host. Installed mixed runs reproduce each source run's manifest/peaks,
including 72,732,624 modeled principal bytes on Linux and 73,212,000 on macOS;
each balances all 6,427,996 allocation/free pairs. Dense-field binary digest
`1b7ec394d184ca46665864fde9b05a126552e84dc0622ab1e7fb22dcaca8f234` and
per-host encode peaks remain unchanged. Built-in/custom package manifest digests
are `0db1ca584106b8a31bd9c0a1bc414203bc66a775c0e80b75518f09c8a4c1c7c7` /
`53fb0471850d4b9209860e07aa1fcc9caa46a9958a2715db14dce154ca789d56`.
The macOS source mixed-publication manifest is unchanged, its modeled principal
peak is now 73,212,000 bytes (672 newly accounted retained path bytes), backing/
native-mapping peaks remain 72,384,864/72,384,672, and all 6,427,996 allocation/free
pairs balance. This is accounting evidence, not increased backing allocation or
a whole-stack/RSS bound.

Execution/producer principal-state accounting on 2026-11-05 now reserves
`CookRun::Execute`'s capture context, publication initialization arguments, scene
input, digest, borrowed views and named results before acquiring input roots.
Scene/asset producer callbacks separately reserve their input/output and borrowed
view/result state before accessing captures or invoking codecs/adapters. These
charges are distinct from retained run members and caller-service callback owners;
the existing fixed-run `CookPublicationResult` reservation is not charged again.
Resolved dependencies were already constructed directly in the charged reusable
buffer; there is no additional dependency aggregate to reserve or allocate.

The shared `CookSceneCaptureSet::InputInto` fills caller-reserved storage directly,
preserves all fields on invalid requests and retains the existing value-returning
convenience API. Both production callers now avoid owned result/path temporaries.
Pending public results are also populated field-by-field before publication,
without constructing a second path-owning aggregate. No new heap allocation,
global scratch, retained per-scene buffer or public service contract is introduced.
Two capture-set regressions cover repeated reuse, null/out-of-range/incomplete/
reset rejection, unchanged accounting and captured reads after input exclusion
ends. A parameterized public-cooker regression isolates the exact execution-state
reservation and one-byte-short refusal before input acquisition/allocation on
memory and disk, preserving the prior result and backing allocations.

Remaining ownership/helper acceptance and the final section-10 contract audit
are still open. In particular, setup-query helper scratch and owned query-result
wrappers require a separate review; these named principal reservations are not a
compiler/library call-chain or whole-RSS bound. The offline SampleProject smoke
is optional and its running editor remains untouched. Windows stays user-owned.

Validation of the execution/producer change: all six test/helper targets rebuild
on macOS and native ARM64 Linux. The full macOS engine suite runs 2,745 tests:
2,740 pass and five existing cases skip. Native Linux runs 2,738: 2,729 pass,
seven skip, and XML confirms exactly the same two software-Vulkan grid-label
GPU failures, not cooker failures. All 1,265 editor tests pass on each host;
93 installed public-cooker/resource/capture-set cases and 23 installed resource
cases pass on each host. Fresh installed built-in/custom-schema packages have
byte-identical manifests and all five referenced objects across macOS/Linux;
their manifest digests remain the two values above. Installed plain/Unicode
package protocols and four dedicated-2-MiB-tmpfs Linux ENOSPC/recovery cases pass.

Installed mixed-resource publication, recaptured no-op, forced rebuild and both
logical capture/staging refusals pass on both hosts. Its manifest remains
`7073b566e630c32c4a681406799ae52c6bb36470af7b1fb92ded4ea7c9cfdefe`;
principal peaks are 73,212,872 bytes on macOS and 72,733,496 on Linux (+872 newly
accounted bytes each), with unchanged per-host backing/mapping peaks and all
6,427,996 allocation/free pairs balanced. Apple Clang arm64 `-O0 -g0
-fstack-usage` samples Execute at 1,744 bytes (previously 1,952), ProduceAsset
448 (288), ProduceScene 544 (480), public Cook 28,016 and PrepareRunPaths 1,072
(both unchanged). Accounting tokens add callback-frame storage; these samples
do not prove a total stack or RSS reduction/bound.

All seven Linux source process gates pass. Six macOS source gates pass on the
first run; its mixed stress initially returns a texture-staging `IOError` during
the logical staging-limit probe while host disk headroom is low. Its retained
fixture independently verifies the unchanged expected manifest and all five
referenced objects, empty scratch and no lock/staging leaks. A fresh isolated
stress rerun passes after old successful regenerable fixtures are removed;
source and installed mixed-publication peaks/digests/allocation counts match.
This does not prove the initial native I/O error's cause; no test was weakened or
failure reclassified as a logical-budget refusal. Both source and installed
23-case resource gates pass. Five installed macOS strict signatures,
source/snapshot equality and formatting/diff checks pass. The remaining audit
above is not closed by these results.

Setup-query lifetime accounting on 2026-11-05 replaces the separate retained
provider/caller and registry/callback/VFS query results with one scoped pair of
default-initialized results. The orchestrator reserves those values and the
larger native caller/service projection workspace before querying providers.
The reservation drains before VFS-policy initialization and input acquisition;
only the resulting owner charges survive. Native caller coverage still uses
five inline ranges and constant-space insertion/union, with at most ten shifts.
The memory/disk regression observes the actual live diagnostics/result charge
at query and acquisition boundaries, verifies the exact reservation drains, and
checks that neither backing allocations nor the previous result change.
These are named principal-storage reservations, not arbitrary custom-service,
compiler/library stack or total-process RSS bounds.

The public inline `SceneCookResult` now also returns all five category peaks.
Each is an independent high-water mark; adding them does not yield the global
simultaneous peak. The result remains default-initialized, allocation-free and
non-failing to copy after commit. Failed runs preserve previous category/total
peaks with the previous result; successful no-ops replace them with the new
run's observations. Memory telemetry is never serialized or fingerprinted.
Memory/disk tests verify these properties and unchanged committed manifest
bytes. The process helper's failure-preservation checks include the new fields.

The test-only macOS/Linux smoke helper now separately measures process peak RSS
using `getrusage`, converting Linux KiB to bytes with checked multiplication.
It prints that observation after setup, publication, recaptured no-op, forced
rebuild, refusal probes, independent verification and allocator release. This
includes process/setup/verifier baseline and is not a cooker-budget assertion,
a production API addition, or evidence for a guessed default budget. Mixed
stress gates require positive RSS and all five tracked category fields on
these hosts; Windows remains user-owned and has no new POSIX dependency.

Validation of setup-query accounting and inline category/process telemetry on
2026-11-05: all six targets build on macOS and native ARM64 Linux. Full macOS
engine tests run 2,749 cases: 2,744 pass, five existing skips, no failures.
Linux runs 2,742: 2,733 pass, seven skip, and XML confirms only the two known
software-Vulkan grid-label failures. All 1,265 editor tests and all 119 installed
public-cooker/resource/caller/VFS/allocator-ownership cases pass on each host.
All seven source process gates pass on both hosts. Installed plain/Unicode
protocols pass on both hosts; all four bounded Linux tmpfs ENOSPC/recovery cases
pass. Direct fresh installed built-in/custom-schema manifest bytes and all five
objects per package match across hosts, with recorded sizes/digests verified.
Their manifest digests remain `0db1ca584106b8a31bd9c0a1bc414203bc66a775c0e80b75518f09c8a4c1c7c7`
and `53fb0471850d4b9209860e07aa1fcc9caa46a9958a2715db14dce154ca789d56`.
All five installed macOS strict signatures and nine changed source/test files'
native-Linux snapshot equality verify.

The mixed source package remains `7073b566e630c32c4a681406799ae52c6bb36470af7b1fb92ded4ea7c9cfdefe`.
Tracked principal peaks are 73,213,000 bytes on macOS and 72,733,624 on Linux;
backing/mapping peaks remain 72,384,864/72,384,672 and 71,922,016/71,921,824
respectively, with 6,427,996 balanced allocation/free pairs on each host.
Separate observed process peak RSS is 81,575,936 bytes on macOS and 77,426,688
on Linux. The three runs print every category independently; for example first
publication's catalog/capture/codec/staging/diagnostic peaks are
65,536/181,913/72,519,032/527,776/117,232 bytes on macOS and
20,480/124,553/72,138,104/462,112/117,232 on Linux. These observations select
no default and establish neither a whole-stack nor total-process memory ceiling.

The installed macOS 23-case resource helper passes. Its installed mixed stress
first fails with native `IOError` during the staging-budget refusal probe after
successful publish/no-op/forced runs; the failed fixture is retained. An isolated
retry with the same hashed inputs and a fresh workspace passes all phases and
both logical refusals, with the same tracked/backing/mapping peaks and allocation
counts, and observed RSS 81,641,472 bytes. Independent verification checks the
expected manifest, all five objects, empty scratch and no staging/lock leaks in
the failed fixture and retry; all eleven input hashes are unchanged.

Installed Linux mixed stress exits 1 without a captured diagnostic. Its retained
fixture independently verifies the expected manifest, all five objects, empty
scratch and no staging/lock leaks, and unchanged eleven input hashes; that is
not a passing stress gate. The installed Linux resource helper and subsequent
Docker API queries remain live without completion output. Docker's VM log records
`no space left on device` for its console at 2026-11-05T06:05:01.593733Z, concurrent
with the installed-run failures. This proves an environment disk-exhaustion
event, not the exact errno/cause of every operation. The two installed Linux
checks remain unfinished; Docker was not restarted and no test was weakened.
Completed regenerable source stress fixtures were removed to recover space;
failure fixtures and logs are retained. The memory/contract audit also remains
open. Windows is user-owned and the editor/SampleProject remain untouched.

After user approval, `docker desktop restart` completes successfully and the
daemon responds with no running containers. The old resource/query handles
terminate with Docker connection errors; they were not silently treated as
successful or restarted on an observation timeout. Only the two affected
installed Linux checks are retried. The 23-case resource helper passes in
194.677 seconds. Mixed stress reuses the retained eleven hashed input files and
a fresh empty workspace; publication, recaptured no-op, forced rebuild and both
capture/staging refusals pass. Its expected manifest/all five objects and empty
scratch/no staging/lock leaks are independently verified, and all eleven input
hashes remain unchanged. Tracked/backing/mapping peaks and balanced allocation
counts match the successful source run; separately observed process peak RSS is
77,357,056 bytes. This closes the interrupted installed Linux checks without
reclassifying the original failures or claiming their exact operation-level cause.

The contract's independent schema-pattern gate also passes: `jsonschema` 4.23.0
validates the Draft 2020-12 schema, accepts the design/built-in/custom-schema
goldens, and rejects all fifteen schema-phase negative fixtures. Node 22.20.0
accepts four valid spellings and rejects all eight reviewed LF/CRLF/NUL cases
using the actual ECMAScript patterns alone (without relying on length limits).
The validator and scripts live only in a temporary verification directory, not
in the engine or its dependencies. Formatting checks pass for every cooker
production header/source, and `git diff --check` passes. No editor/project
modification, commit or push accompanies this verification.

Current section-10 audit checkpoint (macOS/native ARM64 Linux; Windows remains
user-owned):

| Requirement area | Inspected implementation/test evidence |
|---|---|
| References/catalog | `CookSceneRoots`, `CookFrozenInputSource`, `CookInputLease`, `CookAssetCatalog` tests cover typed settings/entity/custom references, strict paths, metadata/identity errors, duplicate batches, changed-input sealing and capture lifetime. |
| Adapters/closure | Four adapter suites, custom-schema vectors and dependency-graph tests cover mesh/material/texture/sky closure, malformed data, cycles, shared nodes and limits. |
| Incremental/deterministic | Recipe/fingerprint/reuse tests, installed process protocols and direct fresh cross-host manifest/all-object equality cover leaf/schema/source invalidation, canonical ordering, no-op, forced rebuild and corrupt caches. |
| Publication | Fault-injection, held-open old snapshots, rooted VFS race/lifetime tests, process interruption/recovery and bounded tmpfs ENOSPC cover precommit preservation, postcommit warning-only cleanup and non-failing result handoff. |
| Integration | All seven source gates, installed plain/Unicode protocols, source-free consumers and real application-schema packages pass on both hosts. Runtime/Play rendering is explicitly a separate follow-up. |
| Resources | Allocation-failure/budget-boundary suites, 23 near-limit resource cases, mixed publication/refusals and category/RSS observations pass. The final native bootstrap/helper-query review is closed by the correction and evidence below; measured principal peaks are not compiler/library/custom-service stack or total-RSS bounds. |

Final native query-accounting correction and acceptance record, 2026-11-05:
the scoped setup reservation now covers the larger of native provider, caller,
and service query scratch. Previously it considered only the latter two. The
provider bound composes the allocator-chain helper's three physical/uncovered
profile results, depth results, nested profile values and scalar/cursor state
with the frozen owner/source wrappers' profile/footprint results. All sizes use
the actual engine value/result types. It introduces no allocation, retained
member, registry, recursive walk or arbitrary allocator-chain depth cap. The
existing memory/disk boundary regression now requires the provider bound to
exceed both other projections and checks that the entire reservation drains
before input acquisition without allocating or changing the previous result.

The bootstrap ownership review confirms that the five-view native snapshot
performs allocation-free metadata queries, is retained in the fixed run charge,
and deduplicates only proven complete object/backing ranges. Allocator pools
remain fully charged. Provider bindings keep their own allocation footprint;
shared allocator ancestry is traversed in linear time/constant storage with
cycle rejection before recursive profile queries. Every required service/owner
profile succeeds and is charged before input roots or output/scratch mutation.
Existing alias/coverage/cycle/failure tests and the corrected query-lifetime test
cover these boundaries. This closes the native ownership/principal-query review,
not an unimplemented universal bound on arbitrary custom/compiler/library stacks.

All six targets rebuild on both hosts after this final correction. Full engine
tests again run 2,749 cases on macOS (2,744 pass, five skips, no failures) and
2,742 on native ARM64 Linux (2,733 pass, seven skips, only the same two unrelated
software-Vulkan grid-label failures, confirmed in XML). All five affected short
YAML/document/package process gates pass on each host. The large per-format
resource gates and editor tests are not repeated: neither codec/resource-limit
behavior nor editor paths changed; their completed evidence above remains
applicable. Relocated installations are refreshed; all five macOS strict
signatures, final source/snapshot equality, cooker-wide formatting and diff
checks pass.

One final source-independent installed mixed smoke per host passes publication,
recaptured no-op, forced rebuild and both refusal probes. The manifest remains
`7073b566e630c32c4a681406799ae52c6bb36470af7b1fb92ded4ea7c9cfdefe`;
all five objects are byte-identical across hosts and independently match their
recorded sizes/digests. All eleven input hashes remain unchanged; scratch is
empty with no staging/lock leaks. Both hosts still balance 6,427,996 allocation/
free pairs. Overall tracked/backing/mapping peaks remain unchanged; the corrected
independent diagnostics/result category peak is 117,584 bytes (+352) on each
host. Observed process peak RSS is 81,690,624 bytes on macOS and 77,541,376 on
Linux; neither observation selects a default or establishes a process ceiling.

The requested allocation/type/style review uses inline default-initialized
engine values, `FixedArray`/arena-backed containers and borrowed `cstring`
messages, with secure copying and no `using namespace`, raw `memcpy`, STL owning
containers or standalone `std::atomic` in the cooker. The library exclusion
mutex deliberately protects compound read/mutation state; replacing it with
separate padded atomics would not make acquisition atomic. No extra allocation
or threading mechanism is added merely to use an allocator/atomic type.

The implementation and agent-owned macOS/Linux verification are complete.
Native Windows testing remains explicitly user-owned: the enabled CI/runtime
gates are not waived, and #718's three-host acceptance must not be claimed until
they pass. Runtime/Play #829, shipping CLI/UI, archives/compression/transcoding
remain the already-deferred follow-ups, not hidden implementation work. No editor
or SampleProject changes, new investigation document, commit, push or issue
closure is part of this handoff.

## 11. Production readiness and acceptance gates

The architecture is suitable for a production-quality first cooker: headless
operation, complete typed dependency validation, bounded ownership, complete
cache invalidation, immutable objects, and an explicit manifest commit point.
The acceptance requirements below are not guarantees established by the proposal
alone. Do not describe this proposal, or #718 by itself, as a shipping-ready game
pipeline.

### Concrete contracts requiring review before implementation

The formerly open decisions now have selected definitions in the
[v1 contract](scene-cooker-contracts.md). Review these choices, rather than
reopening them ad hoc during coding:

1. **Dependency keys:** approve the dependency-result key rule in section 6,
   including canonical edge/slot ordering and the changed-texture regression.
2. **Input stability:** approve the offline/staged ownership boundary in section
   3, explicit fallible pre-staging seal, and capture-failure tests. Live editor
   integration remains gated on a concrete exclusion/snapshot protocol; import
   freshness against raw sources
   additionally requires effective import receipts upstream.
3. **Wire and API contracts:** approve the closed manifest v1 schema, canonical
   JSON/material numbers and fingerprints, compatibility rules, and checked-in
   wire-shape golden/negative fixtures. Approve the inline result, selected
   callback/service signatures, diagnostic codes, and scoped VFS publication-root
   acquisition/capability query, strict relative-path type, scratch-root lease,
   read flag, and pinned atomic-write overload.
   The contract distinguishes synthetic design wire fixtures from the real
   binary/package integration goldens now exercised by the process gate.
   Approve non-empty scene batches and rejection of duplicate source paths or
   scene UUIDs before output-object staging, as specified in section 7.
4. **Recovery:** approve section 8's explicit maintenance prerequisites and
   new-root rebuild procedure, with tests proving retries preserve published
   state. No undocumented manual deletion or automatic destructive repair is
   an acceptable operational dependency.
5. **Reader and commit boundaries:** approve the pinned-manifest reader
   contract, replace-compatible read mode on Windows, and pre-commit result
   preparation/non-failing post-commit handoff and input/output lease release.
   Keep scratch pin ownership independent of input sealing through final capture
   reads and cleanup. Test concurrent readers and output/scratch remount/shutdown
   races, not just cooperating writer contention.
6. **Resource and cancellation policy:** approve complete allocator accounting,
   explicit caller-supplied `MaxWorkingBytes` (zero is invalid), fixed hard caps,
   and the initial non-cancellable API. No guessed 768 MiB default is selected.
   Measurement remains necessary before adding a convenience preset.

### Evidence required before production claims

- **#718 core acceptance:** all section 10 tests pass together, including
  independent-process package consumption, deterministic output, injected I/O
  and allocation failures, interrupted-process recovery, concurrent snapshot
  readers, invalid scene-batch rejection, cross-host golden byte/digest equality,
  and real-disk macOS/Windows/Linux coverage. Record tested profiles, fixture
  coverage, peak memory, and results; a list of planned tests is not evidence
  that these guarantees hold.
- **Runtime/shipping acceptance:** the follow-up runtime integration must mount
  the package, resolve UUIDs and staged material texture paths, and load/render
  representative scenes in a fresh process without the editor library, source
  YAML, or original native import files. Include mesh/material/texture and sky
  coverage on supported platforms. This is a separate shipping gate, not an
  expansion of #718's codec/package-reader integration scope.
  Define explicit roots for dynamically loaded project assets before claiming
  support for them, and verify that the runtime distribution supplies every
  engine-owned fallback resource used by the package.
  The future packager must deploy only one pinned manifest's referenced object
  set. Test a removed scene with now-unreferenced unique assets, unrelated
  orphan objects, and temporary/lease files: none may leak into the distribution,
  while shared assets still referenced by another scene remain included. Verify
  the resulting deployment independently of the cook-cache directory.

Archives, compression, parallel cooking, distributed caches, and a build UI
are not prerequisites for a correct production-quality core. The absence of
power-loss durability can be an accepted limitation for rebuildable artifacts,
provided it remains explicit and no stronger guarantee is advertised. Neither
these deferred capabilities nor documentation alone replace correctness proof.

## 12. Review decisions

Recommended choices to approve before implementation:

1. Imported-library inputs, strict complete dependency validation, and no raw
   source reimport/guessed provenance during cook.
2. One batch of scenes -> one loose scene package; SHA-256 immutable objects
   and atomic manifest replacement, not a new archive format.
3. Initial `desktop-vulkan-le64` adapters with existing encoded image formats;
   no claim of texture transcoding, full game packaging, or runtime integration.
4. Include the required shared-reader/reference/VFS correctness work in #718;
   leave the CLI, power-loss durability, parallel cooking, and cache GC separate.
5. Close the section 11 contract gates before the affected implementation;
   require tested core acceptance for #718 and separate runtime acceptance
   before calling the overall pipeline shipping-ready.
6. Review the [v1 contracts](scene-cooker-contracts.md) and schema/wire fixtures
   as the selected design. Implementation remains a subsequent, reviewed step.
