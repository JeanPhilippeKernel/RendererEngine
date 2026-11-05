# Scene Cooker — Proposed v1 Contracts

**Status:** Selected v1 contracts. Implementation acceptance is defined in section 9;
execution evidence and unresolved failures belong in the associated PR and CI.
**Scope:** The [scene cooker proposal](scene-cooker.md), issue #718.

This page resolves the previously open wire/API, ownership, publication, and
budget choices. Its definitions take precedence over illustrative examples in
the proposal. Implementation was authorized after review; this page remains
the target contract, not evidence that every named API or guarantee exists.
The associated code/tests establish the implemented, tested profiles. No scene wire
format, asset migration, CLI, or game packager is introduced by this design.

## 1. Manifest v1

The machine-readable shape is [manifest-v1.schema.json](scene-cooker-contracts/manifest-v1.schema.json).
Every listed property is required, unknown properties are rejected at every
level, and `null` is never an alternative to omission or an empty collection.
JSON Schema checks shape; the semantic and byte-level rules below also apply.

| Record | Exact fields |
|---|---|
| Manifest | `manifest_version`, `recipe_revision`, `target`, `schema_key`, `settings`, `settings_key`, `scenes`, `assets` |
| Scene | `uuid`, `source`, `source_digest`, `input_key`, `object`, `roots` |
| Asset | `kind`, `uuid`, `format`, `adapter_revision`, `input_key`, `object`, `dependencies` |
| Object | `path`, `digest`, `size` |
| Asset root | `kind`, `uuid` |
| Asset dependency | `slot`, `kind`, `uuid` |

- `manifest_version` is 1. `recipe_revision` is a positive u32, initially 1;
  changing codec/migration/canonicalization behavior without another version
  change requires bumping this reviewed engine-wide revision. Custom component
  authors must obey the same version/revision rule. No function-address hashes.
- `target` is exactly `desktop-vulkan-le64`. `settings` is exactly `{}` in v1:
  encoded-image passthrough and authored material behavior are fixed by the
  target/recipe. No unimplemented quality switches are accepted. `settings_key`
  is SHA-256 of the canonical settings JSON, including its final LF. Initially:
  `ca3d163bab055381827226140568f3bef7eaac187cebd76878e0b63e9e442356`.
- A digest/key is 64 lowercase hexadecimal digits representing 32 SHA-256
  bytes. `source_digest` hashes the exact captured scene bytes. `schema_key`
  and input keys use section 3. UUIDs are canonical lowercase hyphenated UUIDs;
  the nil UUID is forbidden in identities and graph edges. Digests and UUIDs
  are exactly 64 and 36 ASCII characters respectively, with no whitespace or
  suffix. Schema patterns use the ECMAScript-compatible `(?![\s\S])` end-of-input
  assertion, not `$` (which can match before a final line terminator) or a
  non-portable `\z`. Object/logical path patterns use the same strict boundary.
  The manifest's final LF is outside its JSON string values, never part of an
  identifier or path.
- `size` is a positive integer, at most 16 GiB and within the applicable object
  budget. All JSON integers use decimal digits without sign, fraction, exponent,
  or leading zeroes. No JSON integer may exceed `2^53 - 1`.
- Scenes are sorted by UUID bytes; assets by UTF-8 kind key then UUID bytes;
  roots by the same asset-identity order; dependencies by UTF-8 slot key then
  asset identity. Root identities and exact dependency triples are unique.
  Different slots may intentionally reference the same child.
- Scene UUIDs and source paths are unique. Asset UUIDs are globally unique
  across asset kinds. Scene and asset UUIDs are separate namespaces. The asset
  table is exactly the reachable closure of the non-empty scene set: no missing
  edges, cycles, or unreachable asset records. Nil fallbacks have no edge.
- An object path is exactly `objects/<digest>.<extension>`; its basename digest
  must equal `object.digest`. Scene objects use `zescenebin`. Asset kind, format,
  extension, and adapter revision must match the following table. Referenced
  objects are regular files whose verified bytes have the recorded size/digest.
  Identical objects may be shared; inconsistent descriptors for one path fail.

| Kind | Format key | Extension | Adapter revision / existing wire version |
|---|---|---|---|
| `mesh` | `zemesh-v1-le64` | `zemesh` | 1 / `ASSET_FILE_VERSION == 65536` |
| `material` | `zematerial-v1-json` | `zematerial` | 1 / `material_version == 1` |
| `environment-map` | `zenvmap-v2-le64` | `zenvmap` | 1 / `Version == 2` |
| `texture` | `image-png` | `png` | 1 / encoded PNG |
| `texture` | `image-jpeg` | `jpg` | 1 / encoded JPEG, including input `.jpeg` |
| `texture` | `image-bmp` | `bmp` | 1 / encoded BMP |
| `texture` | `image-tga` | `tga` | 1 / encoded TGA |
| `texture` | `image-gif` | `gif` | 1 / encoded GIF; first frame only |
| `texture` | `image-psd` | `psd` | 1 / encoded PSD supported by the decoder |
| `texture` | `image-pic` | `pic` | 1 / encoded PIC |

Format identification validates the encoded content, not just its extension.
Unsupported variants fail. This is not animation, HDR-texture, or arbitrary
image-format support; HDR/EXR environment inputs use the environment adapter.
The scene format remains `SceneBinaryFormat::Version == 1` with document schema
2. These format requirements are independently checked by the package reader.

### Logical paths and slots

The input provider maps stable logical root keys to leased input roots. A key
matches `[a-z][a-z0-9_-]{0,31}`. `source` is `<root-key>/<relative-path>`, e.g.
`scenes/main.yaml`; it never encodes the native checkout path or mount handle.
All source paths use `/`, valid UTF-8, at most 255 UTF-8 bytes and 32 components,
with no empty, `.` or `..` component, leading/trailing slash, backslash, colon,
NUL, or control character. Do not truncate to fit the existing 256-byte VFS
buffer. Do not silently lowercase or Unicode-normalize a logical identifier.
The provider preserves the declared logical spelling and rejects aliases or
collisions that make one identity ambiguous on the selected backend.

Use the new shared `Core::VFS::VFSRelativePath` for these logical source paths,
package object paths, and paths inside a leased output/scratch root. It is an
owned inline value with at most 255 UTF-8 bytes plus NUL and 32 components; its
default value is invalid. `Parse(std::string_view raw) -> VFSResult<VFSRelativePath>`
validates the complete length-delimited original bytes against the rules above,
then preserves them exactly. `View() const -> std::string_view` borrows those
bytes and `IsValid() const -> bool` distinguishes a parsed value. There is no
empty-path/root sentinel; operations acquire the root separately. Bounded
parent/sibling construction must preserve the same validation rules, with
top-level siblings constructed directly beneath the already-acquired root.

Reject absolute/drive/UNC paths, backslashes, empty/dot/traversal components,
embedded NUL, controls, invalid UTF-8, excess bytes/components, and trailing
slashes **before** any normalization or native conversion. Do not collapse
separators, resolve `..`, strip a drive prefix, lowercase, Unicode-normalize,
truncate, or decode percent escapes. A literal `%2e%2e` is not a traversal
component and must never become one in a later layer. Only a successful parse
can construct a valid value; there is no implicit conversion from `VFSPath`.

The existing `VFSPath` intentionally normalizes paths and emits a leading `/`;
it cannot enforce this raw-input contract. Keep it unchanged for caller context
paths (`SceneCookRequest::Sources`, `OutputRoot`, `ScratchRoot`, and the returned
context-level `ManifestPath`). Request sources are normalized VFS locations;
the input provider maps each to its declared root key and validates the exact
relative spelling before using it as a manifest `source`. Manifest/adapter path
strings are parsed directly as `VFSRelativePath`, never through `VFSPath::Parse`
first. Leased operations consume the validated relative value directly, without
resolving it again against the global VFS namespace.

Material dependency slots are exactly `albedo`, `emissive`, `normal`, `opacity`,
and `specular`. Mesh slots are `submeshes/<zero-based-decimal-index>/material`
(no leading zeroes); the index must identify the actual validated submesh.
Textures and environment maps have empty dependency arrays. Scene roots are
deduplicated semantic identities; entity/schema/field occurrence provenance
belongs in diagnostics, not a duplicate scene graph in the manifest.

### Canonical JSON and compatibility

Canonical output is UTF-8 without BOM, no whitespace except one final LF,
object keys sorted by unsigned UTF-8 bytes at every level, and arrays ordered
as above. Escape `"` and `\` as `\"` and `\\`; escape U+0000–U+001F as lowercase
`\u00xx`; do not escape `/` or otherwise escape valid non-control Unicode.
Reject invalid Unicode and duplicate keys **before** building a DOM. Manifest
numbers are integers only; booleans/floats are not integer substitutes.

The shared `CanonicalJSONWriter` owns one 64-byte inline formatting buffer,
reused by escapes, integers, binary32, UUIDs and chunked lowercase hex strings.
Schema emitters use these primitives instead of separate UUID/digest buffers;
existing `sizeof(writer)` reservations include the buffer. Sink callbacks consume
each borrowed byte view synchronously, without retaining it. Reentry into the
same writer is rejected before formatting can mutate borrowed bytes, with sticky
`InvalidPath`; the outer call fails even if the sink ignores that nested failure.
A callback may use a separate writer. No new heap allocation or wire change is
introduced; helper/compiler/library call-stack accounting remains separate.

Readers may accept insignificant whitespace/property order, but emitters and
goldens always use canonical bytes. Semantic duplicate/ordering/path rules
still apply to readers. Parse the bounded root `manifest_version` first:

- Missing manifest: first cook. Valid v1 with matching recipe/target/settings:
  verify all relevant keys and objects before reuse.
- Syntactically valid JSON with one unambiguous root version 1 but invalid v1
  shape/semantics: warning and full rebuild; never trust its paths for cleanup.
  Malformed JSON, duplicate keys, or ambiguous version tags instead fail closed
  with `InvalidManifest`; an invalid/absent version is `UnsupportedVersion`.
- Valid v1 with an older recipe: rebuild without reuse. Newer recipe: fail
  `UnsupportedVersion`, preserving the package. A target mismatch fails too.
- Version other than 1, absent/invalid version, or an unreadable manifest:
  fail closed. Unknown/newer formats are not overwritten by guessing v1.

Check target, recipe, and recognized adapter/format versions before treating a
shape error as a rebuildable cache miss. An unknown/newer adapter or format is
`UnsupportedVersion`, not permission to overwrite potentially newer output.
No-op requires exact equality with the newly generated canonical manifest bytes;
an otherwise reusable noncanonical input manifest is canonically republished.

The manifest is a local build ledger, not an authenticated remote cache.
Hash verification detects changed bytes; it does not establish publisher
authenticity. Uncooperating processes modifying the output tree are outside
the publication guarantee; symlink/path escapes remain rejected regardless.

## 2. Canonical material output

The strict shared material reader requires current version, identity, name,
all five texture slots, all six four-element arrays (`ambient_color`,
`albedo_color`, `emissive_color`, `roughness_color`, `specular_color`, `factors`),
`alpha_mode`, and `alpha_cutoff`. Only optional `source_material` provenance
is additionally recognized; unknown fields or duplicate fields fail. Provenance
is bounded/validated as data, never followed, and omitted from output and keys.
If present, `source_material` has exactly `path` (UTF-8, at most 255 bytes),
`importer` (non-empty, at most 63 bytes), and `index` (u32 integer).
An explicitly authored `LEGACY` is allowed alongside `OPAQUE`, `MASK`, `BLEND`;
missing/unknown alpha modes are not inferred. `alpha_cutoff` must agree with
`factors[2]` after checked binary32 conversion and be finite/non-negative.
All other numeric fields must also convert to finite binary32 without overflow.
Material name/provenance and imported text fields are bounded UTF-8 C strings;
embedded NUL is invalid rather than permission to truncate the persisted value.

The shared CPU `Importers/MaterialAssetDocument` reader now implements this
closed schema with owned inline strings/slots/vectors, checked binary32 parsing,
expected-UUID validation, and no heap allocation or native file access.
Its canonical runtime emitter and semantic-projection emitter share the same
streaming implementation; both omit provenance and use the shared canonical
JSON primitives. The material adapter now uses these shared readers/emitters;
full graph/package validation is still required. The legacy permissive
editor/runtime material reader is unchanged.

`MaterialAssetReader` is caller-owned, allocation-free storage for one document,
one JSON parser and one shared 37-byte token buffer. Per-field byte limits remain
enforced when that buffer is reused. `Read` returns a borrowed pointer to its
validated owned document, valid only until the next read or reader destruction;
a failed read exposes no partial document and invalidates previous borrowed
views. The expected UUID is snapshotted before document reuse, so it may borrow
the previous document's identity. Input bytes overlapping the reader are rejected.
The convenience `ReadMaterialAssetDocument` wrapper keeps its strong-output
guarantee using one local reader, but the cooker does not use that stack-owning
wrapper. Its existing single transient allocation contains the complete reader
plus observed encoded bytes, not another full-document stack temporary. Parser
overhead counts in working memory; `MaxDecodedAssetBytes` still caps document
payload. Scoped principal reservations cover inspection's dependency array/local
digest and encoding's resolved-path array, output context and JSON writer.
Refusal preserves prior inspection/sink state and drains owned scratch/charges.
These named-object reservations do not cover every helper/compiler frame.

