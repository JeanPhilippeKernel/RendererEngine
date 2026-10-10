# Roadmap

ZEngine 1.0.0 is released. The next work focuses on completing the editor's
persistent scene-authoring workflow, hardening the renderer, and turning
implemented foundations into production-ready workflows.

This page describes direction, not dates or delivery promises. Each area links
to its current design record and issue tracker where applicable.

## Active work

| Area | Current focus | Design record |
|---|---|---|
| Scene authoring | Persistent scene documents, transactional load/save, entity identity, and cross-machine correctness | [Scene serialization](../design/future-plan/scene-serialization.md) |
| Editor interaction | Selection, undo/redo, play-mode isolation, gizmos, and scene grid | [Editor program](../design/execution-plan.md) |
| Rendering hardening | Resource lifetime, validation, GPU visibility, and real-device test coverage | [Render resource manager](../design/future-plan/render-resource-manager.md) |
| Asset workflow | Import stability, registry correctness, and the VFS-backed pipeline | [Asset pipeline](../reference/asset-pipeline.md) |
| Build and distribution | Reproducible source builds, package provenance, and future shipping contracts | [Build integration](../design/future-plan/build-integration.md) |

## Planned systems

Physics, animation, audio, scripting, networking, plugins, LOD, lightmap
baking, texture compression, and platform services have design records but are
not current product commitments. Browse the [proposal index](rfcs.md) for their
status and acceptance criteria.

## Release history

- [ZEngine 1.0.0 release](releases.md)
- [Complete changelog](https://github.com/JeanPhilippeKernel/RendererEngine/blob/main/CHANGELOG.md)
- [Historical pre-1.0 roadmap](roadmap-legacy.md)
