# Create your first project

Use Zodiac Engine Hub to create or open projects. The Hub keeps the project
selection and generated configuration aligned with the engine version you have
installed.

## Create a project

1. Open Zodiac Engine Hub.
2. Choose the option to create a project.
3. Select the installed ZEngine version that the project should use.
4. Choose the project location and finish creation.
5. Open the project from the Hub to launch the editor.

## Open an existing project

1. Open Zodiac Engine Hub.
2. Choose the option to open or add a project.
3. Select the project folder.
4. Confirm the engine version, then launch it from the Hub.

The current editor and importer workflow is described in the
[editor guide](../editor/index.md). If a project was created with an earlier
engine version, read [Update and migration](update-and-migration.md) before
changing it.

!!! note
    The Hub owns generated project configuration. The engine accepts the
    `--projectConfigFile` argument at launch, but it does not prescribe a
    hand-authored `project.json` schema or Hub cache layout.
