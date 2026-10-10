# Project configuration

Zodiac Engine Hub generates the project configuration that ZEngine consumes at
startup. The engine receives it through the `--projectConfigFile` command-line
argument and carries the path into normal initialization.

## Ownership boundary

| Concern | Owner |
|---|---|
| Creating projects and generating `project.json` | Zodiac Engine Hub |
| Project-manager UI, registry, templates, and cache layout | Zodiac Engine Hub |
| Reading the supplied configuration during engine startup | ZEngine / Obelisk |
| Launching the editor with the configuration path | Zodiac Engine Hub or a developer launch configuration |

Do not hand-edit a Hub-generated `project.json` merely to change an engine
default. Changes to generated schema, defaults, or persistence belong to the
Hub. This keeps project creation and installed-engine selection consistent.

## Integration requirements

When another launcher or a local IDE starts the engine:

- Pass the executable path and configuration path as distinct process arguments.
- Quote path arguments according to the host platform.
- Report missing executables, missing configuration, invalid configuration, and
  launch failures with actionable diagnostics.
- Preserve project data when a launch is cancelled or fails.

For the deeper design boundary, see
[Zodiac Engine Hub / Panzerfaust integration](../design/future-plan/panzerfaust.md).
