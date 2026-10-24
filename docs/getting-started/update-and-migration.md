# Update and migration

Update an installed engine version through Zodiac Engine Hub. Before changing a
project's version, review the release notes for breaking changes and keep a
recoverable copy of the project.

## Moving to ZEngine 1.0.0

ZEngine 1.0.0 includes a breaking asset-importer change: importer and codec
writes now use VFS paths. Read the exact release entry before upgrading:

- [ZEngine 1.0.0 release](https://github.com/JeanPhilippeKernel/RendererEngine/releases/tag/v1.0.0)
- [Full changelog](https://github.com/JeanPhilippeKernel/RendererEngine/blob/main/CHANGELOG.md)

## Suggested update flow

1. Commit or back up the project before changing engine versions.
2. Install the target version through [Zodiac Engine Hub](install-with-zodiac-engine-hub.md).
3. Read the release notes and migration entries for every version being crossed.
4. Open a copy of the project with the target engine and verify asset import,
   editor startup, and launch behavior.
5. Update the project only after that verification succeeds.

For source-built projects, also rebuild the engine and refresh generated assets
before testing the project. See [Build from source](../development/build-from-source.md).
