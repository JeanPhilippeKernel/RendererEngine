# ZEngine — Component Reflection and Editor Metadata

**Priority:** P2 — dynamic inspector foundation
**Status:** Core reflection implemented; remaining editor, plugin, and schema work is tracked separately
**Depends on:** actor-ecs-architecture.md
**Relates to:** scene-serialization.md and editor-undo-redo.md
**Blocks:** generic inspector/component editing and plugin editor integration
**Tracked by:** [#707](https://github.com/JeanPhilippeKernel/RendererEngine/issues/707) and
[#708](https://github.com/JeanPhilippeKernel/RendererEngine/issues/708) for metadata tests,
[#759](https://github.com/JeanPhilippeKernel/RendererEngine/issues/759) for the current
inspector's hard-coded actor header, and [#705](https://github.com/JeanPhilippeKernel/RendererEngine/issues/705)
for future plugin metadata.

## 1. Scope

Reflection answers current-process editor questions:

- which reflected components belong to a live ECS entity;
- which fields can be displayed or edited;
- field labels, types, visibility, ranges, and enum data; and
- how generic editor UI can locate current component memory safely.

It is not C++ reflection, a persistence format, or an undo snapshot format.
It does not establish a stable cross-process component identity by itself.

## 2. Current and durable identities

ComponentTypeOf returns a fast runtime ComponentTypeID. It is useful for ECS
storage and in-process reflection lookup, but its allocation order is not
serializable. Scene serialization and undo field operations therefore also
require stable component and field schema keys from the serialization registry.

~~~text
runtime ComponentTypeID -> ECS storage and reflection lookup
stable component key    -> scene YAML/binary schema, plugin compatibility
stable field key        -> undoable property editing and migration
~~~

Reflection descriptors may cache ComponentTypeID and offset/size information
after validating that their metadata matches the live registered type. A field
offset is never written to a scene file or durable undo payload.

## 3. Descriptor requirements

Each reflected component declares:

- runtime ComponentTypeID and stable display/type name;
- category, tooltip, and component-level read-only/editor-only flags;
- field descriptors with label, stable field key, field type, current layout
  offset/size, editor range, visibility, and read-only state;
- enum tables or nested descriptors where applicable;
- value codec/accessor hooks for types that cannot safely be raw copied; and
- component-specific validation and side-effect notification where a direct
  memory write would be invalid.

New or changed engine-facing metadata string fields use cstring where the
engine owns the API. C ABI/plugin boundary types retain their explicit ABI
character-pointer form and lifetime requirements.

The reflection registry must reject duplicate runtime IDs, duplicate stable
keys, invalid field bounds, and descriptors whose component layout/version does
not match. Diagnostics identify the plugin or component responsible.

## 4. Inspector behavior

The ZUI inspector is generic. For each selected entity target, it reads the
current archetype/component mask, enumerates reflected registered components,
and renders supported fields. It does not require ActorManager; actor-backed and
pure ECS entities are handled by their EntityID resolved from SelectionModel.

For a single editable target:

1. find the field descriptor and stable component/field schema keys;
2. on control activation, request an EditorSession transaction and capture the
   codec-produced before value;
3. apply validated live previews while the field is edited;
4. on deactivation, commit one semantic field operation if changed; and
5. on cancellation, restore before value.

For a multi-selection, render common compatible components/fields and apply a
single atomic batch operation. Unsupported or mixed values are displayed
explicitly; the UI never silently edits a subset without telling the user.

Read-only, hidden, runtime-derived, and editor-only fields cannot become
editable merely because they have an address in component memory. A missing
ZUI widget renders an explicit read-only unsupported value instead of omitting
the data.

## 5. Component structural edits

The Add Component menu lists eligible reflected types whose runtime storage can
be created and whose schema/constructor is available. It applies an editor
transaction, not a direct UI mutation. Remove Component performs the inverse
only when component dependency rules allow it.

Required authored identity and transform components cannot be removed through a
generic raw removal path. Components containing resources, references, variable
storage, or custom invariants require their registered operation codec.

## 6. Plugin boundary

Plugins may contribute reflection metadata, but registration must include a
stable plugin/component schema identity and a compatibility version. Plugin
unload/hot reload invalidates UI pointers and any operation using the old
schema. EditorSession cancels active edits and invalidates unsafe history before
the old module is unloaded.

Reflection registration does not imply scene serialization. A plugin component
must separately register the serializer/migration/reference hooks required by
scene-serialization.md before it can be saved in a production scene.

## 7. Required tests

- Every registered field fits its live component layout and has unique stable keys.
- Reflection lookup and raw component access reject dead EntityIDs and unknown types.
- Inspector correctly renders actor-backed and pure ECS entity components.
- Single and multi-selection field edits create one reversible transaction.
- Read-only, hidden, unsupported, and mixed-value behavior is explicit.
- Add/remove respects required-component and custom codec constraints.
- Plugin registration, duplicate-key rejection, and hot-reload invalidation are safe.
- Reflection offsets are not used as persisted scene or durable history identity.

## 8. Delivery checklist

- [x] Runtime reflection registry and built-in metadata foundation
- [ ] Stable component/field schema keys linked to serializer registry
- [ ] Field codec/validation and side-effect contract
- [ ] ZUI inspector transaction bridge and multi-selection behavior
- [ ] Safe Add/Remove Component operations
- [ ] Plugin ABI/schema registration and hot-reload invalidation
- [ ] Unit and integration coverage