Imported texture paths may retain the existing importer's leading `/` context
VFS spelling; validate the original tail strictly before any normalization and
map it unambiguously to a declared root in the adapter. Logical relative paths
must also retain exact spelling. A nonnil texture UUID requires a nonempty path
matching that UUID's catalog record; a nil UUID requires an empty path. Neither
spelling authorizes native-file lookup or a path-only fallback. Cooked output
always uses the resolved child object's strict relative package path.
Material validation uses `VFSRelativePath::Validate`, the allocation-free,
validation-only form of the same strict checks used by owning `Parse`. It does
not construct/copy a full path merely to check spelling. Both APIs share one
component validator, including UTF-8, controls, length, traversal, separator and
component-count rules; the C-string overload's scan remains bounded.

Cooked JSON has exactly these required fields and uses section 1's string/key
rules. A nil texture slot emits the nil UUID and empty path; a non-nil slot
emits its UUID and resolved child object path. An input path/UUID disagreement
fails before rewriting. Each four-element array has exactly four numbers.

For deterministic binary32 numbers, emit zero (including negative zero) as `0`.
Emit every nonzero finite value in scientific notation with one digit before
the point, exactly eight digits after it, lowercase `e`, and an exponent with
no `+` or redundant leading zeroes: `1.00000000e0`, `5.00000000e-1`.
Round to nearest, ties to even. These nine significant digits round-trip
binary32; do not delegate formatting to locale-sensitive stream defaults.
Include subnormal, halfway-rounding, large/small exponent, and signed-zero
goldens. Non-finite numbers are never serialized as JSON `null`.

## 3. Exact fingerprint encoding

`H` is SHA-256 over a concatenation of the following typed encodings, with no
native struct layout: `U32`/`U64` are unsigned little-endian integers; `S` is a
U32 UTF-8 byte count followed by those bytes; `D` is 32 digest bytes; `UUID` is
16 RFC UUID bytes in display/network order; arrays start with a U32 count.
Domain strings use `S`, and all arrays use their declared canonical order.

```text
settings_key = H(canonical settings JSON bytes, including LF)

schema_key = H(
  S("ze.cook.schema.v1"), U32(document schema version),
  U32(scene binary version), U32(schema count),
  for each schema sorted by stable key:
    S(key), U32(version), U32(field count),
    for each field sorted by key: S(field key), U32(SceneFieldClass value)
)

metadata_key = H(canonical JSON {"importer": string, "settings": object} + LF)

asset input_key = H(
  S("ze.cook.asset.v1"), S(kind), UUID(id), D(local_content_key),
  D(metadata_key), S(format), U32(adapter revision), U32(recipe revision),
  S(target), D(settings_key), U32(dependency count),
  for each dependency sorted by slot/kind/UUID:
    S(slot), S(child kind), UUID(child id), D(child input_key),
    D(child object digest), U64(child size), S(child object path), S(child format)
)

graph_key = H(
  S("ze.cook.graph.v1"), U32(reachable asset count),
  for each reachable asset sorted by kind/UUID:
    S(kind), UUID(id), D(asset input_key),
    D(object digest), U64(object size), S(object path)
)

scene input_key = H(
  S("ze.cook.scene.v1"), D(source_digest), D(schema_key),
  U32(recipe revision), D(graph_key for this scene's closure),
  S(target), D(settings_key)
)
```

`metadata_key` includes all strictly parsed effective settings as exact UTF-8
key/value strings, sorted by key, plus the recorded importer name. Reject
duplicates, unsupported keys, or values the adapter cannot interpret; do not
truncate the existing 64/128-byte metadata fields. Timestamp, source/artifact
locations, rapidhash hints, and native provenance are not semantic settings.
Adapter identity includes its revision and stable format key as shown above.

`ComputeCookMetadataKey` now streams this canonical metadata projection into
SHA-256 using at most 32 inline byte indexes. A bounded insertion pass orders
the indexes and rejects equal keys together, without mutating caller records,
recursive sort helpers, or a second buffer. Its worst case at the hard setting
cap is 496 comparisons/shifts; this small-table strategy is not used for
unbounded catalog/graph tables. `ComputeCookMaterialContentKey`
streams the shared material semantic projection through the same hash callback.
Neither needs a retained JSON string or DOM. The recipe/schema/asset/graph/scene
key functions described below now connect these primitives to the finalized
graph; manifest emission/reading, verified object reuse and the synchronous
publication session are implemented and connected by the public synchronous
`SceneCooker::Cook` entry point. Complete resource/platform/package acceptance
remains required.

The shared strict sidecar reader requires `uuid`, non-empty `importer`, and
`settings`. Recognized optional fields are `source_path`, `source_hash`,
`import_time_ns`, and `artifact_path`; unknown/duplicate fields fail. Settings
retain the current array of objects containing exactly `key` and `value`, at
most 32 entries with unique keys and no truncation. Mesh/material/texture
adapters currently accept an empty settings array only; non-empty settings need
an explicitly versioned adapter contract, not silently ignored options.
Environment settings require exactly `artifact_version=2`, `pixel_format=rgba32f`,
`color_space=linear_scene`, `orientation=renderer_canonical_v1`,
`mip_policy=generate_on_gpu`, and a positive finite binary32 `exposure` agreeing
with the artifact header. Its logical artifact path and unsigned u64 source
hash are required and validated against captured content. Provenance paths are
bounded strings, never dependency lookup paths; timestamps are signed i64 and
excluded from keys. A `.zenvmap` is resolved through its source sidecar, not
indexed as an independent asset needing a second invented identity.

Persisted environment `artifact_path` may name a declared logical path such as
`envmaps/<uuid>.zenvmap`, or the existing importer's context VFS location such as
`/_cache/envmaps/<uuid>.zenvmap`. For the latter, strictly validate the original
relative tail before constructing a normalized `VFSPath`, then require an
unambiguous declared-root mapping. Neither form authorizes native paths or
following `source_path`. Outside-root, ambiguous, missing, and non-`.zenvmap`
artifact locations fail; artifact content/settings validation remains mandatory.

For meshes/textures, `local_content_key` is the exact captured artifact SHA-256.
For materials it is SHA-256 of the canonical material semantic projection:
current material fields with provenance removed and all texture path strings
empty, retaining the validated UUIDs/slots. This projection is for hashing only,
not a loadable output; resolved child paths participate in the asset key and
actual output. For environment maps it is
`H(S("ze.cook.environment.v1"), D(captured source digest), D(artifact digest))`;
validate the sidecar, artifact header/settings, and source relationship first.

Schema reference-descriptor changes and callback implementation changes not
covered by the key/field versions require a recipe-revision bump. Callbacks
and migrations must be deterministic, pure with respect to captured inputs,
and bounded; they cannot read time, native files, environment variables, live
ECS, or random state. Custom callbacks cannot bypass this contract.

## 4. Public orchestration API and result ownership

The public signatures below are the selected API, not alternate approaches.
Opaque service types are defined by the behavioral interfaces in sections 5–8.
Standard views are call-borrowed; no `using namespace` or static registration.

```cpp
namespace ZEngine::Cook
{
    enum class CookTarget : uint8_t
    {
        Invalid = 0,
        DesktopVulkanLE64 = 1,
    };

    struct Digest256
    {
        std::array<uint8_t, 32> Bytes = {};
    };

    struct SceneCookRequest
    {
        std::span<const Core::VFS::VFSPath> Sources = {};
        Core::VFS::VFSPath OutputRoot = {};
        CookTarget Target = CookTarget::Invalid;
        bool ForceRebuild = false;
    };

    struct SceneCookContext
    {
        ICookInputSource* Inputs = nullptr;
        Core::VFS::IVFSContext* Output = nullptr;
        Core::VFS::IVFSContext* Scratch = nullptr;
        Core::VFS::VFSPath ScratchRoot = {};
        const ECS::SceneComponentSchemaRegistry* Schemas = nullptr;
        const ECS::SceneMigrationRegistry* Migrations = nullptr;
        const CookAdapterRegistry* Adapters = nullptr;
        ICookAllocator* Allocator = nullptr;
        CookLimits Limits = {};
        const ICookCallbackStorage* Callbacks = nullptr;
    };

    struct SceneCookResult
    {
        Core::VFS::VFSPath ManifestPath = {};
        Digest256 ManifestDigest = {};
        uint32_t SceneCount = 0;
        uint32_t AssetCount = 0;
        uint32_t ObjectCount = 0;
        uint64_t ReferencedObjectBytes = 0;
        uint64_t PeakWorkingBytes = 0;
        Core::Containers::FixedArray<uint64_t, static_cast<size_t>(CookMemoryCategory::Count)> PeakWorkingBytesByCategory = {};
        bool Published = false;
    };

    class SceneCooker
    {
    public:
        [[nodiscard]] static Core::VFS::VFSResult<void> Cook(
            const SceneCookRequest& request,
            const SceneCookContext& context,
            SceneCookResult* out_result,
            CookDiagnostics* diagnostics);
    };
}
```

`CookLimits` must be defined before the context, and service forward declarations
before use in the eventual header. The snippet specifies signatures/member
types; it is not a second header implementation. All pointers except migrations
are required. Inputs, schemas, migrations, adapters, allocator, and caller
diagnostics are exclusively stable for the synchronous call; no pending worker
or borrowed result survives return. Concurrent calls need independent contexts
or independently thread-safe providers; one context is not re-entrant.

The caller supplies fixed-value result and diagnostic storage. Diagnostics are
cleared once at call entry; the prior result is unchanged on every failure.
The result contains no heap/arena-backed strings or manifest DOM: paths and
digest are inline values. `Published` is true only if this call replaced the
manifest; a verified no-op succeeds with false. `ObjectCount` and byte totals
count unique referenced paths, not cache orphans or number of writes.

`PeakWorkingBytesByCategory` reports the five independent ledger high-water marks
in `CookMemoryCategory` order. Each is at most `PeakWorkingBytes`; their sum need
not equal the global peak because categories peak at different times. Both peak
fields are execution telemetry only, never manifest data or fingerprint inputs.
They are fixed inline values, copied without allocation after publication, and
the caller's previous telemetry remains unchanged on failure. Process peak RSS
is measured separately by the test-only smoke helper on macOS/Linux; it includes
the process baseline, setup and independent package checks and is not asserted
to fit the cook-owned budget. This adds no production RSS API or budget default.
`ManifestDigest` hashes the exact committed canonical manifest bytes including
LF, even on a no-op. Memory telemetry is not written into that manifest.

Prepare the entire fixed result before commit; afterward assign it without
allocation or failure. All work allocations are released/rewound on return,
including success. Cleanup failure after commit is a warning only; preallocated
diagnostic overflow cannot change the committed outcome. No callbacks execute
after commit. No cancellation/progress API exists in v1.

## 5. Asset adapter interface

`CookAdapterRegistry` is an explicitly built immutable table, unique by kind.
Register each of the four initial adapters once with revision 1; reject duplicate
kinds, null callbacks, unknown kinds, and revision zero before capture. The
registry never uses a runtime `AssetManager`, GPU object, or static initializer.

The following exact callback signatures use these value/view contracts:

| Type | Owned/borrowed contents |
|---|---|
| `CookAssetIdentity` | Inline `CookAssetKind` plus UUID; `Unknown = 0`, then `EnvironmentMap`, `Material`, `Mesh`, `Texture`. Wire keys are section 1's strings, not enum ordinals. |
| `ICookCapturedFile` | Immutable `Size() -> uint64_t`, `Digest() -> Digest256`, `Read(offset, span<uint8_t>) -> VFSResult<size_t>`. Short reads handled; premature EOF fails. No original native path/reopen API. |
| `CookAssetInput` | Identity, borrowed captured artifact, optional captured environment source, caller-owned strictly parsed metadata exposed as const, limits, and logical source for diagnostics. All borrows outlive the callback. |
| `CookDependency` | Child identity, non-empty slot view, and optional original `SourcePath` view. Materials require that original texture path; mesh UUID-only references leave it empty. Discovery checks a present path against the typed catalog before sealing. |
| `CookAssetInspection` | Stable format key, local-content/metadata digests, and direct dependencies/text deep-copied into one exact-size block from the supplied reusable scratch allocator. Leaf results allocate nothing. Inline move-only ownership releases that block on `Reset`/destruction; no `Ref` or control block. |
| `CookResolvedDependency` | Slot, identity, child input key, format, and verified `VFSRelativePath` output path/digest/size. No runtime handle. |
| `ICookByteSink` | `Write(span<const uint8_t>) -> VFSResult<void>`; handles complete writes or fails. Tracks size/digest and enforces output budget; no rename/commit authority. |

