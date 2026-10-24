# Run and debug

`Obelisk` is the application executable that links the engine and Tetragrama
editor. Zodiac Engine Hub normally launches it for installed engine versions;
source developers can launch it with the same project configuration handoff.

## Launch the editor

From the directory containing the built executable, pass the editor and project
configuration arguments as separate process arguments:

```text
Obelisk --launchEditor --projectConfigFile /path/to/project.json
```

Use the appropriate executable form for your platform. Quote paths that contain
spaces through your shell's normal argument rules.

## Debugging guidance

- Build the Debug configuration before investigating engine behavior.
- Start with a project created or opened through Zodiac Engine Hub so the
  generated configuration matches the installed engine.
- Keep project paths and configuration paths explicit in launch configurations.
- Rebuild after changing shaders, engine code, or generated resources.

The engine's current command-line contract accepts `--launchEditor` and
`--projectConfigFile`. See [Project configuration](project-configuration.md)
for ownership and validation responsibilities.
