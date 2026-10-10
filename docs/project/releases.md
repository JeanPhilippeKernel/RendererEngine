# Releases and changelog

## ZEngine 1.0.0

**Released 2026-10-10** · [GitHub release](https://github.com/JeanPhilippeKernel/RendererEngine/releases/tag/v1.0.0)
· [Full changelog](https://github.com/JeanPhilippeKernel/RendererEngine/blob/main/CHANGELOG.md)

ZEngine 1.0.0 is the current stable release. It brings together the stable core
work across the ECS, native editor, asset pipeline, VFS, memory management, and
Vulkan rendering.

### Breaking change: VFS-backed asset import

Importer and codec writes now use VFS paths. Review the 1.0.0 migration entries
before opening an existing project with the new version.

### Install the release

1. Download and install [Zodiac Engine Hub](https://github.com/JeanPhilippeKernel/ZodiacEngineHub/releases).
2. Open the Hub's engine-management view.
3. Select **ZEngine 1.0.0** and download/install it.
4. Create or open a project from the Hub.

See [Install ZEngine with the Hub](../getting-started/install-with-zodiac-engine-hub.md)
for the complete guided path.

## Release policy

Stable releases are published from `main`; the development track publishes
release candidates from `develop`. Release versions, changelog entries, tags,
and platform artifacts are produced by the repository's release automation.

For contributor-facing detail, see [Release process](../contributing/release-process.md).