```cpp
struct CookAdapterFns
{
    CookAssetKind Kind = CookAssetKind::Unknown;
    uint32_t Revision = 0;
    const void* Context = nullptr;

    Core::VFS::VFSResult<void> (*Inspect)(
        const void*, const CookAssetInput&, ICookAllocator&,
        CookAssetInspection*, CookDiagnostics*) = nullptr;

    Core::VFS::VFSResult<void> (*Encode)(
        const void*, const CookAssetInput&,
        Core::Containers::ArrayView<const CookResolvedDependency>, CookTarget,
        ICookByteSink&, ICookAllocator&, CookDiagnostics*) = nullptr;
};
```

Inspection calls the shared strict reader and validates identity/version before
enumerating dependencies. The orchestrator copies its bounded edge/digest
results into graph storage and resets the inspection before releasing/reusing
per-asset scratch. Parsed material documents and encoded input buffers do not
remain in the inspection result. Encoding receives
exactly those resolved slots in canonical order; an omitted, duplicated, wrong
kind/UUID, or extra slot fails. It may reread/revalidate captured bytes with the
same shared reader, never reopen the original mutable file. A material must
use the supplied child paths. Mesh/image/environment passthrough still requires
complete validation, not an unchecked copy.

Callbacks return structured failures and do not throw across the interface.
Every allocation, including decoder hooks, uses the supplied allocator or a
charged bounded shared-codec arena. Adapter scratch/results never outlive their
documented scopes. Returning success without consuming/validating the complete
required payload is a contract violation. Only orchestration publishes files.

### Document-wide typed reference API

Extend existing `ECS::SceneReference` additively with `cstring FieldKey = nullptr`
and `cstring AssetKindKey = nullptr`; keep its current `Kind` and `UUID` fields
first so existing aggregate initializers remain source-compatible. Entity
references leave `AssetKindKey` null. Cook rejects an asset occurrence without a
stable field key or recognized asset-kind key; permissive legacy editor callers
are not forced to become cook consumers.

Add `ECS::SceneReferenceOccurrence` containing the reference, inline entity UUID
(nil for settings), and borrowed `cstring SchemaKey = nullptr`. Add
`SceneDocument::EnumerateReferences(const SceneComponentSchemaRegistry&,
SceneReferenceOccurrenceVisitorFn, void*, SceneDiagnostics*) const -> bool`;
the visitor is `bool (*)(void*, const SceneReferenceOccurrence&)`. It includes
all supported component references plus scene settings without exposing YAML.
Keep `EnumerateComponentReferences` unchanged for current consumers. False from
a visitor terminates/fails enumeration; no provenance view survives the call.

Built-in mesh/material occurrences use their existing schema keys and
`mesh_uuid`/`material_uuid` field keys with `mesh`/`material` kind keys. The sky
uses schema `SceneSettings`, field `sky.environment`, kind `environment-map`,
including retained non-nil inactive values. Entity references stay under scene
graph validation. Nested/custom reference field keys are stable schema-owned
paths, at most 127 bytes, not offsets or process-local component type IDs.
Field/kind descriptor changes require schema/recipe invalidation as section 3
specifies. Count occurrences before deduplication to enforce the resource cap.

## 6. Input lease and capture contract

`ICookInputSource::Acquire(const CookCaptureContext&, CookInputLease*,
CookDiagnostics*)` returns `VFSResult<void>`. `CookCaptureContext` contains
`VFSScratchRootLease* Scratch = nullptr`, `VFSRelativePath ScratchRun = {}`,
`ICookAllocator* Allocator = nullptr`, and `const CookLimits* Limits = nullptr`
(VFS types are fully qualified in the header). All are required and borrowed
for the call/run. The orchestrator passes its checked per-call allocator and
limits; the provider cannot substitute a larger private capture budget.
`IVFSWorkingMemoryBudget* WorkingMemory = nullptr` additionally borrows the
run's VFS accounting policy. The public cooker must provide it; low-level
non-cook fixtures may omit it. Before capture/catalog storage is created, the
frozen provider retains one exact allocator-routed array of input-root memory
reservations. Borrowed read-root state and shared storage/mapping capacities
then participate in the same run budget as output/scratch. Reset releases those
reservations after input authority is no longer used, even on preparation
failure; captures and their scratch-domain reservation can survive that reset.
The public orchestrator uses two-phase acquisition. First acquire output and
scratch root leases, then call `ICookInputSource::AcquireRoots(CookInputLease*,
CookDiagnostics*)`. This binds a `Rooted` lease holding real input exclusion and
physical root authority, without capture allocation, catalog discovery, or
scratch writes. While exclusion remains held, call
`CookInputLease::RequireDisjoint(const VFSRootLease&, CookDiagnostics*)` for both
output and scratch, and check `scratch.Overlaps(output)` is false. Unsupported
checks fail closed. Only after all three domain checks succeed may the
orchestrator exclusively create its private scratch run directory and call
`CookInputLease::PrepareCapture(const CookCaptureContext&, CookDiagnostics*)`.
`ScratchRun` is that directory's validated path relative to the pinned scratch
root. The provider borrows this lease/run instead of reopening a live scratch
context. The convenience `Acquire` shares the same root-acquisition/preparation
implementation but is not a substitute for the public run's domain preflight.
`CookInputLease` is opaque, move-only, RAII, and
non-forgeable by a boolean flag. On success it provides stable root mappings,
bounded read-only catalog enumeration,
`MapContextPath(const Core::VFS::VFSPath&, Core::VFS::VFSRelativePath*,
CookDiagnostics*) -> VFSResult<void>`,
`FindAsset(const CookAssetIdentity&, CookAssetCatalogEntry*, CookDiagnostics*)
-> VFSResult<void>`, and
`Capture(const Core::VFS::VFSRelativePath&, CookCapturedFile*, CookDiagnostics*)
-> VFSResult<void>`; its path includes the declared logical input root key.
`CookCapturedFile` owns an immutable memory/disk capture and implements
`ICookCapturedFile`; it releases its owned storage on destruction. Acquisition
or capture failure leaves its output unchanged. Catalog records are copied,
not borrowed from the live registry. Exclusion is held through the explicit
fallible `CookInputLease::Seal(CookDiagnostics*) -> VFSResult<void>` boundary,
not merely until the last capture read or until eventual lease destruction.

`CookCaptureStore` reserves principal temporary SHA-256, pending capture-owner,
EOF-probe and charge-token state before source metadata queries, reads, capture
allocation or scratch-file creation. Seal-time `VerifySource` separately
reserves its principal SHA-256/result/EOF-probe/token state before source I/O.
The common hash loop borrows the caller's charged nonmovable SHA-256 state;
there is no second hash object or new allocation. Actual captured bytes and the
already retained stream remain separately accounted. Exact/one-byte-short and
unsupported/native-refusal tests preserve the old output, avoid source/scratch
I/O on fixed-state refusal and drain reservations on every return. These are
principal-state gates, not complete scalar/helper/compiler-frame bounds.

Mapping accepts already-normalized caller context locations, checks component
boundaries against declared roots, preserves relative spelling, and rejects
ambiguous mappings across contexts rather than choosing the first root.
Lookup copies one typed catalog record through its sorted index; consumers do
not retain a duplicate catalog just to resolve dependencies. Both operations
require an active lease and preserve their outputs on failure, including invalid
provider-success values. Entry validation is shared with catalog construction.

The first disk provider serves an **offline frozen library** controlled by one
`CookFrozenLibraryOwner`, not an arbitrary editor VFS. The owner serializes its
read leases against mutations and mount/root changes: `Acquire` fails busy
while mutation is active; mutation/release/remount fails busy while a lease is
active. Multiple read leases may coexist. Frozen input roots must be physically
disjoint from output/scratch, with stable mappings and no followed symlinks or
reparse points. Enumeration is bounded before retaining entries; do not call an
unbounded directory-list API and check its count only after allocation.
The frozen file-set builder rejects multiple logical names for one physical
identity, including hardlinks across roots, by sorting its existing discovery
records before publishing the path index. Equal content in distinct files is
not an alias. Discovery and seal do not retain a second identity index.
Scratch captures use exclusively created owned files beneath `ScratchRun`;
scratch/output roots are also physically disjoint. The scratch-root lease
verifies checked read/write/close, exclusive file/directory creation, and stable
no-follow rooted resolution before creating the run directory or capturing any
bytes. A one-time capability check on an unpinned `IVFSContext*` is insufficient.

Each disk capture completes its checked write/flush/close before being exposed
as immutable; subsequent reads use retained read-only handles or reopen only
through the same pinned scratch root and owned run path. No writable capture
handle is exposed to adapters. Releasing/sealing the input lease neither
releases the scratch pin nor removes its captures: `CookCapturedFile` storage
remains readable independently of source exclusion. The orchestrator keeps the
scratch lease alive until all captures and their file wrappers are destroyed
and all owned run-file/directory cleanup attempts finish, on success, no-op,
or failure. Never delete a run directory just because the input lease ended.
The provider may access only its owned run files; cleanup must not use live
mount resolution or recursively remove an unverified/pre-existing directory.

The caller gives the owner exclusive control of that offline directory for its
lifetime and completes any imported-library staging **under quiescence** before
acquisition. The provider does not turn a concurrently copied directory into a
transactional snapshot. Native writers bypassing the owner are outside the
cooperating ownership contract; a read-only mount alone is not proof of
immutability. This trust boundary is explicit, not a claim of OS sandboxing.
Raw live `IVFSContext*`, watcher pauses, timestamps, and caller `is_stable` flags
cannot stand in for the input provider/lease. A live-editor provider remains
unsupported until import writers participate in its exclusion protocol.

Capture streams source bytes into owned storage while hashing, with checked
size and full reads, then records the digest and rewinds only that capture for
validation. Scenes are parsed from their capture. Once all scenes, catalog
metadata/sidecars, reachable artifacts, and required environment sources have
been captured and discovery is complete, the orchestrator calls `Seal` while
exclusion is still held. `Seal` verifies captured source digests/identities and
the catalog/root identity set used for discovery. A changed artifact, sidecar,
or catalog identity fails `InputChanged`; recheck I/O failures retain their
underlying error. Never retry just the changed file into this generation.
Rechecking detects violations; the owner lease, not sequential per-file hashes,
establishes cross-file consistency.

Two-phase acquisition starts `Rooted`; only disjointness checks, preparation,
move, and reset are allowed in that state. Successful preparation transitions
to `Active`; preparation failure transitions to `Failed`, retains exclusion
until reset, and cannot be retried. Convenience `Acquire` returns an `Active`
lease on success and preserves its output on failure. Successful `Seal`
transitions an active lease to `Sealed`; failure
transitions it to `Failed`, aborts the run, and cannot be retried on that lease.
No capture/enumeration is allowed after either transition; a repeated `Seal` or
an operation on an inactive lease fails `InvalidRequest`. Destruction releases
exclusion without validation/callbacks and cannot fail the cook. After successful
sealing, exclusion may be released immediately or retained until cleanup; all
further work uses only owned immutable captures/catalog records. Changes to the
original library after sealing do not alter this run's captured generation.

Successful sealing and validation of every scene identity are prerequisites to
the first package-object staging/install, manifest publication, or successful
no-op return. A destructor or post-commit lease release must never perform the
first source recheck or introduce a new input failure. A memory provider freezes
owned buffers under the same lifecycle contract and supports mutation-injection
tests before sealing and after exclusion release.

`ICookInputSource::FixedWorkingBytes(const ICookAllocator& covered) const noexcept`
returns `VFSResult<uint64_t>` describing provider/pre-owned state not already covered by
the run's retained allocator chain. It is charged once separately from routed
allocations. The frozen provider includes its inline state, retained frozen
library owner and the configured root bindings' complete backing footprint:
headers, alignment and page/size-class overhead, not just their payload bytes.
Bindings inside an already charged pool add no child-block charge. Separate
allocator state/idle pools are charged fully; shared ancestors are subtracted
once. The query allocates nothing and does not acquire input exclusion or mutate
roots/storage. Unknown contracts fail `Unsupported`; underreported block bounds,
cyclic chains and inconsistent aggregate profiles fail closed. Arithmetic and
budget failures cannot overwrite the caller's result or publish a package.
The public run uses `UncoveredFixedWorkingBytes(covered, coverage)` with the
same native-service proof as its working allocator. The default delegates to
`FixedWorkingBytes`, keeping opaque/custom providers' complete charge. The native
frozen source and owner exclude their fixed state only when each complete object
range is covered by a charged service family or declared complete VFS facade.
Their allocator branch applies that proof to both chains and their nearest
common ancestor; ordinary profiles remain the complete physical footprint.
Configured bindings still retain their full non-pool block footprint, even when
the source, owner and allocator objects are all covered. Partial fixed-object
ranges never qualify. No coverage pointer or additional ownership index is retained.
`ReleaseStorage() noexcept -> VFSResult<void>` is cleanup-only and idempotent.
Call it after resetting every input lease and destroying/resetting every capture,
but before destroying the per-call allocator or scratch pin. It frees retained
provider buffers even when the caller-owned provider outlives the cook call.
Live leases/captures reject release with `AlreadyExists`. Cleanup errors remain
observable; release does not validate inputs, retry failed file cleanup, or mutate
the package. Releasing storage does not make a single-run provider reusable.

