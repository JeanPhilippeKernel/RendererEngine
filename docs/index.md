<div class="ze-home" markdown>

<div class="ze-constellation" aria-hidden="true">
  <svg viewBox="0 0 1200 480" preserveAspectRatio="xMidYMid slice" focusable="false">
    <g class="ze-constellation__lines ze-constellation__lines--left">
      <path d="M 70 280 L 180 155 L 300 215 L 410 92 L 505 180" />
      <path d="M 180 155 L 245 70 L 410 92" />
      <path d="M 300 215 L 365 350 L 505 180" />
    </g>
    <g class="ze-constellation__lines ze-constellation__lines--right">
      <path d="M 655 305 L 745 175 L 865 250 L 990 130 L 1125 208" />
      <path d="M 745 175 L 805 82 L 990 130" />
      <path d="M 865 250 L 930 375 L 1125 208" />
    </g>
    <g class="ze-constellation__lines ze-constellation__lines--crown">
      <path d="M 425 420 L 515 300 L 600 365 L 690 300 L 780 420" />
    </g>
    <g class="ze-constellation__stars">
      <circle cx="70" cy="280" r="4" />
      <circle cx="180" cy="155" r="6" />
      <circle cx="245" cy="70" r="3" />
      <circle cx="300" cy="215" r="4" />
      <circle cx="365" cy="350" r="3" />
      <circle cx="410" cy="92" r="7" />
      <circle cx="505" cy="180" r="4" />
      <circle cx="655" cy="305" r="4" />
      <circle cx="745" cy="175" r="6" />
      <circle cx="805" cy="82" r="3" />
      <circle cx="865" cy="250" r="4" />
      <circle cx="930" cy="375" r="3" />
      <circle cx="990" cy="130" r="7" />
      <circle cx="1125" cy="208" r="4" />
      <circle cx="425" cy="420" r="3" />
      <circle cx="515" cy="300" r="5" />
      <circle cx="600" cy="365" r="4" />
      <circle cx="690" cy="300" r="5" />
      <circle cx="780" cy="420" r="3" />
    </g>
    <g class="ze-constellation__dust">
      <circle cx="120" cy="76" r="2" />
      <circle cx="390" cy="252" r="2" />
      <circle cx="555" cy="72" r="2" />
      <circle cx="636" cy="135" r="2" />
      <circle cx="835" cy="410" r="2" />
      <circle cx="1060" cy="342" r="2" />
    </g>
    <g class="ze-geometry">
      <path d="M 80 84 L 178 52 L 234 128 L 170 206 L 74 168 Z" />
      <path d="M 84 292 L 177 248 L 255 305 L 212 390 L 108 384 Z" />
      <path d="M 954 72 L 1082 98 L 1128 196 L 1040 250 L 940 178 Z" />
      <path d="M 924 294 L 1010 252 L 1118 300 L 1124 390 L 1010 430 L 918 382 Z" />
      <path d="M 332 44 L 390 98 L 370 178 L 286 196 L 248 126 Z" />
      <path d="M 762 298 L 828 254 L 900 298 L 886 378 L 804 404 L 742 358 Z" />
    </g>
  </svg>
</div>

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

</div>
