# ZEngine — Serialized Editor Grid

**Priority:** P2 — shared scene-authoring feature
**Status:** Design
**Depends on:** scene-serialization.md, editor-undo-redo.md,
editor-entity-selection.md, gizmo-3d-pass.md, render-graph-integration.md
**Blocks:** production replacement of the current XZ-only finite grid

## 1. Goal

The grid is scene-owned reference geometry shared by every user who opens a
scene. It has arbitrary orientation, defaults to the world XZ plane, and
renders as a depth-aware analytic overlay without a finite camera-following
edge.

The grid is not an entity, not a project-level generated configuration value,
and not editor-local UI preference. Its selection, active tool, snap toggle,
hover state, and pick tokens are editor-local.

## 2. Authored settings

SceneGridSettings is serialized as part of the versioned scene document:

~~~text
enabled
origin
orientation                 normalized canonical-sign quaternion
base_spacing                finite and strictly positive
major_line_every            positive integer
minor, major, U-axis, V-axis colors
line width, fade, label, and display settings
~~~

The default has origin zero, local normal N equal to world +Y, local U equal to
world +X, and local V equal to world +Z. The orientation defines an orthonormal
U/V/N frame. Every line calculation, label, snap, and gizmo operation uses this
local frame; no implementation may hard-code X, Z, or GroundY behavior.

Schema validation rejects non-finite origin/orientation/color values, invalid
quaternions, non-positive spacing/cadence, and unsupported display enum values.
Normalization and quaternion sign canonicalization occur at serialization and
mutation boundaries to keep deterministic scene diffs.

## 3. Editing and transactions

Grid selection is explicit: Scene Settings, a dedicated grid tool, or an origin/
axis marker chooses SceneTarget::Grid. Clicking the infinite rendered plane does
not select it.

The grid uses the generic EditorSession scene-setting operation:

- Inspector edits capture codec-produced before/after SceneGridSettings.
- Gizmo translate/rotate captures one drag transaction and commits one operation.
- Scale is not a grid operation.
- Snapping happens in local U/V/N coordinates, using the captured transaction
  settings.
- Escape, lost pointer capture, target invalidation, scene replacement, and mode
  change restore the before settings and produce no history entry.

Grid edits advance the scene document revision, dirty state, save checkpoint
logic, and immutable render snapshot just like an entity edit.

## 4. Renderer

The renderer consumes an immutable SceneGridRenderSnapshot. It does not access a
mutable EditorSession, RenderScene, or ECS Scene from the render thread.

The target algorithm is:

1. reconstruct a world-space camera ray for the pixel;
2. intersect it with the configured grid plane;
3. transform the hit point into local U/V coordinates;
4. calculate minor and major grid coverage with screen derivatives for stable
   anti-aliasing and LOD;
5. draw U/V axes from local coordinates and configured colors;
6. compare with scene depth using the active projection/depth convention; and
7. fade by local distance without creating a finite geometry edge.

It is registered as an editor-overlay pass after scene tone mapping and before
final ZUI composition. Its exact color/depth inputs, transfer behavior, and
attachment compatibility are declared through the current RenderGraph lifecycle.

Near-parallel rays, pixels with no valid forward plane hit, extreme zoom, very
small/large spacing, orthographic cameras, reverse-Z/depth-range conventions,
and viewport resize must produce stable defined results.

## 5. Labels and visual behavior

Axis labels derive from the configured local U and V directions. Default labels
may display X and Z because the default frame matches world XZ, but rotating the
grid must change the semantic label presentation or use neutral U/V labels.

Major/minor contrast, axis colors, fade, and line widths must remain usable with
editor themes and color-vision accessibility. The grid respects depth occlusion;
an explicit x-ray/grid-through-geometry mode would be a separate setting.

## 6. Required validation

- Default scene round-trip yields world XZ settings.
- A rotated/translated grid round-trips across a fresh process and another user
  sees the same plane and style.
- Inspector and gizmo operations undo/redo exactly; cancellation changes neither
  history nor dirty state.
- Local U/V/N snapping is correct under arbitrary rotation.
- Grid remains stable across camera position, perspective/orthographic views,
  high-DPI scale, resize, and extreme valid spacing.
- Reference images verify derivative LOD, labels, depth occlusion, fade, and no
  finite quad edge.
- GPU validation confirms declared render-graph resource use and no mutable
  main-thread data access.

## 7. Delivery checklist

- [ ] SceneGridSettings schema, codec, validation, and migration
- [ ] Scene Settings UI and SceneTarget::Grid selection
- [ ] Undoable inspector and translate/rotate gizmo adapter
- [ ] Immutable grid render snapshot
- [ ] Analytic ray-plane derivative renderer and depth integration
- [ ] Local-frame labels and accessibility settings
- [ ] Serializer, transaction, visual-reference, and validation-layer tests