## 7. Shared VFS publication capabilities

Add `IVFSContext::AcquirePublicationRoot(const VFSPath& destination,
VFSPublicationRootLease* out_lease) -> VFSResult<void>`. The output is opaque,
move-only, RAII, initially empty, and non-forgeable. Failure leaves it unchanged.
Acquisition resolves and pins the backend instance, mount mapping, and physical
root anchor as one synchronized operation, then checks the capabilities below
on that pinned root. Acquire before creating the output writer lock or reading
the old manifest; keep the lease through all output cleanup and writer-lock
removal. All output paths, including lock and temporary paths, use this lease.

`VFSPublicationRootLease::QueryPublicationCapabilities() const` returns the
captured `VFSPublicationCapabilities` value. Required guarantees are exclusive
file/directory creation, atomic replacing/no-replace rename, replace-compatible
reads, checked native close results, and no-follow/stable rooted resolution.
If any are unavailable, acquisition fails `Unsupported` before output changes;
the default context/backend implementation also returns `Unsupported`. A
context-only query, `Write` bit, backend name, or numeric identity is not a pin.

The selected shared value types/signature are:

```cpp
namespace ZEngine::Core::VFS
{
    enum class VFSPublicationGuarantees : uint32_t
    {
        None = 0,
        ExclusiveFileCreate = BIT(0),
        ExclusiveDirectoryCreate = BIT(1),
        AtomicReplace = BIT(2),
        AtomicNoReplace = BIT(3),
        ReplaceCompatibleReads = BIT(4),
        CheckedClose = BIT(5),
        StableRootedPaths = BIT(6),
    };

    struct VFSPublicationCapabilities
    {
        uint64_t BackendIdentity = 0;
        VFSPublicationGuarantees Guarantees = VFSPublicationGuarantees::None;
    };

    // New IVFSContext virtual method; default returns Unsupported.
    // VFSResult<void> AcquirePublicationRoot(
    //     const VFSPath& destination, VFSPublicationRootLease* out_lease);
    // The opaque lease supplies:
    // VFSPublicationCapabilities QueryPublicationCapabilities() const;
}
```

`BackendIdentity` is nonzero and stable only while that backend/mount pin is
held; it is diagnostic, never serialized or hashed, and cannot recreate a lease.
The lease provides `Open`, `Stat`, `Exists`, `CreateDir`, `CreateDirExclusive`,
`Remove`, `Rename`, `RenameNoReplace`, and `Close` with the corresponding
`IVFSContext` signatures, except every input path parameter is
`const VFSRelativePath&`, not `const VFSPath&`. Invalid/default relative values
fail `InvalidPath`. Paths resolve directly through the pinned backend/root,
never through the live mount table. The strict parser rejects raw absolute and
traversal inputs; native rooted resolution separately rejects symlink/reparse
escapes and operations crossing the acquired root/backend. The lease exposes
no mount/shutdown operations. Both rename operands and every temporary sibling
stay on that same root/backend.

While a lease is held, unmount, replacement/overlay remount affecting its root,
and backend destruction/shutdown are rejected as busy or deferred until pins
drain; they must not invalidate or reroute existing operations. Acquisition races
with those mutations are synchronized. Disk implementations retain the native
root handle or an equivalent enforced stable anchor, not only its pathname.
Memory implementations retain the same tree and synchronization domain.
`StableRootedPaths` includes that anchor and no-follow resolution, not just
lexical `..` removal. A backend unable to enforce it is unsupported.

Close all lease-owned file wrappers and finish owned-file/lock cleanup attempts
before destroying the lease. Its destructor releases the pin without allocation,
callbacks, validation, or a new failure outcome. Disk/memory implementations
advertise these guarantees only after their race/lifetime tests exist. The root
pin establishes backend lifetime/routing, not writer exclusion: the exclusive
writer directory lock remains separately required.

### Anchored target-directory acquisition

Each final root lease exposes `AcquireChildRoot(const VFSRelativePath&,
SameLeaseType*) -> VFSResult<void>`. It acquires an existing descendant directory
directly from the retained physical anchor, never through a context, mount
lookup, or provider reactivation. It does not create directories. Invalid/null
outputs fail `InvalidPath`; occupied outputs fail `AlreadyExists`; every failure
preserves the output. Read-only and scratch authority cannot be promoted to
publication authority through this API. Unknown backends default to `Unsupported`.

The child owns an independent entry in the existing bounded mount-pin table;
parent release does not permit overlay/remount/unmount of the surviving child.
Native children retain no-follow directory handles on the same device and share
the retained provider lifetime. Owned-memory children retain/pin the same storage
domain and namespace anchor. Creation/acquisition of child pins remains available
through existing parent authority after context shutdown/provider retirement;
it does not enable new context-level acquisitions. Current implementations make
one root-state allocation (with inline file slots), not an allocation per file or
a second pin table. Their public run accounting uses the policy below.

Budgeted root acquisition accepts an optional trailing
`IVFSWorkingMemoryBudget*` on all three context root-acquisition methods. A
`VFSWorkingMemoryReservation` is prepared before allocating root state and moves
with its lease; child roots inherit the same policy. Reset destroys root/file
state and releases its pin before releasing this reservation. Acquisition
failure releases its provisional reservation and leaves the output unchanged.
Unbudgeted callers preserve the existing VFS behavior; budgeted callers reject
providers with unknown bounds instead of falling back to uncharged storage.

For the public cooker, pin the requested `OutputRoot`, complete physical-domain
preflight, then create the target directory through that pin and acquire its
child publication root. All lock/object/manifest operations use the target pin.
Do not use live `CreateDir` followed by context-level acquisition as a substitute.

### Scratch-root ownership

Add `IVFSContext::AcquireScratchRoot(const VFSPath& destination,
VFSScratchRootLease* out_lease) -> VFSResult<void>`. The output is initially
empty, opaque, move-only, RAII, and unchanged on acquisition failure. This
lease uses the **same backend/mount/root pin mechanism** as publication leases,
including synchronized acquisition, root anchoring, no-follow resolution,
and rejection/deferral of remount/unmount/backend shutdown until pins drain.
Share that infrastructure; do not build a second lifetime/locking protocol.

Scratch acquisition requires read/write, checked flush/close, exclusive file
and directory creation, and stable rooted resolution. It does not require
atomic publication renames or replace-compatible manifest reads. Unsupported
pinning/capabilities fail `Unsupported` before any run directory/file is created;
the default context/backend implementation returns `Unsupported` too.

`VFSScratchRootLease` supplies `Open`, `Stat`, `Exists`, `CreateDir`,
`CreateDirExclusive`, `Remove`, and `Close` with the publication lease's
strict `VFSRelativePath` inputs and same-root routing. No global context,
mount/shutdown, or publication/commit authority is exposed to the provider.
Its pin is owned by the orchestrator, not by `CookInputLease`; borrowed capture
operations must not outlive it. Close/release all captured file wrappers and
finish owned cleanup attempts before releasing this pin, even when source
exclusion was released much earlier. Destructor pin release is non-allocating,
non-validating, and cannot change a committed result. Cleanup errors use the
existing bounded warning policy; they do not trigger cleanup in a remounted
tree or recursive deletion outside the proven-owned run directory.

Add `VFSOpenFlags::AllowReplace` for read-only opens, implemented with
`FILE_SHARE_READ | FILE_SHARE_DELETE` on Windows and the existing stable-open-file
semantics on POSIX. Using it with write/create/truncate/append is invalid. Other
opens retain their current sharing policy. Unsupported backends fail rather
than silently ignoring the flag. Manifest readers use this mode.

Extend the shared atomic-file helper with `AtomicWriteMode::Replace` and
`AtomicWriteMode::NoReplace`; default remains replace for current callers.
Add an overload accepting `VFSPublicationRootLease&` instead of `IVFSContext&`;
its path parameter is `const VFSRelativePath&`. The cooker uses only this
pinned-root overload. Existing context-based overloads retain `VFSPath` inputs.
Share the write/rename algorithm with existing callers rather than duplicate it.
Both modes use exclusive sibling temp creation, complete write, checked flush
and close, then the selected atomic rename. Immutable objects use no-replace; an
existing destination is returned as `AlreadyExists` for orchestrator digest/
size verification, never overwritten. Manifest replacement is the only commit.
Capability failure is detected before output publication; no cross-backend or
copy/delete rename fallback is permitted.

Disk close reports the native error and invalidates its handle once; do not
retry POSIX `close` after an error against a possibly reused descriptor.
Backend teardown may release the file wrapper but must not mask the checked
close outcome. Memory-backend operations must offer equivalent atomic visibility
and synchronization for the concurrency tests, not merely claim the flags.
No directory-sync or power-loss durability promise is added.

## 8. Resource policy and diagnostics

`CookLimits` has these exact fields/defaults. All counters use checked u64
arithmetic internally; configurable limits may be lowered but not exceed the
v1 hard caps below, except working memory which has no guessed default.

| Field | Default / v1 hard cap |
|---|---|
| `MaxWorkingBytes` | **0: invalid until the caller supplies a positive budget**, at most host `SIZE_MAX` |
| `MaxScenes` | 256 |
| `MaxCatalogEntries` | 65,536 |
| `MaxAssets` | 8,192 reachable identities |
| `MaxEdges` | 65,536 distinct dependency triples |
| `MaxReferenceOccurrences` | 65,536 before deduplication, across the request |
| `MaxManifestBytes` | 8 MiB |
| `MaxSidecarBytes` | 64 KiB |
| `MaxMaterialBytes` | 1 MiB |
| `MaxEncodedAssetBytes`, `MaxDecodedAssetBytes` | 512 MiB each |
| `MaxCapturedBytes` | 8 GiB, including sidecars and environment sources |
| `MaxStagedBytes` | 16 GiB, including temporary/installed objects written this run and manifest |
| `MaxImageDimension` | 16,384 per axis; decoded-byte bound also applies |
| `SceneLimits` | Existing default `SceneDocumentLimits`; may only be lowered |

Global memory/byte budgets and per-format limits apply together. A copy or
regeneration is charged when its storage exists; renaming a temp to its object
does not double-charge that same storage. Retained older objects are not new
staged bytes; the writer still handles disk exhaustion safely. The stream
buffer is 64 KiB, bounded by remaining available memory. Manifest JSON depth
is at most 8, sidecar/material JSON depth at most 16; strings at most 4,096
UTF-8 bytes except logical paths (255), slot keys (127), and existing metadata
key/value maxima (63/127). Duplicate fields fail before allocation growth.

There is **no automatic 768 MiB allocation/default**. A caller may explicitly
choose that ceiling for experimentation; telemetry and representative fixtures
must justify any later convenience preset. CI fixtures use explicit budgets,
including a deliberately insufficient one. Budgets affect acceptance and
execution only, never fingerprints or successful output bytes.

`ICookAllocator` exposes checked `Allocate(bytes, alignment, category)` returning
`VFSResult<void*>`, non-failing `Free(pointer)`, and inline live/peak statistics.
Its allocation-free `TryCharge(bytes, category, CookMemoryCharge*)` also charges
fixed working storage outside returned blocks, such as bounded parser state on
the call stack. The default is `Unsupported`, not zero cost. Native page/budget
allocators use their ledger; native scratch views delegate to their parent,
because unrelated stack storage is not contained in an idle scratch pool. The
charge is acquired before constructing/using the fixed object and released only
after that object drains. Charges and returned blocks must drain before the
allocator. A fully charged pool remains reusable for its own child allocations;
it does not discount external fixed state.
Shared CPU code uses the lean optional
`Core::Memory::IWorkingMemoryBudget` hook on `IAllocationRoute`. Ordinary
editor/default routes may return null, but the final native
`CookCodecAllocationRoute` always returns its actual checked budget. Its
allocation-free `WorkingMemoryCharge` tokens acquire before using fixed state
and drain before the route. The native bridge coalesces nested reservations in
one existing ledger charge, checks overflow/growth, permits non-LIFO releases,
and still drains earlier reservations after its first sticky refusal. No heap
block, registry, retained callback or pool-containment guess is introduced.
It is wrapped by a per-call ledger enforcing `MaxWorkingBytes`; callbacks cannot
silently allocate from a different heap. Categories are catalog/graph, captures,
codecs, staging/manifest, and diagnostics/result. Charge actual retained writable
storage, alignment/capacity overhead, codec/parser heap allocations, and temporary
simultaneous old/new buffers. A rewind releases charges only for storage actually
relinquished; retained reusable arena capacity stays charged. Do not charge the
same child-arena storage twice. Uncommitted virtual address
reservation is reported separately, not treated as live working bytes.

