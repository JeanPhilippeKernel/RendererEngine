# Assets and importing

The editor's asset workflow is built on the virtual file system (VFS), the asset
registry, and importer-specific processing. The editor content browser and
importer panel are the user-facing entry points; the engine reference documents
the runtime contract behind them.

## Supported source types

The current import pipeline includes these source families:

| Source | Import path |
|---|---|
| glTF and GLB | `GltfImporter` using fastgltf |
| FBX and legacy model formats | `AssimpImporter` and the ufbx-based path |
| HDR and EXR environment images | `EnvironmentMapImporter` |
| Textures referenced by imported models | Imported with their source model and registered as assets |

## Import workflow

1. Open the project through Zodiac Engine Hub.
2. Use the editor's asset workflow to choose source content from the project.
3. Select the import options appropriate to the source asset.
4. Let the editor register and cook the result through the VFS-backed pipeline.
5. Confirm the imported asset in the content browser before using it in the scene.

## VFS paths and reimporting

ZEngine 1.0.0 changed asset-importer and codec writes to VFS paths. This makes
asset routing independent from temporary source paths, but it is a breaking
change for projects moving from older versions. Read
[Update and migration](../getting-started/update-and-migration.md) before
upgrading a project with existing imported content.

For pipeline behavior, memory ownership, deduplication, and GPU binding, see
[Asset pipeline reference](../reference/asset-pipeline.md).
