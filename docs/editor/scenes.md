# Scenes and authoring

Tetragrama provides the editor surface for working with scene content, including
an outliner, inspector, viewport, camera navigation, and the asset workflow.
The underlying engine uses ECS entities and components, and synchronizes scene
state to the renderer.

## Current boundary

Persistent, cross-machine scene documents are an active production program.
The repository deliberately keeps the current scene-serialization contract and
its remaining work visible rather than presenting a speculative file format as
complete user guidance.

Use the editor for the implemented workflow and treat generated `project.json`
as configuration owned by Zodiac Engine Hub. For a technical description of
scene ownership and ECS synchronization, see
[Engine architecture](../reference/architecture.md#ecs-and-simulation).

## Related material

- [Editor roadmap](../project/roadmap.md)
- [Scene serialization design](../design/future-plan/scene-serialization.md)
- [Editor play-mode design](../design/future-plan/editor-play-mode.md)
- [Editor selection design](../design/future-plan/editor-entity-selection.md)