SHA-256 reservations use `Core::Hash::SHA256::FixedWorkingBytes()`, including
the stream object and the pinned implementation's simultaneous `ah[8]`/`w[16]`
compression buffers (96 bytes), not just `sizeof(SHA256)`. The composed
`CookFingerprint` query adds that transient bound once to its containing state.
Capture, source receipts, adapter passthrough, semantic/recipe keys, scene
reading/encoding, manifest writing, publication and object verification all
reserve it before hash construction/use. No new allocation or retained hash
member is added. This principal-buffer query is tied to the pinned sha-2 source
revision and must be audited when that dependency changes; scalar/helper/compiler
frames are still separate, not silently covered by the 96-byte buffer bound.
`SHA256::Reset` reinitializes the live upstream state without redundant aggregate
clears: input/padding overwrites compression storage before use, and finalization
overwrites every digest byte. Reuse is checked after finalized and unfinished
streams across compression/padding boundaries. Reset discards a stream; it is
not a secure-erasure API, and object members remain default-initialized.

Before accepting a backing allocator, the ledger queries its allocation-free
`MemoryRequirements()` and `AllocationFootprint(bytes, alignment, category)`.
Unknown contracts fail `Unsupported` during setup, before acquiring input roots
or changing output/scratch. The per-block bound includes upstream headers,
alignment/size-class slack, unsplittable tails and native writable-page rounding;
the ledger checks it before calling the backing allocator. Bounds smaller than
the requested upstream capacity are invalid, not permission to undercharge.

`CookAllocatorMemoryRequirements` separates the complete upstream chain's
`StateBytes` from retained `PoolBytes`. Both are charged before run storage is
used, including idle pool capacity. `IncludesAllocations` means returned blocks
are contained in those already charged pools; do not charge their occupied bytes
again. Nested budget/scratch wrappers compose fixed state and retain the outer
pool once, not a second charge for each nested slab. Fixed allocator state uses
diagnostics/result accounting, and retained allocator pools use codecs accounting.
Allocator chains and pool capacities remain stable through consumer teardown.
Existing caller allocations are not inferred from aggregate allocator statistics:
their owners still require explicit accounting. Frozen root bindings now report
their full retained cost relative to the run's covered allocator chain. The actual
stable ownership edge is exposed by `ParentAllocator()` (null for a leaf).
`CookUncoveredAllocatorBytes` walks two single-parent chains in linear time with
constant storage, finds the nearest common ancestor, and subtracts only its
retained fixed/pool totals. A cycle check precedes recursive profile queries;
there is no second shared-domain registry, allocation or arbitrary depth cap.
Its optional native-service coverage proof is applied consistently to both
branches and the shared ancestor. Physical and uncovered profiles must both
contain that ancestor; coverage cannot mask an inconsistent physical aggregate.
Changed pool/containment or growing fixed-state claims fail closed.
No allocator address/relationship enters a cook fingerprint or package byte.

Fixed allocator objects can also reside in already charged native service
storage. `ICookMemoryCoverage::Contains(object, bytes)` is a borrowed, pure proof
of complete range containment, not another domain registry or a heap-size guess.
`UncoveredMemoryRequirements(coverage)` preserves the ordinary profile's exact
pool bytes and allocation-containment flag, discounting only proven fixed state.
Native page, scratch and budget allocators compose this query through their
actual parent chain in linear work, without copying objects or allocating an
index. Opaque/custom implementations keep their full charge unless they
explicitly supply their own complete coverage-aware contract.

`CookBudgetAllocator::Initialize` checks chain cycles before recursive profiles,
rejects growing-state/changed-pool/changed-containment claims and checks the
uncovered charge before accepting the allocator. Its ordinary
`MemoryRequirements()` still reports the full physical state, independently of
the ledger's discounted charge; nested callers must not inherit somebody else's
credit as if that memory did not exist. Pool capacity, idle storage and per-block
footprints do not receive a native-service discount. The public cooker uses the
existing five-slot native-service calculator as the coverage proof, includes that
inline query state in its fixed charge, and requires every registry, callback and
VFS profile to succeed and be charged before input acquisition. A failed or
unaffordable owner profile cannot turn a coverage declaration into free memory.
The coverage pointer is used only during synchronous setup queries, never retained.

`QueryCookRegistryMemory` accounts the schema registry, four-kind adapter registry
and optional initialized migration registry before any input-root acquisition or
output/scratch work. It includes their inline objects, native root/sub-arena owner
state, complete writable committed native pages (including idle/reusable pages),
and page-rounded arena commit-tracker storage. `ArenaAllocator::RetainedStorage`
provides a pure allocation-free snapshot of a native arena family's backing; a
root and all its sub-arena views share one tracker identity and complete totals,
including discontiguous pages committed through children. Clear/reuse and child
teardown do not release root-owned pages, so they do not reduce this charge.
The common native-service calculator has five fixed view/snapshot slots: schema,
optional migration, optional declared callback arena, output and scratch caller
facade arenas. A public cook initializes this snapshot once for allocator
coverage and reuses it for registry, VFS and caller-value requirements; these
projections do not re-query facade storage or create another snapshot. Standalone
public queries still own a temporary snapshot. Callback owners retain their
independent complete `MemoryRequirements` query, which may itself inspect native
storage; this is not a universal once-per-cook facade-query guarantee.
Backing families are deduplicated in constant space without a new allocation or
domain registry.
Registry objects, root owners, sub-arena views and the native callback provider
add inline state only when their complete object ranges are outside every charged
native family or complete declared VFS facade object. This includes cross-family containment, such as one root owner
stored inside the other family's pages or registry objects stored in a separate
callback arena. Each external root/view is counted once. Snapshots carry the
actual live root `Owner` pointer; moving a root updates that pointer for all live
child/sibling views while retaining the same backing `Identity`. Total reserved
address bytes are reported separately and
are not added to live working bytes. Queries never change cursors or commit pages.
Borrowed/routed backing without an external-owner footprint fails `Unsupported`
during setup rather than treating entry payloads as complete retained storage.
Uninitialized optional migration registries are invalid requests before input work.
Registry fixed state uses diagnostics/result accounting; writable backing uses
codecs accounting. Budget refusal preserves caller results and published output.

Native registry arenas and their complete families must remain exclusively
stable through the synchronous call; a query does not grant permission to grow
or rewind them during a cook.

Callback-owned context/service graphs have a separate explicit owner:
`SceneCookContext::Callbacks`, a borrowed `const ICookCallbackStorage*`. Before
acquiring input roots or running any codec/reference/migration/adapter callback,
`QueryCookCallbackMemory` visits all registered codec/reference contexts, all
migration-entry contexts and all four adapter contexts. Every nonnull root must
be recognized by the owner's pure, allocation-free `Contains(const void*)` query.
Missing/unrecognized ownership fails `Unsupported` during setup. All-null
built-ins require no owner and add no storage charge or allocation.

After coverage succeeds, the owner's pure `MemoryRequirements(context, covered)`
query runs once, not once per callback. It returns the common
`CookServiceMemoryRequirements` value (`StateBytes`, `StorageBytes`,
`ReservedAddressBytes`); the first two are charged before input acquisition and
address reservation is separate telemetry. The default footprint is unsupported.
The supplied `covered` allocator is the actual run ledger. The reported footprint
must include the complete owner/context/service graph and idle backing/headers,
excluding only storage demonstrably charged by that allocator and the native
registry backing. One owner describes shared contexts once; it must not return
only `sizeof` a pointer-bearing root, sum shared graphs repeatedly, or infer
pre-owned block sizes from aggregate allocator statistics. A custom composed
owner may reuse `CookUncoveredAllocatorBytes` for allocator-chain state/pool
deduplication, with explicit full block costs for uncovered non-pool allocations.
Unresolved aliases with other services require `Unsupported`, not guessed costs.
These footprint hooks are implementer contracts, not proof that arbitrary user
callbacks obey them; no callback may reach an undeclared heap/service, mutate
pre-owned graph storage, or retain/grow it during the synchronous call.

`CookArenaCallbackStorage` is the concrete zero-allocation implementation for a
complete context graph in one native arena family (root/sub/sibling views).
`ArenaAllocator::RetainsStorage(pointer, bytes)` recognizes only ranges whose
every page is already writable in that family. It does not establish C++ object
lifetime or recursively inspect stored pointers. The caller must actually place
all owned writable context data in that family; external graphs require a custom
complete owner instead. Native accounting includes the family's complete retained
pages, tracker, root/view owner state and this provider's inline state. Existing
schema/migration family charges and an arena-resident provider are not charged
again. Unknown borrowed/routed backing, foreign roots and uncommitted addresses
are not accepted. The coverage walk uses linear time and constant storage; there
is no context copy, per-callback allocation or second hash/domain registry.
`ICookCallbackStorage::StorageArena()` optionally exposes the native family its
complete footprint includes. This is a stable ownership declaration, not merely
a borrowed address range. Registry containment may depend on that declared
family, so registry, callback and VFS service queries must all succeed and their footprints
be charged together before any work. `SceneCooker::Cook` enforces that ordering;
it never uses a partial registry-only charge after callback refusal. Custom
owners without native backing return null from this optional query.
Native allocator fixed objects may share this native family through the
coverage-aware state query above. Except for that fixed-state containment and
the specified registry/VFS-facade family sharing, native callback backing must
be independent of allocator pools/per-block backing and VFS-operational domains;
callers with other aliases need a custom complete owner rather than using the
native provider unchanged.

The native run's callback projection reuses the same initialized five-service
snapshot as registry, VFS facade and caller projections. It still validates all
codec/reference/migration/adapter roots before reporting a footprint. Only the
exact final `CookArenaCallbackStorage` implementation uses this direct native
projection: a custom `StorageArena()` declaration is not proof that the arena
is its complete graph. Custom owners still receive their complete virtual
`MemoryRequirements` query exactly once after coverage validation, including the
covered allocator, and their failures propagate unchanged. Standalone queries
retain the independent native snapshot when needed. The run adds no stored
member, allocation, budget preset or ownership protocol, and avoids a second
snapshot and repeat facade-native-storage queries on the native callback path.

Broader caller-service/VFS/allocator alias acceptance and fixed helper/callback/
parser/decoder/hash stack accounting remain separate acceptance work.

`CookPageAllocator` supplies a concrete native backing with an explicit positive
ceiling: one page-rounded writable mapping per block, inline owner/charge/header,
and no CRT heap or allocation map. Footprint queries are pure; refusal occurs
before `mmap`/`VirtualAlloc`, and charges remain live through unmapping/free.
Use explicit reusable scratch pools above it for high-frequency small allocations,
not an eager unmeasured default reservation. TLSF scratch reports an occupied-block
bound including an unsplittable tail/header; its whole backing and idle capacity
are reported separately as retained pool storage. The stream-sizing hint recognizes
precharged reusable pools instead of mistaking a full ledger for pool exhaustion;
actual checked allocation remains authoritative, including fragmentation/failure.

Shared readers using internally owned arenas or third-party heaps must expose
budget hooks or a conservatively charged bounded workspace before cooker use.
`SceneYAML::Storage::InitializeBorrowed` now accepts precharged caller storage
without OS reservations/commit or secondary allocation.
`SceneDocument::Parse` and `BinarySceneCodec::Decode` accept the shared checked
route. The document/header and initial tree share one block, including name
text. Routed trees start with at most 32 KiB (or their smaller decoded cap),
then grow geometrically through stable checked arena blocks. `MaxDecodedBytes`
is a cap on retained payload capacity, not an eager writable allocation.
Block headers, alignment, skipped tails and spare capacity remain charged by
the working allocator. No pointer-moving backing resize, callback replay or
unaccounted heap/OS fallback is used. Checkpoint rewind reuses retained blocks;
their charge remains live until the owning document/scratch tree is destroyed.
Avoid assert-on-OOM string/container helpers in fallible paths. An allocation
refused by the budget is `SizeLimitExceeded`;
host allocation failure within it is `OutOfMemory`. Both fail before commit,
release run-owned storage, and preserve prior result/package. Fixed diagnostic
and pending-result storage count in the per-call ledger too; do not grow them
to report an allocation failure. RSS is separate observational telemetry.

### Bounded VFS storage accounting

`VFSRootMemoryRequirements` separates exclusive `StateBytes` from two retained
shared domains: backend `Storage` and pinned mount `Mapping`. Domains carry
process-local identity pointers and immutable byte capacities, never serialized
paths or cook keys. Backend profiles must leave `Mapping` empty; the mount pin
supplies it. Unknown profiles fail `Unsupported`. Borrowed input roots use
`ReserveWorkingMemory` without changing their lifetime/allocator or attaching
per-call state to a shared library owner. Reserve each borrowed root once per run.

`CookVFSWorkingMemory` implements the shared VFS policy against the existing
`CookBudgetAllocator`. It sums exclusive root state, reference-counts each shared
domain once across input/output/scratch/children, rejects conflicting capacities,
and checks addition/charge growth before accepting a reservation. The object and
its inline engine hash table are themselves charged; up to eight distinct shared
domains need no index allocation. Larger sets grow geometrically using two
checked workspaces, releasing the old backing after rehash. Tombstone compaction
uses that same bounded path. Both simultaneous buffers count during growth;
refusal preserves live reservations. There is no per-file accounting allocation.
VFS charges use the existing staging/manifest category. The synchronous policy
uses no atomics and rejects teardown while a reservation remains live.

