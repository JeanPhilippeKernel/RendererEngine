# Engine reference

These pages describe the implemented engine contracts for contributors and
advanced users. Start with the architecture overview, then follow the subsystem
that matches the work you are doing.

<div class="grid cards" markdown>

- :material-sitemap: [**Architecture**](architecture.md)

  Threading, ECS, VFS, startup, shutdown, and cross-thread communication.

- :material-monitor-dashboard: [**Rendering**](rendering.md)

  Render graph, resource management, geometry, materials, and Vulkan lifetime.

- :material-package-variant-closed: [**Asset pipeline**](asset-pipeline.md)

  Ingest, registry, GPU material binding, and import behavior.

- :material-memory: [**Memory management**](memory-management.md)

  Arena, pool, TLSF, scratch, GPU allocation, and platform rules.

- :material-view-dashboard-outline: [**ZUI system**](zui.md)

  Retained-mode UI, layout, interaction, docking, and rendering.

- :material-code-braces: [**Containers**](containers.md)

  Arena-backed `Array`, maps, strings, ownership, and capacity rules.

</div>

For proposed work and historical design records, use
[Design documents](../design/index.md). Those pages are not an API or product
commitment unless their status says that the work is implemented.
