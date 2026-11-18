# Development progress

This is a high-level snapshot of project direction, not a release schedule.
The public roadmap and issue tracker are the sources of truth for changing
priorities. The latest released baseline is ZEngine 1.0.0.

## Current focus

- Complete and harden persistent scene authoring and editor interactions.
- Improve renderer resource lifetime, validation, and real-device coverage.
- Strengthen asset import, registry, and VFS-backed workflows.
- Improve reproducibility of source builds and engine distribution.

These are areas of ongoing work, not claims that every listed feature is
complete. Consult the current [roadmap](docs/project/roadmap.md) and
[design records](docs/design/index.md) for the latest implementation and
proposal status.

## Implemented foundations

The repository includes a C++20 engine runtime, a Vulkan renderer and render
graph, an ECS and scene system, a virtual file system, asset import and cook
components, and the Tetragrama editor. Automated checks build and test Linux,
Windows, and macOS configurations.

For code-level ownership and status of individual systems, use the
[architecture references](architecture.md#detailed-references). Planned
systems such as physics, animation, audio, scripting, networking, plugins, and
LOD remain longer-term proposals unless the roadmap or an accepted design
states otherwise.

## Keeping this page current

Update this summary when the roadmap's active themes materially change. Do not
add dates, completion percentages, or delivery commitments unless they are
approved and tracked by the project.