Disk profiles cover one root state with inline file slots and one shared retained
provider. Owned-memory profiles cover one root state plus the shared storage
object, fixed committed arena/pools, and page-rounded allocation-tracker storage.
The independently retained root/provider/storage/mapping objects now use
`NativePageObject<T>` fixed-size native mappings, not system-heap `new` whose
overhead is unknown. Each final type has one header-free, page-rounded writable
mapping and a matching native deallocator, including virtual deletion through
adjusted base pointers. Its complete writable footprint, not just `sizeof(T)`,
is reported in the exclusive root state or shared provider/mapping domain.
`NativePages` supplies pure checked rounding and native mapping/release shared
with `CookPageAllocator`; there is no second allocation map, CRT control block,
per-object header or new per-file allocation. Unsupported size/alignment/page
contracts fail before mapping. Owned-memory storage counts its native object
mapping plus its separately owned arena/tracker; it does not charge child pool
allocations again. These standard operational owners have independent native
backing, not aliased caller registry arenas. This does not establish a complete
bound for arbitrary custom backends, call-stack state or other external
services, which retain their separate accounting/acceptance requirements.
Snapshots, writes, file/node wrappers and rename scratch stay in that already
bounded pool, so do not charge them again per open. Retained mapping tables are
also charged once even across parent/child/borrowed roots. These capacities are
conservatively charged while used by this call even if configured before it;
this is accounting, not another allocation or a process-RSS estimate. Independent
contexts sharing one backend charge their different mapping tables separately,
not the backend pool twice. Shared domains remain charged until the last run
reservation releases them. The policy must outlive every such reservation.

Real memory/native tests cover shared domains, independent mappings, child/root
failure rollback, parent/context/provider teardown, every index-growth refusal,
capacity conflicts, overflow and accounting reset/reuse. The real captured
scene/material publish and independently captured no-op fixture applies this
policy to input/output/scratch through one checked budget, and drains all VFS
reservations before releasing that budget. The public run also charges its fixed
owners, caller diagnostics/results, input provider, frozen library owner and
configured root-binding storage. Per-block backing overhead and retained allocator
chain/pool accounting now have explicit contracts and native-backed regressions.
`QueryCookCallerMemory` additionally charges the caller's request/context values,
the complete borrowed source-path span, and result/diagnostic values before input
root acquisition. It uses five inline ranges, checked address/size arithmetic and
an allocation-free range union; source paths borrowed from request/context fields
do not count twice. Filtering compacts and insertion-orders the five address/size
pairs in place, with at most ten shifts and no general-purpose recursive sort or
redundant endpoint fields. Complete value ranges inside charged native service families
or complete declared VFS facade objects add no separate charge. This query and all
three registry/callback/VFS profiles must succeed and be charged together. It does not infer
extra heap capacity/metadata outside these value ranges or ownership of context
service pointers; broader external-backing/custom-service aliases remain open.

`IVFSContext::NativeStorage()` is a pure, allocation-free native caller-facade
declaration: complete fixed object address/extent and one complete native arena
family containing its retained facade state. It is not a root, pin or new budget
protocol. Unknown implementations return `Unsupported`; storage ranges must cover
the actual interface subobject without overflow. The standard `VFSContext` owns
its import-bookkeeping arrays in that native family, even when initialized before
the cook. It refuses active import publications and attached watcher/cache/scanner/
registry/coordinator/listener service graphs. Its inherited implementation also
refuses unknown derived classes; a custom context must explicitly describe its
complete storage, not inherit a guessed `sizeof(base)` profile.

`QueryCookVFSServiceMemory` charges the output/scratch caller-facade objects and
their complete native writable backing, tracker and root/view owner state before
input-root acquisition. Equal context pointers are queried once per profile;
overlapping complete facade object ranges count once. Its shared five-slot
calculator deduplicates all native registry/migration/callback/facade arena
families, including inline services/owners/caller values resident in a complete
facade object. Reserved virtual address bytes remain telemetry, not a working
charge. Operational root/provider/pool/mapping backing stays in the existing VFS
policy and must be independent of these native caller-service families. Unknown,
borrowed, malformed or unaffordable facade storage fails closed before root/file
acquisition or output mutation. All caller/native service storage is exclusively
stable through the synchronous call; no query authorizes concurrent editor work.

Complete fixed helper/callback/parser/decoder/hash call-stack and remaining
caller-owned service storage accounting remain unfinished; these gates are not a
full memory-bound proof.

The manifest reader now has a concrete partial fixed-state gate. Syntax probing,
measurement and typed construction reuse one bounded parser decode buffer, not
three 4097-byte arrays. Syntax checking borrows the complete buffer without
retaining it; short/null ranges fail before index allocation. Measurement keeps
owned recipe/count values and restarts the same parser before construction.
`JSONReader::Reset` resets input, limits, depth, root/error and cursor state in
place, rather than constructing another parser. Inactive inline stack entries
are overwritten before use; decoded strings remain owned by caller buffers.
The principal typed/syntax parser objects, temporary validation graph and their
inline charge tokens receive scoped `sizeof` charges in staging/manifest
accounting, with no extra allocation. Unsupported/failed charge contracts are
hard resource failures preserving the prior manifest, never cache-miss/rebuild
permission. This covers these named objects, not compiler spill/call-frame bytes,
recursive helpers or the remaining parser/decoder/hash/callback stack paths.

Syntax preflight's semantic container traversal is now iterative, with a fixed
16-entry engine array inside the charged syntax owner, not depth-dependent
recursive frames. The second key-index pass resets that same parser and depth
cursor in place; inactive semantic frames are overwritten before reuse. Root
and asset compatibility roles, decoded-key uniqueness (including unknown nested
objects), the depth-16 limit and hard-failure/rebuild boundary are unchanged.
One key-index allocation remains; no new allocation is added. Depth-boundary,
deep duplicate/malformed-input, role-scope and exact/one-byte-short principal
budget tests verify cleanup and prior-ledger preservation. Scalar/library/sort/
compiler-frame accounting is still separate work.

`WriteCookManifest` likewise requires the actual checked allocator. After input/
descriptor/workspace validation it reserves one principal stream, JSON writer,
SHA-256, pending output and charge token in `StagingManifest`, before either
measurement or sink emission. It reuses the same stream/writer for both passes;
measurement constructs no unused hash. The emission hash is created only after
successful measurement and the reservation drains after that state. No allocation
is added. Unsupported/OOM/limit refusals and measurement/sink errors preserve the
caller output. Publication routes both exact comparison and staging through this
allocator, reports unsupported emission as `UnsupportedBackend`, cleans staging,
and refuses a later commit after failure. A canonical no-op holds nested result,
comparison and writer reservations but no manifest staging owner. Exact/one-byte-
short tests and every sink-write failure check reservation lifetime/drain, output
preservation and no new allocations. This is principal-object accounting, not a
bound for validation/fingerprint/JSON/hash helpers or compiler-generated frames.

Shared YAML/image readers now have their own partial fixed-state gates through
that working-budget hook. `ReadSceneYAML` charges actual `ParserLease`,
`EventLease`, `BoundedTree` and charge-token sizes before the first
`yaml_parser_initialize`; refusal preserves the output and tree cursor without
allocating any libyaml block. The existing creating-thread YAML scope supplies
the budget and restores its parent on unwind. Parser/event/tree state drains
before the reservation; ordinary unrouted editor calls are unchanged.

`ValidateEncodedImage` charges its principal common source/prefix/context/result/
scope/token objects before the first source read. PNG additionally charges
`stbi__png` before header inspection and `stbi__zbuf` before decoding. GIF
charges `stbi__gif` before decoding, after header inspection has freed its
separately routed heap object of that type; the sequential large header/decode
states are not double-counted. These use named object sizes, not guessed frame
reserves, and add no decoder allocations. All reservations drain on resource,
read, validation and decode failures. Smaller structured/local/recursive
decoder helpers, libyaml helpers, hash internals and compiler spill/call-frame
bytes still require their own accounting proof.

### Shared codecs and parsers

The single `STB_IMAGE_IMPLEMENTATION` has been extracted from
`RenderResourceManager.cpp` to the shared CPU module `Importers/ImageCodec.cpp`.
Format recognition allocates no decoder state. JPEG SOI recognition includes
permitted 0xff marker fill without invoking stb's allocating type probe; the
shared full header/payload validation still follows. In particular, TGA and
unsupported bytes do not speculatively allocate a JPEG decoder before their
own format/limit checks.
Its scoped allocation route takes precedence over the existing worker-slab/
system-heap route, and `CookCodecAllocationRoute` bridges it to the checked cook
allocator. Adapters must scope probes/decode/free together and check both the
decoder result and the route's sticky error. Do not add a second conflicting
implementation, depend on renderer initialization, or fall back to uncharged
`malloc` after a cook allocation refusal. Existing rendering callers retain
their allocation route outside the cook scope. The encoded-texture integration
below uses this route; complete package acceptance remains work.

Shared libyaml now uses the same `Core::Memory::IAllocationRoute` interface via
the thread-local, nestable, allocation-free `YAMLAllocationScope`. The build
wraps pinned libyaml's API translation unit without changing fetched sources,
covering allocate/reallocate/free and its separate `strdup` path. Aligned
inline headers retain the original allocator and capacity; header overhead and
simultaneous realloc blocks pass through the checked cook backend too. Refusal
never falls back to the system heap. Non-cook callers retain the default heap
route. Tests cover every allocation failure in shared parsing, scanner growth,
document string duplication, and emission; nested/thread-local routing and
borrowed tree rollback/reuse. A separate helper links only yaml (no engine or
renderer) and checks default/routed parsing.

The immutable document now exposes borrowed `SceneDocumentSettingsView` values;
its NUL-terminated name refers to tree-owned text rather than a separate heap
string. The existing owning `SceneDocumentSettings` remains a convenience input
for editor capture, which copies into its tree. Moves preserve the original
checked allocator/deleter. Compatibility migrations use two inline optional
tree owners with routed backing; checkpoint rewind restores records, arena
cursors, error and freeze state before reusing a no-longer-referenced snapshot.
Small migration chains use two initial backing allocations. Larger snapshots
grow those same two owners to their observed peak size; a rewind reuses every
retained block without step-count-proportional backing allocations.
Tests cover the full 32-step chain, failure at every parse/document/component
migration allocation, output preservation, allocator-correct moves/replacement,
name lifetime after source/schema teardown, one-allocation small binary decode,
multi-block source/binary round trips and allocation-failure preservation,
default decoded caps under small working budgets, and identical binary bytes
under different sufficient decoded-memory limits.
The scene bridge translates structured scene-limit/allocator diagnostics. The
run-owned `CookSceneCaptureSet` maps and validates the complete request before scene
capture, keeps one exact owner/path-index allocation, parses one scene at a time,
and copies roots through the existing graph implementation. It preserves request
index order and rejects duplicate normalized logical paths and scene UUIDs.
Capture failure exposes no complete set; Reset releases partial captures and
retains cleanup errors. Real frozen-provider fixtures cover lifetime after seal,
source replacement and context teardown, invalid/count/path/UUID boundaries and
every scene-set allocation refusal. These owners now feed the public synchronous
run. Complete fixed helper/callback/parser/codec-stack accounting remains work.

`CookAdapter` now provides the allocation-free four-kind registry, shared wire
format/object-descriptor table, compact scratch-backed inspection values, and
discovery-only UUID/path-pair resolution. `CookMaterialAdapter` validates strict
metadata/current material payloads, computes semantic keys, enumerates all five
texture slots, and streams resolved canonical output. Its tests exercise one
reusable backing allocation across repeated assets, transactional allocation
failures, short/failed capture reads, all slots, sink failures, and encoding
after input seal/original context teardown.

`CookMeshAdapter` now shares `Importers/MeshAssetReader` with native/VFS engine
readers through `AssetByteSource`. `MeasureMeshAssetReadWorkspace` uses the same
bounded wire traversal to compute exact arena high-water capacity, including
actual engine-container alignment, names/maps, and temporary validation state.
It allocates nothing, preserves its output on failure, and validates layout
only: the adapter must still fully decode the same immutable capture before
consuming/staging it. `MeshAssetReadFixedWorkingBytes()` reports the actual
buffered reader (including its 16 KiB inline buffer), pending mesh/hierarchy and
arena checkpoint/validation helpers. The adapter reserves that bound and its
named bridge/limits/charge state before either measurement or decoding; the
reservation spans allocation and drains after those sequential calls. Refusal
precedes capture reads/native mapping, preserves prior inspection and emits
nothing. Unsupported fixed-state contracts report `UnsupportedBackend`, not a
mesh-version error. This allocation-free reservation is separate from measured
arena capacity; it is not complete compiler/helper/scalar stack accounting.
The full reader checks geometry/hierarchy,
UTF-8 names, maps, redundant submesh byte offsets/sizes, and exact consumption.
File wrappers check close before publishing outputs and preserve arena/results
on failure. Unknown mesh versions return `Unsupported`.

