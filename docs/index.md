<div class="ze-hero" markdown>

# Build worlds with Zodiac Engine

Zodiac Engine is a cross-platform C++20 and Vulkan engine with a native editor,
asset pipeline, virtual file system, and project tooling for Windows, macOS, and Linux.

[Install ZEngine](getting-started/install-with-zodiac-engine-hub.md){ .md-button .md-button--primary }
[Build from source](development/build-from-source.md){ .md-button }

</div>

<div class="ze-release" markdown>

**ZEngine 1.0.0 is available.** Install it through
[Zodiac Engine Hub (Panzerfaust)](https://github.com/JeanPhilippeKernel/ZodiacEngineHub/releases),
then select the engine version from the Hub. Review the
[release notes](project/releases.md) before updating an existing project.

</div>

## Start where you are

<div class="grid cards" markdown>

-   :material-download: **Install the engine**

    ---

    Download Zodiac Engine Hub, install a ZEngine version, and open a project.

    [Install with the Hub](getting-started/install-with-zodiac-engine-hub.md)

-   :material-cube-outline: **Use the editor**

    ---

    Learn the current asset-importing and scene-authoring workflow in Tetragrama.

    [Open the editor guide](editor/index.md)

-   :material-hammer-wrench: **Develop from source**

    ---

    Build, run, and debug the engine with the supported CMake presets and scripts.

    [Open the development guide](development/index.md)

-   :material-book-open-page-variant: **Understand the engine**

    ---

    Explore the engine architecture, rendering, memory, UI, and container contracts.

    [Browse the reference](reference/index.md)

</div>

## What is included

| Component | Purpose |
|---|---|
| **ZEngine** | Core runtime: ECS, Vulkan rendering, virtual file system, memory system, input, and platform services. |
| **Tetragrama** | Native editor for scene authoring and asset management. |
| **Zodiac Engine Hub / Panzerfaust** | Launcher and project manager that installs and selects engine versions, and creates or opens projects. |

## Current release

ZEngine **1.0.0** is the current stable release. It includes the 1.0 engine,
editor, and importer work. The asset-importer migration uses VFS paths; consult
the [1.0.0 changelog](project/releases.md#zengine-100) before updating existing
projects.

## Need help?

- Read the [getting-started guide](getting-started/index.md).
- Search this documentation with the control at the top of the page.
- Ask the community on [Discord](https://discord.gg/jC3GPVKKsW).
- Report confirmed bugs in the [issue tracker](https://github.com/JeanPhilippeKernel/RendererEngine/issues).