Mesh inspection copies only nonnil material edges into compact result storage;
slots are emitted directly in unsigned lexical order, not numeric submesh
order. An iterative decimal-prefix walk visits each original index exactly
once (including nil-material parents with nonnil descendants), using constant
scalar storage. It removes the comparison sort and its recursive library
frames without a new allocation/table or changing the original slot indices.
Inspection traversal is O(submeshes), not O(edges log edges). Encoding
validates the exact canonical resolved slot set in O(submeshes + dependencies),
destroys decoded containers, then reuses that same arena as its streaming
buffer. No retained geometry, encoded-file copy, second stream allocation, live
input lease, or original native reopen is required. Shared `CookAdapterSupport`
centralizes metadata/diagnostics and verifies captured SHA-256 while streaming.
Its shared passthrough helper requires the checked allocator and reserves actual
`SHA256`, result-digest and charge-token sizes before the first read or sink
write. Unsupported/insufficient fixed-state contracts fail closed, with no
allocation or uncharged fallback; the reservation also drains on late
read/write/digest failures. The stream's existing charged storage is not charged
again. This covers principal passthrough state, not all hash/helper frames.

`CookTextureAdapter` now uses shared `ImageCodec::ValidateEncodedImage` over
borrowed `AssetByteSource` callbacks. The shared stb translation unit selects
one declared LDR format from its fixed prefix, runs that format's shared header
reader, preflights dimensions/native RGBA storage, fully decodes, and releases
pixels inside the required allocation scope. Dispatch goes directly to the
selected shared decoder; speculative format probes do not poison EOF tracking
for short valid files. No second image parser/implementation, retained encoded
copy, native reopen, renderer initialization, or global option mutation is
introduced. GIF validates the runtime's first frame only. Content validation
follows the supported CPU decoder, not an additional strict container/CRC parser.

All provider short reads are completed; zero/overreported reads, provider
errors, and out-of-range skips fail. The cook route's sticky allocation error
also fails the operation, even if a decoder fallback appeared successful.
PNG16 preflight includes its native 8-byte RGBA pixels; temporary decoder
allocations use the same aggregate working budget. A leaf inspection allocates
no retained result storage. Encode rejects extra dependencies/unknown targets,
finishes validation before any sink write, releases pixels, then hashes/copies
unchanged encoded bytes through at most 64 KiB of charged stream storage.
Late digest/read/sink failures can leave partial staging bytes only; future
orchestration must discard those without committing a manifest. Tests cover all
seven formats, channel counts, PNG16, bounded repeated reuse, allocation/read/
write faults, resource caps, truncation, and large streaming input.

`CookEnvironmentAdapter` now requires captured source/artifact bytes and the
exact six settings above, including binary32 exposure agreement. The logical or
context artifact path is checked in its original spelling and must end in
`.zenvmap`; actual root mapping/identity binding remains the frozen provider's
discovery responsibility, never a post-seal native reopen. The shared
`ReadEnvironmentMapAsset`/`ReadEnvironmentMapFileVFS` CPU reader supports the
current little-endian 64-bit artifact version 2/importer version 1, rejects
nonzero reserved fields, validates exact payload size/header/source agreement,
enforces byte/dimension limits, and reads every RGBA32F value to reject NaN/Inf.
The VFS wrapper publishes its header only after checked close succeeds.

The allocation-free `Core::Hash::SourceContentHash` is shared with the existing
`MetaFileIO::ComputeHash`: its zero-seeded rapidhash chain advances on exact
4096-byte chunks and then the final partial chunk, regardless of read/update
fragmentation. This is the existing import receipt hint, not a cook identity.
The adapter also verifies source SHA-256, then fingerprints captured source and
artifact SHA-256 as specified. Neither raw HDR/EXR reimport nor GPU environment
generation is allowed. One bounded stream workspace is reused across source
hashing, complete artifact validation, and SHA-256-verified passthrough; there
is no cubemap allocation or retained leaf result block. Unit coverage includes
hash-boundary compatibility, artifacts larger than the pool, strict settings/
source/header/payload cases, short/read/write/allocation failures and VFS close.
Source validation separately charges its principal `SourceContentHash`
(including its 4096-byte partial chunk), `SHA256`, result-digest, receipt value
and token before reading captures. That reservation drains before passthrough;
it is not a child of the retained stream pool. Exact/one-byte-short external
budgets and unsupported contracts preserve prior inspection, drain stream state
and produce no sink write. Neither this gate nor the shared passthrough gate
changes the receipt boundary contract, content keys or successful package bytes.

`CookDependencyGraph` now owns compact node/edge/scene/occurrence records, copied
slot/original-path text, a typed identity hash index, and iterative finalization
workspace in one checked geometrically grown block. It reserves nothing at
initialization. All borrowed pointers/views expire on a mutating call, including
a failed call that grew retained capacity; numeric node indexes remain stable.
Document root collection uses `SceneDocument::EnumerateReferences`, validates
asset kind/field descriptors even for emitted nil fallbacks, and counts every
callback before filtering entity references or nils. The same request-wide
occurrence cap applies to discovered dependency slots. A failed scene addition
rolls back its logical scene/node/occurrence/reference counts, keeping charged
capacity reusable. Shared children retain distinct slots. Iterative canonical
leaf-first ordering/cycle detection performs no finalization allocations;
per-scene closure uses caller-supplied workspace, not recursive traversal.

`CookAssetDiscovery` drives the active lease and adapter registry from those
roots. Each unique typed node is inspected once; every original UUID/path pair
is resolved before edges commit. Captured artifacts, sidecars, and optional
environment sources live in lazily allocated blocks of 32 inline file owners,
without owner relocation or per-asset control blocks. Parsed metadata and
decoded payloads are temporary. `ReadInput` reparses captured strict metadata
into caller-owned storage, preserving both outputs on failure and requiring no
input lease after sealing. The caller still must seal before staging or reuse,
and destroy capture owners before the provider/capture store/scratch pin.
Reset retains the first capture-cleanup error for caller warning policy and
never retries abandoned names against a replacement source/root.
Discovery failures after traversal starts require discarding that attempt;
they cannot expose a completed graph/capture result. Fixtures cover shared
reachability, input teardown after seal, path disagreement, missing roots,
capture-block/scratch allocation refusal, and logical rollback.

`CookSceneCodec` now reads/verifies owned captures using exact source storage,
the routed scene parser/migrations, and one reused inline diagnostics buffer.
Returned documents retain this codec's route; destroy them before the codec.
Structured scene failure categories survive reader rollback and diagnostic-entry
overflow, without message-text matching. Shared binary layout/codec measurement
allows exact-sized output storage; encoding reparses the capture, verifies its
discovered UUID, computes output SHA-256, and writes a staging-only sink. Failed
reads/encodes preserve caller outputs. The caller still owns seal-before-encode
ordering. Fixtures include every parsing/migration/encoding allocation refusal,
configured versus native memory failure, format/decoded limits, independent
binary decoding, and scene/asset encoding after frozen-input seal, source
replacement and source-context teardown.

Scene read/encode entry paths now reserve principal temporary workspace,
document-owner, SHA-256/result and token state before allocations/capture reads.
Encode's outer reservation and its nested read/YAML reservations share one
coalesced checked ledger, outside retained slab capacity; inner phases cannot
waive their own cost or discount that slab. A single allocation-free input
preflight validates arguments, schemas, source size and limits before querying
fixed storage. Encode calls the already-validated read path rather than
repeating preflight. Resource refusal is sticky and leaves the caller's prior
document/output and sink unchanged, with drained reservations. An unsupported
allocation-route contract reports `UnsupportedBackend`, not a scene-version
error. Tests cover invalid inputs even with an unsupported budget, exact nested
boundaries, and an observed principal peak succeeding exactly and refusing one
byte below without new native allocations or changed successful binary bytes.
Complete shared scene/compatibility/binary/helper/hash/compiler-stack accounting
still requires its separate proof.

The v1 settings/schema/recipe/asset/graph/scene fingerprints now use exact typed
streaming encodings. Schema sorting uses caller-owned uint32 indexes for the
largest field table, preserving registry field order. Asset key computation
reads direct edges and child results by stable graph index, without allocating
a resolved-dependency list. Graph/scene keys use canonical identities and the
existing iterative scene closure; unrelated scene results need not be present
to fingerprint one scene. Shared object-descriptor construction and validation
enforce the digest-derived path spelling. Missing/corrupt descriptors fail with
unchanged output keys; actual file verification remains the reuse/publication
layer's obligation. Fixtures include independent typed-byte goldens, semantic
invalidation/non-invalidation, exact workspace refusals, and real post-seal
scene/asset recipe keys after source-context teardown.

Variable fingerprint helpers require the actual `ICookAllocator&` (no optional
unbudgeted overload). Each reserves its principal fixed hash/temporary/callback
state before hashing or sorting, without calling `Allocate`; refusal preserves
the prior key/result and optional closure view. Recipe construction uses one
reservation for its pending result, schema hash and callback context, not nested
public-helper reservations. Scene graph and scene-domain hashing reuse one
`CookFingerprint` with `Reset`; its eight-byte integer-encoding scratch is inline
in the charged type. Metadata/material reservations include the principal
canonical writer, SHA and metadata sorting indexes where applicable. Environment
inspection separately reserves its semantic fingerprint and pending digest.
Settings are fixed to `{}\n` in v1, so their digest is static read-only data, not
recomputed SHA state; an independent runtime hash over `CookSettingsJSON()`
checks that constant. Adapter/reuse callers preserve reservation errors as hard
resource failures, not invalid metadata, scene versions or cache misses.
Exact/one-byte-short, injected refusal, no-allocation and output-preservation
tests cover these principal-state gates; hash/JSON/sort/callback internals and
compiler-generated call frames still require separate bounds.

Canonical manifest v1 emission now streams from finalized graph/result tables,
without a DOM or output-buffer allocation. Caller-owned uint32 workspace is
reused for scene ordering and shared-object descriptor conflicts. Validation
and exact byte-limit measurement precede the first staging write; the returned
digest covers exact canonical bytes including LF. The strict ledger reader
checks full JSON syntax, Unicode and duplicate decoded keys (including unknown
nested objects) before compatibility/shape decisions. One temporary key index
and one exact retained record/text block are allocator-routed; no per-record
allocation occurs. Recognized-v1 invalid shape/semantics warn and clear records;
malformed/ambiguous or newer/unknown versions fail closed and preserve the old
owner. Closure/cycle/global UUID validation reuses the shared dependency graph,
not an independent graph algorithm. Reviewed golden/negative fixtures, exact
limits, every allocation/sink refusal, move/input lifetimes and real post-seal
scene/material manifest emission/read are covered. This validates a ledger,
not the referenced physical bytes, reuse eligibility or publication safety.

The synchronous reuse owner now compares current keys, formats, previous direct
edges and child results before accepting a cached asset; scenes additionally
check source/schema keys, roots and their complete per-scene closure. One exact
budgeted block stores result/state tables, closure workspace and compact
open-addressed verified-file indexing, without per-asset/scene allocations.
A caller-owned stream (capped at 64 KiB) checks exact size, EOF and SHA-256 through
read/checked-close authority borrowed from a retained root. Only Open's NotFound
is a recoverable miss; existing corruption and read/stat/close failures preserve
logical results and fail. Successful verifications are shared by digest/extension
within the locked immutable-file run, with size consistency enforced; misses and
errors are not cached. Forced encoding bypasses previous input-key reuse, not
installed-byte verification. Ready results cannot be rebound, and scene source
digests cannot change after preparation. Tests cover shared objects, actual hash
collisions, invalidation/isolation, inconsistent child/edge ledgers, every reuse
allocation refusal, file/native-error injection, rooted memory/disk verification,
symlink/special-file rejection and real encoded scene/material reuse after
source-context teardown. Root wrappers/snapshots and fixed stack/owner storage
remain separately charged run-integration responsibilities.

All three physical verification entry points (`VerifyCookObjectFile`,
`VerifyCookObject`, `VerifyCookStagedObject`) require the checked allocator.
Allocation-free argument/descriptor preflight precedes fixed-budget queries.
Principal SHA-256/digest/charge-token state uses one staging/manifest reservation
before stat/read or Open, retained through checked-close. The shared byte loop
borrows that state, avoiding a second hash object or nested charge; EOF uses the
existing stream. Unknown/refused fixed contracts fail before file I/O, never
become cache misses or ready results. Unsupported verification contracts report
`UnsupportedBackend`, not a scene/asset version error. Publication retains this
allocator and fails closed on staged or installed verification refusal, preserving
the previous manifest and caller result; no later call can commit the poisoned
run. Exact/one-byte-short, callback-lifetime, drain/no-allocation and memory/disk
publication regressions cover these principal-state guarantees. Helper/compiler/
hash-internal call-stack and backend wrapper storage remain separate work.

The synchronous `CookPublication` session now owns the exclusive writer directory,
requires a sealed input lease, strictly reads the previous ledger, and combines
verified reuse with streamed immutable-object installation and canonical
manifest publication. It shares the VFS atomic-file owner instead of duplicating
its temp/write/flush/checked-close/rename protocol. The same borrowed bounded
stream buffers encoding, verifies actual sealed staging bytes and installed
objects, and compares canonical manifest bytes exactly. Missing objects
regenerate; existing corrupt collisions fail without replacement. A canonical
verified no-op performs no output file writes, even with zero staging budget.
Global staging charges include new installed objects and manifest staging;
successful duplicate-temp removal releases its charge, cleanup failure retains
it conservatively. Fixed result preparation precedes the sole replacing rename;
postcommit cleanup cannot turn success into failure or retry against later state.
The publisher itself allocates no additional cook state after initialization;
producer codecs and VFS wrappers retain their separately charged allocation
policies. Principal previous-manifest read, object staging/hash, exact comparison,
prepared result and manifest staging state now have distinct allocation-free
fixed reservations before their I/O. Object staging keeps its reservation while
producer/verification work borrows separately charged state; staged and installed
verification reservations are sequential, not duplicated in the outer charge.
Prepared result and comparison state overlap, but a canonical no-op never acquires
manifest staging state. The staging sink borrows an optional hash pointer: only
object encoding constructs that SHA-256 object; the canonical manifest writer
already hashes its bytes, so its staging sink neither creates nor initializes
another hash. Invalid public workspace/producer inputs fail before fixed-budget
queries. Refusal preserves the prior package and caller result, releases earlier
phase reservations and poisons a live publication before commit. These cover
principal objects, not complete helper/manifest-writer/compiler/hash-internal
frames. Memory and POSIX disk
fault tests cover short I/O, actual staged corruption, read/write/stat/flush/
close/rename errors, malformed/future ledgers, allocation refusal, contention,
missing/corrupt existing objects, retained old manifest handles/snapshots and
cleanup warnings. A real frozen capture/discovery/codec integration publishes
after source replacement/context shutdown and verifies an independently captured
second canonical no-op.

Root-first acquisition and explicit provider-storage release now exist, including
real capture/publication and independently captured no-op integration after
source-context shutdown. Both runs create/acquire the target directory through
retained output-parent authority, without live mount resolution. Shared child
pin tests cover access preservation, path replacement/symlink rejection, bounded
pin exhaustion, parent release, and retained-context/provider lifetime.
The per-call VFS accounting policy now includes borrowed input-root state,
output/scratch/child state, fixed memory-backend pools and pinned mapping tables
without duplicate shared-domain charges. Its common engine hash index is inline;
geometric growth uses checked reusable workspaces. This policy is exercised by
the real captured publication/no-op fixture, not merely a fake allocation probe.
The public `SceneCooker::Cook` now wires this preflight and VFS policy into
capture/discovery/seal, leaf-first encoding/reuse, manifest publication and
unconditional owned cleanup. Its fixed owners, diagnostics/results and reported
input provider/frozen-owner/root-binding storage are charged before use. Common
schema/manifest sorting uses 32 inline words; larger workspace is routed/reused.
Dependency results use one geometrically grown table, only when an encoder is
actually called, with dead old backing freed before replacement. The private
scratch run uses shared VFS exclusive-name creation, not another random generator
or per-run atomic. Cleanup drains captures and provider buffers while scratch
authority and the allocator remain alive. Failure leaves caller results unchanged;
postcommit provider-cleanup errors only warn.

Public memory/POSIX-disk integration tests cover complete batches, shared assets,
texture/material path rewriting, scene binary decoding, verified no-op/forced
encoding, order/budget independence, duplicate paths/UUIDs, physical overlaps,
every routed allocation refusal, contention, corruption, unowned scratch entries
and cleanup warnings. Fixed helper/callback/parser/decoder/hash call-stack and
remaining caller-owned service storage accounting remain unfinished. Public-call
tests are separate from the independent `SceneCookProcess` gate described in section 9.
The Windows rooted backend now exists and its shared disk/native process gates
are enabled, but native Windows execution, cross-host golden equality and
remaining platform/package acceptance are still required.

### Bounded error interface

`CookDiagnostics` is caller-owned fixed storage: 64 entries, a dropped-entry
counter, and error count. Each entry has severity, stage, code, underlying
`VFSError`, logical source path (256 bytes including NUL), scene/entity UUIDs,
schema/field keys (128 bytes each), asset identity, dependency chain (32 inline
identities plus truncation flag), and a 256-byte message. No borrowed strings.
Truncate explanatory text only; an overlong identity/path is an input error,
not a silently truncated lookup key. Diagnostic overflow never hides failure.

`CookDiagnostics::Emplace` constructs the entry directly in the next retained
slot and returns a borrowed pointer for synchronous provenance/message filling.
It uses no allocator, second full diagnostic, or aggregate-reset assignment.
Code/severity are final for counter purposes on return: callers must preserve
the normalized severity and code when enriching the record. `CacheMiss` and
`CleanupWarning` normalize to warnings on both copied and emplaced paths.
At capacity, emplacement returns null, increments the saturating dropped count,
and still increments the saturating error count for errors. No provenance
construction is required for a dropped record. Clearing reconstructs retained
slots in place and resets counters. Existing copied `Add` remains alias-safe,
including a source that is the next destination slot.

Graph `Describe` validates keys and the complete explanatory UTF-8 text before
replacing provenance, writes directly into its caller's record, and preserves
every output byte on validation failure. Text may borrow from the old message
or other old text fields. Unused chain slots are cleared; long first-discovery
chains still retain the root-first prefix and the failing identity. This
eliminates duplicate principal diagnostic storage, not the separate remaining
helper/compiler/callback stack and ownership-accounting acceptance work.

Stages are `Setup`, `Capture`, `Discover`, `Encode`, `Publish`, `Cleanup`.
Severity is `Info`, `Warning`, or `Error`, default `Error`. Code 0 is `None`;
stable code names and return categories are:

| Code | `VFSError` returned on failure |
|---|---|
| `InvalidRequest`, `InvalidBudget`, `InvalidPath` | `InvalidPath` |
| `InputBusy`, `OutputBusy` | `AlreadyExists` |
| `UnsupportedBackend`, `UnsupportedTarget`, `UnsupportedVersion`, `UnsupportedAsset` | `Unsupported` |
| `InputChanged`, `IdentityConflict`, `InvalidMetadata`, `InvalidScene`, `InvalidAsset`, `DependencyCycle`, `InvalidManifest`, `CorruptObject` | `Corrupted` |
| `MissingDependency` | `NotFound` |
| `BudgetExceeded` | `SizeLimitExceeded` |
| `AllocationFailed` | `OutOfMemory` |
| `IOFailure` | Actual underlying VFS error |
| `CacheMiss`, `CleanupWarning` | Warning only; no failed result |

Numeric code ordinals are not serialized and need not be a wire ABI. Report
the actual failed phase and provenance; don't misclassify a decoder failure as
a missing file. Chains retain the first 31 ancestors and final failing identity
when longer than 32. Pre-commit cleanup can add warnings without replacing the
primary failure; post-commit cleanup only adds warnings. A malformed known-v1
manifest rebuild uses `CacheMiss`, while a future version is a hard failure.

## 9. Golden fixtures and implementation acceptance

This section defines acceptance criteria, not a chronological build report.
Record execution results, host limitations, and unresolved failures in the
associated pull request and CI runs. Passing a subset of these gates does not
establish three-host or shipping readiness.

### 9.1. Reviewed wire and package fixtures

[manifest-v1.golden.json](scene-cooker-contracts/manifest-v1.golden.json) fixes
canonical wire shape, key ordering, the real settings digest, and the final LF.
Its synthetic scene digests have no corresponding binary scene or source tree;
it is not evidence of a valid package. The
[negative cases](scene-cooker-contracts/manifest-v1.negative-cases.json) specify
raw inputs, mutations, and expected outcomes. Keep shape validation separate
from graph, fingerprint, byte-integrity, and codec validation.

Validate the schema with both Draft 2020-12 and ECMAScript pattern semantics.
Reject trailing LF/CRLF in digests, UUIDs, and object paths, and trailing LF or
embedded NUL in logical source paths. A final document LF remains valid.

The real `ZEngine/tests/Cook/Fixtures/package-v1.golden.json` anchors a
mesh/material/texture/sky package. Checked-in scene/material source snapshots
must match generated inputs byte-for-byte. `package-v1.inputs.json` fixes the
SHA-256 and size of every source, sidecar, and generated binary artifact. The
fixture uses shared serializers, a literal 1x1 BMP, and a finite six-face RGBA32F
sky. Expectations require review; never regenerate goldens just to pass a test.

`package-custom-schema.golden.json` anchors source digest/size and canonical
manifest hashes for application schema v1/v2, including every object descriptor.
Independent producers and consumers must allocate different runtime type IDs
and register schemas in different orders. The `GameSurface` UUID/float unit
vectors fix its 20-byte persisted representation; its runtime cache is excluded.
A registered identity source migration must preserve source/asset records while
changing schema/scene keys and the scene object. Missing migration must preserve
the caller result, old manifest, and package inventory without scratch/lock leaks.
Consumers accept current schemas and reject missing or non-current schemas.
Canonical binary tables are validated before `UnsupportedVersion` is reported;
malformed keys, ranges, ordering, reserved fields, and trailing bytes remain
`InvalidData`.

### 9.2. Independent processes and host portability

Run installed, relocated producer/consumer helpers through plain and Unicode
native paths, including four-byte UTF-8 characters. Consumers need no source
tree, editor, dependency checkout, or GPU. Include moved checkout roots,
changed child paths, no-op/forced rebuilds, texture-only transitive invalidation,
custom schemas, and canonical material-float vectors. Verify decoded scene,
mesh, material, texture, and sky semantics as well as physical bytes.

Compare identical captured fixture hashes, canonical manifest bytes, and every
object path/digest/size on native macOS, Linux, and Windows. A build,
cross-compilation, configured CI gate, or two-host comparison is not a substitute
for native execution on the remaining host. Missing and same-size-corrupted
objects must fail independent consumption.

### 9.3. Ownership, isolation, and strict paths

Exercise every capability/error seam with fixed old/new manifests and independent
package readers. Cover empty/duplicate scenes, unknown/newer versions,
unsupported backend capabilities, allocation failures, and caller output/result
immutability on failure.

Test input-owner mutation contention and capture violations. Inject a seal-time
recheck failure after all captures exist: no object may be staged/installed, no
manifest replaced, and no no-op accepted. Sealing is terminal; later source
mutation cannot affect captured output, and lease destruction introduces no
new input errors.

Race publication-root acquisition with mount/backend teardown and attempt
remount, unmount, and shutdown during publication and cleanup. Operations must
stay on the retained native backend/root, using anchored no-reparse access,
not a saved pathname. Pins release after file/lock cleanup. Unsupported pinning
must fail before writer-lock creation.

For scratch lifetime, seal and destroy the input lease while keeping disk
captures alive, then encode successfully. Attempt scratch remount, unmount, and
backend teardown during capture, after sealing, during encoding, and during
cleanup. Test retained-handle and pinned-reopen paths, partial acquisition
failure, unsupported pinning before run creation, and cleanup failure after
commit. All capture/file wrappers and owned cleanup attempts finish before
the scratch pin releases; no deletion may reach a replacement mount.

Raw strict-path tests accept `manifest.json`, `objects/<digest>.png`, and valid
Unicode relative names without changing bytes. Reject absolute/drive/UNC and
backslash paths, repeated separators, dot/dot-dot components, trailing slash,
embedded NUL/control bytes, invalid UTF-8, and over-limit lengths/component
counts. Exercise 255 versus 256 UTF-8 bytes and 32 versus 33 components,
including multibyte names. No aliasing or truncation is allowed; tests must
supply raw spellings, not already-normalized `VFSPath` values.

### 9.4. Publication, recovery, and bounded resources

Use concurrent processes to prove that an old open manifest and its immutable
dependency objects remain readable after replacement. Terminate producers
before and after manifest rename to verify old/new authority, writer exclusion,
refusal to steal stale locks, and non-destructive new-root recovery. Include
concurrent Windows manifest reads and failures throughout staging, commit, and
cleanup.

Exercise actual scene, mesh, material, texture, and environment resource caps,
exact-fit and one-byte-short boundaries, principal/fixed-state accounting, and
every allocation refusal without fallback, leaks, or mutation of prior outputs.
Test logical staging limits separately from native disk exhaustion. Real ENOSPC
acceptance uses a bounded native filesystem; injected errors do not replace it.
Mixed publication stress must preserve package integrity and balance ownership
under repeated success and failure.

Ledger/principal accounting is not process RSS, total stack usage, or a shipping
memory claim. Keep these measurements and their scope distinct. Rendering
acceptance remains the separate #829 gate. Completion of #718 requires the
relevant implemented profiles to satisfy these contracts and acceptance gates;
the document alone is not a readiness claim.
